// log_file.h — histórico de logs persistido no LittleFS (ADR-0018, SPEC-0004): formatação,
// rotação e parse do arquivo, tudo portátil e sem I/O. O File/LittleFS fica só em main.cpp, que
// implementa LogFileSinkPort; aqui mora a política testável no env `native` (mesmo padrão de
// wifi_config.h/bt_config.h).
//
// Duas responsabilidades separadas:
//   1. LogFileStore — acumula em RAM o lote de linhas warn+ pendentes (offer/takePending), sem I/O;
//   2. funções livres — rotação, parse e serialização, puras e determinísticas.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "log.h"
#include "snapshot.h"  // ONIX_HAS_FREERTOS_MUTEX e includes do mutex

namespace onix {

// Porta de escrita do arquivo (implementada em main.cpp sobre LittleFS; fakes nos testes).
// Nenhuma implementação que faz I/O deve ser chamada pelo caminho quente do log: só o flush do
// loop() (a cada ONIX_LOG_FILE_FLUSH_MS) fala com a porta.
class LogFileSinkPort {
 public:
  virtual ~LogFileSinkPort() = default;

  // FS montado e com espaço acima do piso: `false` faz o flush pular o lote (as linhas ficam
  // pendentes) sem corromper nada.
  virtual bool ready() const = 0;

  // Acrescenta uma linha completa (com '\n') ao segmento `index`. Devolve `false` se a escrita
  // falhou ou o arquivo cresceu além de ONIX_LOG_FILE_SEGMENT_BYTES (o chamador rota).
  virtual bool append(std::size_t index, const std::string& line) = 0;

  // Tamanho atual do segmento em bytes (0 se não existe) — usado para decidir a rotação.
  virtual std::size_t size(std::size_t index) const = 0;

  // Rotação: apaga o segmento `index` e renomeia `from` para `index`.
  virtual bool moveTo(std::size_t from, std::size_t index) = 0;

  // Remove o segmento `index` (rotação do mais antigo quando a cota estoura).
  virtual bool remove(std::size_t index) = 0;
};

// Lote em RAM das linhas warn+ ainda não gravadas. Nunca aloca (array fixo) e nunca faz I/O:
// `offer` é seguro de chamar do sink Serial, inclusive da task OBD.
class LogFileStore {
 public:
  LogFileStore();
  ~LogFileStore();

  LogFileStore(const LogFileStore&) = delete;
  LogFileStore& operator=(const LogFileStore&) = delete;

  // Aceita a linha já formatada (sem '\n') se o nível for warn+ (Error/Warn); descarta os demais
  // (as métricas info de 30 s ficam só na RAM/Serial). Quando o lote enche, a mais nova é perdida
  // e contada em droppedCount() — a linha continua no Serial, não vale bloquear.
  void offer(LogLevel level, const char* line);

  // Cópia das linhas pendentes (mais antiga primeiro) e limpeza do lote. Chamar do flush, fora do
  // caminho quente.
  std::vector<std::string> takePending();

  std::size_t pending() const;
  uint32_t droppedCount() const;

  void clear();  // usado nos testes

 private:
  std::array<std::string, ONIX_LOG_FILE_BATCH> lines_;
  std::size_t next_ = 0;
  std::size_t count_ = 0;
  uint32_t dropped_ = 0;
#if ONIX_HAS_FREERTOS_MUTEX
  SemaphoreHandle_t mutex_ = nullptr;
#else
  mutable std::mutex mutex_;
#endif
};

// Porta de leitura do histórico (implementada em main.cpp sobre LittleFS; fake nos testes).
// Usada pelas rotas GET /api/logs e GET /api/logs/download, sempre fora do caminho do log.
class LogHistoryPort {
 public:
  virtual ~LogHistoryPort() = default;

  // Entradas mais antiga -> mais nova, já parseadas das linhas do arquivo.
  virtual std::vector<LogEntry> history() const = 0;

  // Concatenação crua dos segmentos (mais antigo -> mais novo) para o download.
  virtual std::string dump() const = 0;

  // Bytes persistidos no total (soma dos segmentos).
  virtual std::size_t persistedBytes() const = 0;
};

// Fila global do firmware (uma por processo); ligada ao sink Serial em main.cpp.
LogFileStore& globalLogFileStore();

// Ponto de entrada único da persistência: formata `TS [level][tag] message` com o timestamp
// injetado e oferece ao lote global. Chamado pelo sink Serial (mesma linha do Serial, sem
// duplicar formatação) e pelo boot. Nunca faz I/O.
void logFileLine(uint64_t timestampMs, LogLevel level, const char* tag, const char* message);

// Relógio de parede injetável (epoch em ms) usado por logFileLine quando o chamador não passa um
// timestamp explícito. ESP32: time(nullptr) * 1000 (0 enquanto o NTP não sincronizou -> a linha
// sai com "----------------Z"). Nativo/testes: 0 por padrão.
using LogFileClock = uint64_t (*)();
void setLogFileClock(LogFileClock clock);
uint64_t currentLogFileTimestampMs();

// ---- Funções puras (sem estado, sem I/O) ----------------------------------------------------

// Formata uma linha do arquivo: "2026-10-20T18:36:00Z [warn][bt] mensagem". `timestampMs` é epoch
// em ms (UTC); com o relógio ainda não sincronizado (0), usa "----------------Z".
std::string formatLogFileLine(uint64_t timestampMs, LogLevel level, const char* tag,
                              const char* message);

// Último '\n' antes de `limit` em `text` (o offset do '\n' ou npos): evita gravar linha pela
// metade quando o segmento está perto do teto.
std::size_t segmentEnd(const std::string& text, std::size_t limit);

// Decide a rotação a partir dos tamanhos atuais (0 = inexistente). Devolve quantos segmentos serão
// removidos do mais antigo antes de escrever em 0 (a cota só é reavaliada com o arquivo cheio).
std::size_t segmentsToDrop(const std::array<std::size_t, ONIX_LOG_FILE_SEGMENTS>& sizes);

// Parse de "TS [level][tag] msg" -> LogEntry com level/tag/message (o carimbo UTC fica no próprio
// texto; `uptimeMs` sai 0, pois o histórico persistido só é exibido com o TS). Linhas fora do
// formato são puladas. Usado pela rota HTTP para reaproveitar o histórico.
std::vector<LogEntry> parseLogFileLines(const std::vector<std::string>& lines);

// Corpo de GET /api/logs com o histórico do arquivo anexado ao buffer em RAM:
// {"uptimeMs":N,"dropped":N,"droppedLack":N,"persisted":N,"entries":[...]}
// `persisted` e `droppedLack` vêm da porta/armazenamento (0 quando não há persistência).
std::string serializeLogHistoryJson(const std::vector<LogEntry>& ramEntries, uint32_t droppedRam,
                                    const std::vector<LogEntry>& fileEntries, uint32_t droppedLack,
                                    std::size_t persistedBytes, uint64_t uptimeMs);

// ---- Constantes de caminho ------------------------------------------------------------------

// "/logs.N.txt" (N = 0..ONIX_LOG_FILE_SEGMENTS-1); o mais novo é sempre o 0.
inline std::string logFilePath(std::size_t index) {
  return std::string(ONIX_LOG_FILE_PATH_PREFIX) + std::to_string(index) + ".txt";
}

}  // namespace onix
