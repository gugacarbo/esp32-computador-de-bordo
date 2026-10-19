// log.h — logging com níveis do firmware (ADR-0011): cada linha vai para um sink (Serial no
// ESP32) e para um buffer circular em RAM, lido por GET /api/logs. Cabeçalho portátil: ESP32 usa
// mutex FreeRTOS, testes nativos usam std::mutex (mesmo padrão de snapshot.h).
//
// Memória fixa: o buffer guarda ONIX_LOG_CAPACITY entradas com texto truncado em
// ONIX_LOG_MESSAGE_MAX bytes; nenhuma alocação acontece em append(). Quando cheio, a entrada mais
// antiga é sobrescrita e contada em droppedCount().
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "snapshot.h"  // ONIX_HAS_FREERTOS_MUTEX e includes do mutex

namespace onix {

enum class LogLevel : uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// "error" | "warn" | "info" | "debug"
const char* logLevelName(LogLevel level);

// Entrada copiada para fora do buffer (snapshot()).
struct LogEntry {
  uint64_t uptimeMs = 0;
  LogLevel level = LogLevel::Info;
  std::string tag;      // truncada em ONIX_LOG_TAG_MAX - 1 caracteres
  std::string message;  // truncada em ONIX_LOG_MESSAGE_MAX - 1 caracteres
};

class LogBuffer {
 public:
  LogBuffer();
  ~LogBuffer();

  LogBuffer(const LogBuffer&) = delete;
  LogBuffer& operator=(const LogBuffer&) = delete;

  // Copia tag/mensagem (truncando) para o slot seguinte sob mutex curto; nunca aloca nem faz I/O.
  // tag ou message nulos viram "".
  void append(uint64_t uptimeMs, LogLevel level, const char* tag, const char* message);

  // Cópia das entradas retidas, da mais antiga para a mais nova.
  std::vector<LogEntry> snapshot() const;

  // Total de entradas sobrescritas desde o boot (perdidas por falta de espaço).
  uint32_t droppedCount() const;

  void clear();  // usado nos testes

 private:
  struct Slot {
    uint64_t uptimeMs = 0;
    LogLevel level = LogLevel::Info;
    char tag[ONIX_LOG_TAG_MAX] = {};
    char message[ONIX_LOG_MESSAGE_MAX] = {};
  };

  std::array<Slot, ONIX_LOG_CAPACITY> slots_{};
  std::size_t next_ = 0;   // próximo slot a escrever
  std::size_t count_ = 0;  // slots ocupados (<= ONIX_LOG_CAPACITY)
  uint32_t dropped_ = 0;
#if ONIX_HAS_FREERTOS_MUTEX
  SemaphoreHandle_t mutex_ = nullptr;
#else
  mutable std::mutex mutex_;
#endif
};

// Corpo de GET /api/logs:
// {"uptimeMs":N,"dropped":N,"entries":[{"uptimeMs":N,"level":"info","tag":"obd","message":"..."}]}
// Strings escapadas para JSON (aspas, barra invertida e controles < 0x20 como \uXXXX).
std::string serializeLogsJson(const std::vector<LogEntry>& entries, uint32_t dropped,
                              uint64_t uptimeMs);

// Buffer global do firmware (um por processo).
LogBuffer& globalLogBuffer();

// Sink da linha formatada "[nível][tag] mensagem" (sem '\n'). ESP32: main.cpp instala um que
// escreve no Serial. Nativo: padrão nulo (sem saída). nullptr desativa o sink.
using LogSink = void (*)(const char* line);
void setLogSink(LogSink sink);

// Formata com printf, descarta se level > ONIX_LOG_MIN_LEVEL, envia ao sink e grava no buffer
// global com monotonicUptimeMs(). Seguro para chamar da task OBD e do loop(); nunca bloqueia
// além do mutex curto do buffer.
void logf(LogLevel level, const char* tag, const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

}  // namespace onix

#define ONIX_LOGE(tag, ...) ::onix::logf(::onix::LogLevel::Error, tag, __VA_ARGS__)
#define ONIX_LOGW(tag, ...) ::onix::logf(::onix::LogLevel::Warn, tag, __VA_ARGS__)
#define ONIX_LOGI(tag, ...) ::onix::logf(::onix::LogLevel::Info, tag, __VA_ARGS__)
#define ONIX_LOGD(tag, ...) ::onix::logf(::onix::LogLevel::Debug, tag, __VA_ARGS__)
