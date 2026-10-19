// log.cpp — buffer circular de logs e formatação de logf (ADR-0011). Portátil: sem includes
// Arduino; o mutex segue o mesmo padrão de snapshot.cpp (FreeRTOS no ESP32, std::mutex nos testes).
#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "api.h"

namespace onix {

namespace {

LogSink g_sink = nullptr;

// Copia `src` para `dst` truncando e sempre terminando em NUL. `src` nulo vira "".
void copyTruncated(char* dst, std::size_t dstSize, const char* src) {
  if (dstSize == 0) {
    return;
  }
  if (src == nullptr) {
    src = "";
  }
  std::size_t n = std::strlen(src);
  if (n > dstSize - 1) {
    n = dstSize - 1;
  }
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}

// Acrescenta `text` ao JSON escapando aspas, barra invertida e controles < 0x20.
// Bytes >= 0x80 passam sem alteração (UTF-8 já válido vindo do chamador).
void appendJsonEscaped(std::string& out, const std::string& text) {
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
          out += escaped;
        } else {
          out += raw;
        }
        break;
    }
  }
}

}  // namespace

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "error";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Info:
      return "info";
    case LogLevel::Debug:
      return "debug";
  }
  return "info";
}

LogBuffer::LogBuffer() {
#if ONIX_HAS_FREERTOS_MUTEX
  mutex_ = xSemaphoreCreateMutex();
  configASSERT(mutex_);
#endif
}

LogBuffer::~LogBuffer() {
#if ONIX_HAS_FREERTOS_MUTEX
  vSemaphoreDelete(mutex_);
#endif
}

void LogBuffer::append(uint64_t uptimeMs, LogLevel level, const char* tag, const char* message) {
#if ONIX_HAS_FREERTOS_MUTEX
  // Espera limitada: se o dono do mutex foi suspenso (OTA suspende a task OBD), a linha é
  // descartada em vez de travar quem loga. A linha já saiu no sink antes de chegar aqui.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
    return;
  }
#else
  std::lock_guard<std::mutex> lock(mutex_);
#endif
  Slot& slot = slots_[next_];
  slot.uptimeMs = uptimeMs;
  slot.level = level;
  copyTruncated(slot.tag, sizeof slot.tag, tag);
  copyTruncated(slot.message, sizeof slot.message, message);

  next_ = (next_ + 1) % ONIX_LOG_CAPACITY;
  if (count_ == ONIX_LOG_CAPACITY) {
    ++dropped_;  // sobrescreveu a entrada mais antiga
  } else {
    ++count_;
  }
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreGive(mutex_);
#endif
}

std::vector<LogEntry> LogBuffer::snapshot() const {
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreTake(mutex_, portMAX_DELAY);
#else
  std::lock_guard<std::mutex> lock(mutex_);
#endif
  std::vector<LogEntry> entries;
  entries.reserve(count_);
  // Com buffer cheio o mais antigo está em next_; com buffer parcial, começa em 0.
  const std::size_t start = (next_ + ONIX_LOG_CAPACITY - count_) % ONIX_LOG_CAPACITY;
  for (std::size_t i = 0; i < count_; ++i) {
    const Slot& slot = slots_[(start + i) % ONIX_LOG_CAPACITY];
    LogEntry entry;
    entry.uptimeMs = slot.uptimeMs;
    entry.level = slot.level;
    entry.tag = slot.tag;
    entry.message = slot.message;
    entries.push_back(entry);
  }
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreGive(mutex_);
#endif
  return entries;
}

uint32_t LogBuffer::droppedCount() const {
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t dropped = dropped_;
  xSemaphoreGive(mutex_);
  return dropped;
#else
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
#endif
}

void LogBuffer::clear() {
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreTake(mutex_, portMAX_DELAY);
#else
  std::lock_guard<std::mutex> lock(mutex_);
#endif
  next_ = 0;
  count_ = 0;
  dropped_ = 0;
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreGive(mutex_);
#endif
}

std::string serializeLogsJson(const std::vector<LogEntry>& entries, uint32_t dropped,
                              uint64_t uptimeMs) {
  // Reserva pelo conteúdo real, não pelo pior caso: com o heap do ESP32 fragmentado (maior bloco
  // ~18 KB com BT + Wi-Fi), 64 x 216 B de reserva fixa ficaria perto do limite.
  std::size_t expected = 64;
  for (const LogEntry& entry : entries) {
    expected += 72 + entry.tag.size() + entry.message.size();
  }
  std::string json;
  json.reserve(expected);
  json += "{\"uptimeMs\":";
  json += std::to_string(uptimeMs);
  json += ",\"dropped\":";
  json += std::to_string(dropped);
  json += ",\"entries\":[";
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const LogEntry& entry = entries[i];
    if (i > 0) {
      json += ',';
    }
    json += "{\"uptimeMs\":";
    json += std::to_string(entry.uptimeMs);
    json += ",\"level\":\"";
    json += logLevelName(entry.level);
    json += "\",\"tag\":\"";
    appendJsonEscaped(json, entry.tag);
    json += "\",\"message\":\"";
    appendJsonEscaped(json, entry.message);
    json += "\"}";
  }
  json += "]}";
  return json;
}

LogBuffer& globalLogBuffer() {
  static LogBuffer buffer;
  return buffer;
}

void setLogSink(LogSink sink) { g_sink = sink; }

void logf(LogLevel level, const char* tag, const char* format, ...) {
  if (static_cast<uint8_t>(level) > ONIX_LOG_MIN_LEVEL) {
    return;
  }
  const char* safeTag = tag != nullptr ? tag : "";

  char message[ONIX_LOG_MESSAGE_MAX];
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof message, format, args);
  va_end(args);

  if (g_sink != nullptr) {
    char line[ONIX_LOG_MESSAGE_MAX + 32];
    std::snprintf(line, sizeof line, "[%s][%s] %s", logLevelName(level), safeTag, message);
    g_sink(line);
  }
  globalLogBuffer().append(monotonicUptimeMs(), level, safeTag, message);
}

}  // namespace onix
