// log_file.cpp — lote em RAM, formatação, parse e rotação do histórico persistido (ADR-0018).
// Portátil: sem includes Arduino/FS; o mutex segue o mesmo padrão de log.cpp (FreeRTOS no ESP32,
// std::mutex nos testes).
#include "log_file.h"

#include <cstdio>
#if !ONIX_HAS_FREERTOS_MUTEX
#include <mutex>
#endif
#include <string>
#include <utility>
#include <vector>

#include "flat_json.h"  // appendJsonEscaped

namespace onix {

namespace {

constexpr std::size_t LINE_MAX = ONIX_LOG_MESSAGE_MAX + ONIX_LOG_TAG_MAX + 40;

// Data UTC de um epoch em ms como "AAAA-MM-DDTHH:MM:SSZ". Relógio não sincronizado (0) vira
// "----------------Z". Implementação civil a partir do Unix epoch, sem depender de <ctime>
// (que no ESP32 reflete o fuso e exige a hora já sincronizada).
std::string formatUtcTimestamp(uint64_t epochMs) {
  if (epochMs == 0) {
    return std::string(16, '-') + "Z";
  }
  const uint64_t seconds = epochMs / 1000;
  const uint32_t secsOfDay = static_cast<uint32_t>(seconds % 86400);
  const uint32_t days = static_cast<uint32_t>(seconds / 86400);
  const uint32_t hour = secsOfDay / 3600;
  const uint32_t minute = (secsOfDay % 3600) / 60;
  const uint32_t second = secsOfDay % 60;

  // Algoritmo civil_from_days (Howard Hinnant): dias desde 1970-01-01 -> y/m/d.
  const int64_t z = static_cast<int64_t>(days) + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp < 10 ? mp + 3 : mp - 9;
  const int64_t year = y + (month <= 2 ? 1 : 0);

  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02uT%02u:%02u:%02uZ",
                static_cast<long long>(year), static_cast<unsigned>(month),
                static_cast<unsigned>(day), static_cast<unsigned>(hour),
                static_cast<unsigned>(minute), static_cast<unsigned>(second));
  return buffer;
}

}  // namespace

LogFileStore::LogFileStore() {
#if ONIX_HAS_FREERTOS_MUTEX
  mutex_ = xSemaphoreCreateMutex();
  configASSERT(mutex_);
#endif
}

LogFileStore::~LogFileStore() {
#if ONIX_HAS_FREERTOS_MUTEX
  vSemaphoreDelete(mutex_);
#endif
}

void LogFileStore::offer(LogLevel level, const char* line) {
  // Só warn+ entra no arquivo: as métricas info de 30 s e os info de rotina ficam na RAM/Serial.
  if (static_cast<uint8_t>(level) > static_cast<uint8_t>(LogLevel::Warn)) {
    return;
  }
  if (line == nullptr) {
    line = "";
  }
#if ONIX_HAS_FREERTOS_MUTEX
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
    return;  // dono suspenso (OTA): descarta a linha em vez de travar quem loga
  }
#else
  std::lock_guard<std::mutex> lock(mutex_);
#endif
  if (count_ < ONIX_LOG_FILE_BATCH) {
    lines_[next_] = line;
    next_ = (next_ + 1) % ONIX_LOG_FILE_BATCH;
    ++count_;
  } else {
    ++dropped_;  // lote cheio: a mais nova é perdida (a versão no Serial permanece)
  }
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreGive(mutex_);
#endif
}

std::vector<std::string> LogFileStore::takePending() {
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreTake(mutex_, portMAX_DELAY);
#else
  std::lock_guard<std::mutex> lock(mutex_);
#endif
  std::vector<std::string> out;
  out.reserve(count_);
  const std::size_t start = (next_ + ONIX_LOG_FILE_BATCH - count_) % ONIX_LOG_FILE_BATCH;
  for (std::size_t i = 0; i < count_; ++i) {
    out.push_back(lines_[(start + i) % ONIX_LOG_FILE_BATCH]);
  }
  next_ = 0;
  count_ = 0;
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreGive(mutex_);
#endif
  return out;
}

std::size_t LogFileStore::pending() const {
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const std::size_t pendingCount = count_;
  xSemaphoreGive(mutex_);
  return pendingCount;
#else
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
#endif
}

