// obd.h — coleta OBD da task única (ADR-0003, ADR-0004, SPEC-0001).
//
// A máquina de sessão (`ObdSession`) é testável sem hardware: transporte, relógio, log e
// publicação entram por injeção. `runObdTask` é o loop de produção do ESP32 e depende de
// BluetoothSerial + ELMduino, ambos usados por uma única task (ADR-0004).
//
// A identidade Bluetooth (nome, PIN, MAC, nome local e timeouts) vem de config.h
// (SPEC-0001 §Configuração); este módulo a consome por ObdConfig e não a redefine.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "snapshot.h"

namespace onix {

constexpr uint16_t ONIX_OBD_PAYLOAD_LEN = 128;
constexpr uint16_t ONIX_OBD_AT_TIMEOUT_MS = 1000;
constexpr uint32_t ONIX_OBD_IDLE_WAIT_MS = 200;  // teto da espera cooperativa sem trabalho
constexpr uint32_t ONIX_OBD_LOG_REPEAT_WINDOW_MS = 5000;  // não repetir a mesma mensagem na janela
constexpr uint8_t ONIX_OBD_SUPPORT_MAP_MAX_ATTEMPTS = 3;  // tentativas por base, por sessão

struct SensorPidSpec {
  SensorId sensor;
  uint8_t pid;
  uint8_t expectedBytes;
  double scale;
  double bias;
};

// Um Serviço 01 por sensor, na ordem de sensorIndex (SPEC-0001 §Sensores).
constexpr std::array<SensorPidSpec, SENSOR_COUNT> OBD_SENSOR_PIDS = {{
    {SensorId::Rpm, 0x0C, 2, 0.25, 0.0},  // (256A+B)/4 rpm (SAE J1979)
    {SensorId::Speed, 0x0D, 1, 1.0, 0.0},
    {SensorId::Coolant, 0x05, 1, 1.0, -40.0},
    {SensorId::Throttle, 0x11, 1, 100.0 / 255.0, 0.0},
    {SensorId::Map, 0x0B, 1, 1.0, 0.0},
    {SensorId::Voltage, 0x42, 2, 0.001, 0.0},  // PID 42: tensão da ECU, só se configurado
}};

// Bases de mapa de suporte permitidas (SPEC-0001 §Limite de comandos).
constexpr uint8_t OBD_SUPPORT_MAP_BASES[3] = {0x00, 0x20, 0x40};
constexpr std::size_t OBD_SUPPORT_MAP_COUNT = 3;

constexpr SensorId obdSensorAt(std::size_t index) {
  return static_cast<SensorId>(index < SENSOR_COUNT ? index : SENSOR_COUNT - 1);
}

constexpr const SensorPidSpec& obdSpec(SensorId id) { return OBD_SENSOR_PIDS[sensorIndex(id)]; }

// Base 0x00/0x20/0x40 que declara o suporte de `pid`.
constexpr uint8_t obdSupportMapBase(uint8_t pid) {
  return pid <= 0x20 ? 0x00 : (pid <= 0x40 ? 0x20 : 0x40);
}

// Bit de `pid` no mapa de 32 bits da base correspondente (0x00/0x20/0x40 são os próprios mapas).
constexpr bool obdMapDeclaresPid(uint32_t bits, uint8_t pid) {
  return pid > 0x00 && pid != 0x20 && pid != 0x40 &&
         ((bits >> static_cast<unsigned>(obdSupportMapBase(pid) + 32u - pid)) & 0x1u) != 0u;
}

// Resultado de uma transação: Ok, ausência de dados, timeout, erro ou perda do transporte.
enum class ObdOutcome { Ok, NoData, Timeout, Error, Disconnected };

struct ObdReply {
  ObdOutcome outcome = ObdOutcome::Error;
  double value = 0;
  uint32_t bits = 0;  // preenchido em Ok quando `hasBits` (mapa de suporte)
  bool hasBits = false;
};

constexpr ObdReply obdOk(double value) { return ObdReply{ObdOutcome::Ok, value, 0, false}; }
constexpr ObdReply obdMap(uint32_t bits) { return ObdReply{ObdOutcome::Ok, 0, bits, true}; }
constexpr ObdReply obdNoData() { return ObdReply{ObdOutcome::NoData, 0, 0, false}; }
constexpr ObdReply obdTimeout() { return ObdReply{ObdOutcome::Timeout, 0, 0, false}; }
constexpr ObdReply obdError() { return ObdReply{ObdOutcome::Error, 0, 0, false}; }
constexpr ObdReply obdDisconnected() { return ObdReply{ObdOutcome::Disconnected, 0, 0, false}; }

// Uma consulta pendente por vez (SPEC-0001 §Polling). A sessão decide; o transporte executa.
class ObdTransport {
 public:
  virtual ~ObdTransport() = default;
  virtual bool isConnected() = 0;
  virtual bool connect(uint32_t timeoutMs) = 0;
  virtual bool adapterInit(uint32_t timeoutMs) = 0;
  virtual ObdReply readPid(uint8_t pid, uint32_t timeoutMs) = 0;
  virtual ObdReply readSupportMap(uint8_t mapBase, uint32_t timeoutMs) = 0;
  virtual ObdReply readAdapterVoltage(uint32_t timeoutMs) = 0;
  virtual const char* protocol() const { return ""; }
};

enum class ObdLogLevel { Quiet, Transition, Verbose };

class ObdLogSink {
 public:
  virtual ~ObdLogSink() = default;
  virtual void log(ObdLogLevel level, const char* line) = 0;
};

using ObdClock = uint64_t (*)(void* context);

struct ObdConfig {
  TimingConfig timing = defaultTimingConfig();
  VoltageSource voltageSource = ONIX_DEFAULT_VOLTAGE_SOURCE;
  uint32_t btConnectTimeoutMs = ONIX_BT_CONNECT_TIMEOUT_MS;
  uint32_t adapterInitTimeoutMs = ONIX_BT_INIT_TIMEOUT_MS;
  ObdLogLevel logLevel = ObdLogLevel::Transition;
};

// Dependências da sessão. O core não depende de API, WebServer nem Arduino: publica estado pelo
// publicador injetado (SnapshotStore em produção, dublê nos testes).
struct ObdRuntime {
  ObdTransport* transport = nullptr;
  ObdLogSink* log = nullptr;
  ObdClock clock = nullptr;
  void* clockContext = nullptr;
  ObdConfig config{};
};

// Publicação do snapshot: duração curta, sem I/O (ADR-0004).
class SnapshotPublisher {
 public:
  virtual ~SnapshotPublisher() = default;
  virtual void publish(const Snapshot& snapshot) = 0;
};

enum class ObdPhase { Disconnected, Initializing, ReadingSupport, Polling };
enum class ObdSupport : uint8_t { Unknown, Supported, Unsupported };

class ObdSession {
 public:
  ObdSession(const ObdRuntime& runtime, SnapshotPublisher& publisher);

  // Avança a máquina em no máximo UMA operação bloqueante (conexão, inicialização ou consulta) e
  // publica o snapshot. Retorna a espera cooperativa sugerida em milissegundos.
  uint32_t step();

  const Snapshot& snapshot() const { return snapshot_; }
  ObdPhase phase() const { return phase_; }
  ObdSupport support(SensorId id) const { return support_[sensorIndex(id)]; }
  int lastTransactionSensorIndex() const { return lastTransactionSensorIndex_; }
  uint64_t transactionsStarted() const { return transactionsStarted_; }
  uint32_t recoveryWaitMs() const { return recoveryWaitMs_; }   // espera aplicada na última tentativa
  uint64_t nextAttemptMs() const { return nextAttemptMs_; }     // instante liberado para agir
  uint64_t nextDueMs(SensorId id) const { return nextDueMs_[sensorIndex(id)]; }

 private:
  uint64_t nowMs() const;
  ObdTransport& transport() const { return *runtime_.transport; }
  const TimingConfig& timing() const { return runtime_.config.timing; }

  void transition(ObdPhase phase, const char* line);
  void logTransition(const char* line);  // no máximo uma vez por janela, por linha
  void logProtocol();
  void publish();

  void invalidateSession(uint64_t now, const char* reason, uint32_t waitMs = 0);
  bool beginSession(uint64_t now);
  void adapterReady(uint64_t now);
  void armAction(uint64_t now, uint32_t waitMs);
  uint32_t nextRecoveryWait() const;

  bool needsBase(uint8_t base) const;
  void markSupport(uint8_t base, uint32_t bits);
  void finishSupport(uint64_t now, bool complete);
  int pickDueSensor(uint64_t now) const;
  uint32_t idleWait(uint64_t now) const;
  uint32_t minIntervalMs() const;
  void refreshEcuHealth(uint64_t now);
  void recordEcuOk(uint64_t now);

  ObdRuntime runtime_{};
  SnapshotPublisher* publisher_ = nullptr;
  Snapshot snapshot_{};
  ObdPhase phase_ = ObdPhase::Disconnected;
  std::array<ObdSupport, SENSOR_COUNT> support_{};
  std::array<uint64_t, SENSOR_COUNT> nextDueMs_{};

  uint64_t nextAttemptMs_ = 0;
  uint32_t recoveryWaitMs_ = 0;
  uint64_t lastEcuOkMs_ = 0;
  uint32_t lastLogMs_ = 0;
  const char* lastLogLine_ = nullptr;
  bool logWindowOpen_ = false;
  uint8_t mapBaseIndex_ = 0;
  uint32_t mapBits_[OBD_SUPPORT_MAP_COUNT] = {0, 0, 0};
  bool mapOk_[OBD_SUPPORT_MAP_COUNT] = {false, false, false};
  uint8_t mapAttempts_[OBD_SUPPORT_MAP_COUNT] = {0, 0, 0};
  int lastTransactionSensorIndex_ = -1;
  uint8_t transactionsStarted_ = 0;  // contador baixo: sessões de teste não chegam a 255
  uint64_t transactionsTotal_ = 0;
};

// Loop de produção do ESP32 (BluetoothSerial + ELMduino, uma instância, uma task — ADR-0003/0004).
// Não é chamado nos testes nativos: a máquina testável é ObdSession. main.cpp cria a task.
void runObdTask(SnapshotStore& store);

// Pausa cooperativa da task OBD (ESP32): com `true`, cancela a busca Bluetooth em andamento e a
// task para de chamar a sessão no próximo passo, liberando o rádio para o Wi-Fi (OTA, ADR-0009).
// Diferente de vTaskSuspend, a task nunca para segurando um mutex. Chamável de outra task.
void setObdPaused(bool paused);

}  // namespace onix