uint32_t LogFileStore::droppedCount() const {
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

void LogFileStore::clear() {
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreTake(mutex_, portMAX_DELAY);
#else
  std::lock_guard<std::mutex> lock(mutex_);
#endif
  for (std::string& line : lines_) {
    line.clear();
  }
  next_ = 0;
  count_ = 0;
  dropped_ = 0;
#if ONIX_HAS_FREERTOS_MUTEX
  xSemaphoreGive(mutex_);
#endif
}

LogFileStore& globalLogFileStore() {
  static LogFileStore store;
  return store;
}

namespace {

LogFileClock g_clock = nullptr;

}  // namespace

void setLogFileClock(LogFileClock clock) { g_clock = clock; }

uint64_t currentLogFileTimestampMs() { return g_clock != nullptr ? g_clock() : 0; }

std::string formatLogFileLine(uint64_t timestampMs, LogLevel level, const char* tag,
                              const char* message) {
  const char* safeTag = tag != nullptr ? tag : "";
  const char* safeMessage = message != nullptr ? message : "";
  std::string line;
  line.reserve(LINE_MAX);
  line += formatUtcTimestamp(timestampMs);
  line += " [";
  line += logLevelName(level);
  line += "][";
  line += safeTag;
  line += "] ";
  line += safeMessage;
  return line;
}

void logFileLine(uint64_t timestampMs, LogLevel level, const char* tag, const char* message) {
  globalLogFileStore().offer(level, formatLogFileLine(timestampMs, level, tag, message).c_str());
}

std::size_t segmentEnd(const std::string& text, std::size_t limit) {
  if (text.size() <= limit) {
    return std::string::npos;  // ainda cabe: não precisa cortar
  }
  std::size_t newest = std::string::npos;
  for (std::size_t i = 0; i < limit && i < text.size(); ++i) {
    if (text[i] == '\n') {
      newest = i;  // guarda o '\n' para fechar a última linha inteira
    }
  }
  return newest;
}

std::size_t segmentsToDrop(const std::array<std::size_t, ONIX_LOG_FILE_SEGMENTS>& sizes) {
  // O segmento mais novo é o 0. A rotação empurra 0 -> 1 -> ... e descarta o último quando a
  // cota estoura; `segmentsToDrop` conta quantos arquivos precisam sumir para o conjunto caber.
  std::size_t total = 0;
  std::size_t existing = 0;
  for (const std::size_t size : sizes) {
    total += size;
    if (size > 0) {
      ++existing;
    }
  }
  if (total == 0 && existing == 0) {
    return 0;  // nada a rotacionar ainda
  }
  if (total <= ONIX_LOG_FILE_SEGMENT_BYTES * ONIX_LOG_FILE_SEGMENTS) {
    return 0;
  }
  // Cota estourada: remove os mais antigos (maiores índices) até caber.
  const std::size_t cap = ONIX_LOG_FILE_SEGMENT_BYTES * ONIX_LOG_FILE_SEGMENTS;
  std::size_t toDrop = 0;
  std::size_t remaining = total;
  for (std::size_t index = ONIX_LOG_FILE_SEGMENTS; index-- > 0 && remaining > cap;) {
    remaining -= sizes[index] < remaining ? sizes[index] : remaining;
    ++toDrop;
  }
  return toDrop;
}

std::vector<LogEntry> parseLogFileLines(const std::vector<std::string>& lines) {
  std::vector<LogEntry> entries;
  entries.reserve(lines.size());
  for (const std::string& raw : lines) {
    // Formato: "TS [level][tag] message"; a tag termina no primeiro ']' após "[level][".
    const std::size_t bracket = raw.find("][");
    if (bracket == std::string::npos || bracket == 0) {
      continue;
    }
    const std::size_t levelStart = raw.rfind('[', bracket);
    if (levelStart == std::string::npos || levelStart >= bracket) {
      continue;
    }
    const std::size_t tagEnd = raw.find(']', bracket + 2);
    if (tagEnd == std::string::npos || bracket + 2 > tagEnd) {
      continue;
    }
    // Depois de "[tag]" vem um espaço, e a mensagem começa em tagEnd + 2.
    const std::size_t messageStart = tagEnd + 2;

    const std::string levelName = raw.substr(levelStart + 1, bracket - levelStart - 1);
    LogLevel level = LogLevel::Info;
    if (levelName == "error") {
      level = LogLevel::Error;
    } else if (levelName == "warn") {
      level = LogLevel::Warn;
    } else if (levelName == "info") {
      level = LogLevel::Info;
    } else if (levelName == "debug") {
      level = LogLevel::Debug;
    } else {
      continue;  // nível desconhecido: linha corrompida
    }

    LogEntry entry;
    entry.level = level;
    entry.tag = raw.substr(bracket + 2, tagEnd - (bracket + 2));
    entry.message = raw.substr(messageStart, std::string::npos);
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::string serializeLogHistoryJson(const std::vector<LogEntry>& ramEntries, uint32_t droppedRam,
                                    const std::vector<LogEntry>& fileEntries, uint32_t droppedLack,
                                    std::size_t persistedBytes, uint64_t uptimeMs) {
  std::size_t expected = 96 + ramEntries.size() * 72 + fileEntries.size() * 72;
  std::string json;
  json.reserve(expected);
  json += "{\"uptimeMs\":";
  json += std::to_string(uptimeMs);
  json += ",\"dropped\":";
  json += std::to_string(droppedRam);
  json += ",\"droppedLack\":";
  json += std::to_string(droppedLack);
  json += ",\"persisted\":";
  json += std::to_string(persistedBytes);
  json += ",\"entries\":[";
  bool first = true;
  for (const LogEntry& entry : ramEntries) {
    if (!first) {
      json += ',';
    }
    first = false;
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
  json += "],\"history\":[";
  for (std::size_t i = 0; i < fileEntries.size(); ++i) {
    if (i > 0) {
      json += ',';
    }
    const LogEntry& entry = fileEntries[i];
    json += "{\"level\":\"";
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

}  // namespace onix
