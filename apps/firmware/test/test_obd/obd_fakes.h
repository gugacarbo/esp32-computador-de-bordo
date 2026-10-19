// obd_fakes.h — dublês e harness dos testes nativos de obd.h (SPEC-0001 DoD: transporte simulado
// e relógio controlado, sem espera real e sem hardware).

#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "obd.h"
#include "snapshot.h"

namespace onix::test {

// Relógio controlado: avança só quando o teste (ou a latência simulada) pede.
class FakeClock {
 public:
  explicit FakeClock(uint64_t nowMs = 1000) : nowMs_(nowMs) {}

  ObdClock provider() { return &FakeClock::read; }
  void* context() { return this; }

  void advance(uint32_t ms) { nowMs_ += ms; }
  void set(uint64_t ms) { nowMs_ = ms; }
  uint64_t now() const { return nowMs_; }

 private:
  static uint64_t read(void* context) { return static_cast<FakeClock*>(context)->nowMs_; }
  uint64_t nowMs_;
};

struct FakeCall {
  enum class Kind { Connect, AdapterInit, Pid, Map, AdapterVoltage };
  Kind kind = Kind::Connect;
  uint8_t arg = 0;  // PID ou base do mapa
  uint64_t atMs = 0;
};

// Publicador dublê: definido antes do transporte para que FakeTransport::perform
// possa ler observer->publishes com tipo completo (evita incomplete type).
class RecordingPublisher : public SnapshotPublisher {
 public:
  void publish(const Snapshot& snapshot) override {
    ++publishes;
    last = snapshot;
  }

  uint32_t publishes = 0;
  Snapshot last{};
};

// Transporte fake: roteiro de respostas fixas, latência simulada, estado de conexão e contagem
// de chamadas. Uma chamada "bloqueante" consome tempo no relógio controlado — sem espera real.
class FakeTransport : public ObdTransport {
 public:
  uint32_t connectLatencyMs = 1;
  uint32_t initLatencyMs = 1;
  uint32_t pidLatencyMs = 1;
  uint32_t mapLatencyMs = 1;
  uint32_t adapterVoltageLatencyMs = 1;

  bool connected = false;
  bool connectResult = true;
  bool adapterInitResult = true;
  std::string protocolName = "6";

  uint32_t connectCalls = 0;
  uint32_t adapterInitCalls = 0;
  uint32_t readsWhileDisconnected = 0;
  uint32_t publishesDuringCall = 0;

  FakeClock* clock = nullptr;
  RecordingPublisher* observer = nullptr;

  std::vector<FakeCall> calls;
  std::vector<ObdReply> script;
  ObdReply def = obdError();

  void onConnect(bool result, bool adapterInit) {
    connectResult = result;
    adapterInitResult = adapterInit;
  }

  void push(ObdReply reply) { script.push_back(reply); }
  void setDefault(ObdReply reply) { def = reply; }

  void onPid(uint8_t pid, ObdReply reply) { fixed_[key(FakeCall::Kind::Pid, pid)] = reply; }
  void onMap(uint8_t base, ObdReply reply) { fixed_[key(FakeCall::Kind::Map, base)] = reply; }
  void onAdapterVoltage(ObdReply reply) {
    fixed_[key(FakeCall::Kind::AdapterVoltage, 0)] = reply;
  }

  // Desconexão do scanner: leituras passam a devolver Disconnected e a conexão falha.
  void cutTransport() {
    connected = false;
    connectResult = false;
  }

  // Scanner recolocado: conexão e inicialização voltam a funcionar.
  void restoreTransport() {
    connected = true;
    connectResult = true;
    adapterInitResult = true;
  }

  bool isConnected() override { return connected; }

  bool connect(uint32_t) override {
    ++connectCalls;
    record(FakeCall::Kind::Connect, 0);
    spend(connectLatencyMs);
    if (connectResult) {
      connected = true;
    }
    return connectResult;
  }

  bool adapterInit(uint32_t) override {
    ++adapterInitCalls;
    record(FakeCall::Kind::AdapterInit, 0);
    spend(initLatencyMs);
    if (!connected) {
      return false;
    }
    return adapterInitResult;
  }

  ObdReply readPid(uint8_t pid, uint32_t) override {
    return perform(FakeCall::Kind::Pid, pid, pidLatencyMs);
  }

  ObdReply readSupportMap(uint8_t base, uint32_t) override {
    return perform(FakeCall::Kind::Map, base, mapLatencyMs);
  }

  ObdReply readAdapterVoltage(uint32_t) override {
    return perform(FakeCall::Kind::AdapterVoltage, 0, adapterVoltageLatencyMs);
  }

  const char* protocol() const override { return protocolName.c_str(); }

  uint32_t count(FakeCall::Kind kind) const {
    uint32_t total = 0;
    for (const FakeCall& call : calls) {
      if (call.kind == kind) {
        ++total;
      }
    }
    return total;
  }

  uint32_t countPid(uint8_t pid) const {
    uint32_t total = 0;
    for (const FakeCall& call : calls) {
      if (call.kind == FakeCall::Kind::Pid && call.arg == pid) {
        ++total;
      }
    }
    return total;
  }

  uint32_t countMap(uint8_t base) const {
    uint32_t total = 0;
    for (const FakeCall& call : calls) {
      if (call.kind == FakeCall::Kind::Map && call.arg == base) {
        ++total;
      }
    }
    return total;
  }

  // Lista permitida da SPEC-0001 §Limite de comandos: comandos AT da conexão/inicialização
  // (connect/adapterInit) e Serviço 01 para os PIDs da Spec e mapas 00/20/40.
  bool sentAnyDisallowedCommand() const {
    for (const FakeCall& call : calls) {
      if (call.kind == FakeCall::Kind::Pid) {
        bool allowed = false;
        for (const SensorPidSpec& spec : OBD_SENSOR_PIDS) {
          allowed = allowed || spec.pid == call.arg;
        }
        if (!allowed) {
          return true;
        }
      }
      if (call.kind == FakeCall::Kind::Map) {
        bool allowed = false;
        for (uint8_t base : OBD_SUPPORT_MAP_BASES) {
          allowed = allowed || base == call.arg;
        }
        if (!allowed) {
          return true;
        }
      }
    }
    return false;
  }

 private:
  static uint32_t key(FakeCall::Kind kind, uint8_t arg) {
    return static_cast<uint32_t>(kind) * 256u + arg;
  }

  void record(FakeCall::Kind kind, uint8_t arg) {
    FakeCall call;
    call.kind = kind;
    call.arg = arg;
    call.atMs = clock != nullptr ? clock->now() : 0;
    calls.push_back(call);
  }

  void spend(uint32_t ms) {
    if (clock != nullptr) {
      clock->advance(ms);
    }
  }

  ObdReply perform(FakeCall::Kind kind, uint8_t arg, uint32_t latencyMs) {
    record(kind, arg);
    const uint32_t publishesBefore = observer != nullptr ? observer->publishes : 0;
    spend(latencyMs);
    ObdReply reply;
    if (!connected) {
      ++readsWhileDisconnected;
      reply = obdDisconnected();
    } else {
      auto it = fixed_.find(key(kind, arg));
      if (it != fixed_.end()) {
        reply = it->second;
      } else if (!script.empty()) {
        reply = script.front();
        script.erase(script.begin());
      } else {
        reply = def;
      }
    }
    if (observer != nullptr && observer->publishes != publishesBefore) {
      ++publishesDuringCall;  // violaria ADR-0004: publicar dentro de chamada bloqueante
    }
    return reply;
  }

  std::map<uint32_t, ObdReply> fixed_;
};

// Mapa de suporte sintético para uma base: bit de cada PID da base, mais o bit "há outro mapa".
inline uint32_t buildMap(uint8_t base, const std::vector<uint8_t>& pids) {
  uint32_t bits = 0x80000000u;
  for (uint8_t pid : pids) {
    if (pid > base && pid <= base + 32 && pid != 0x20 && pid != 0x40) {
      bits |= 1u << static_cast<unsigned>(base + 32 - pid);
    }
  }
  return bits;
}

class RecordingLog : public ObdLogSink {
 public:
  void log(ObdLogLevel level, const char* line) override {
    lines.push_back(line != nullptr ? line : "");
    levels.push_back(level);
  }

  uint32_t count(const std::string& needle) const {
    uint32_t total = 0;
    for (const std::string& line : lines) {
      if (line.find(needle) != std::string::npos) {
        ++total;
      }
    }
    return total;
  }

  uint32_t countLevel(ObdLogLevel level) const {
    uint32_t total = 0;
    for (ObdLogLevel l : levels) {
      if (l == level) {
        ++total;
      }
    }
    return total;
  }

  std::vector<std::string> lines;
  std::vector<ObdLogLevel> levels;
};

inline bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// Harness: relógio controlado + transporte fake + publicador + log + sessão.
class ObdHarness {
 public:
  explicit ObdHarness(const TimingConfig& timing = defaultTimingConfig(),
                      VoltageSource voltage = ONIX_DEFAULT_VOLTAGE_SOURCE,
                      ObdLogLevel logLevel = ObdLogLevel::Transition,
                      uint8_t maxConnectAttempts = ONIX_BT_MAX_CONNECT_ATTEMPTS)
      : runtime(make(std::move(timing), std::move(voltage), logLevel, maxConnectAttempts)),
        session(runtime, publisher) {
    transport.clock = &clock;
    transport.observer = &publisher;
  }

  uint32_t step() { return session.step(); }

  void advance(uint32_t ms) { clock.advance(ms); }

  void advanceAndStep(uint32_t ms) {
    clock.advance(ms);
    session.step();
  }

  // Roda a máquina até o relógio avançar `ms`, respeitando a espera sugerida por step().
  void runMs(uint32_t ms, uint32_t maxSteps = 60000) {
    const uint64_t target = clock.now() + ms;
    for (uint32_t i = 0; i < maxSteps && clock.now() < target; ++i) {
      const uint32_t wait = session.step();
      uint32_t stepMs = wait == 0 ? 1 : wait;
      const uint64_t remaining = target > clock.now() ? target - clock.now() : 0;
      if (stepMs > remaining) {
        stepMs = static_cast<uint32_t>(remaining);
      }
      if (stepMs == 0) {
        stepMs = 1;
      }
      clock.advance(stepMs);
    }
  }

  bool stepUntil(ObdPhase phase, uint32_t maxSteps = 500) {
    for (uint32_t i = 0; i < maxSteps; ++i) {
      if (session.phase() == phase) {
        return true;
      }
      session.step();
    }
    return session.phase() == phase;
  }

  // Todos os PIDs suportados, com respostas válidas, e sessão em Polling.
  bool bringUpAll(uint32_t maxSteps = 500) {
    supportPids(allPids());
    valuesOk();
    return stepUntil(ObdPhase::Polling, maxSteps);
  }

  static const std::vector<uint8_t>& allPids() {
    static const std::vector<uint8_t> pids = {0x05, 0x0B, 0x0C, 0x0D, 0x11, 0x42};
    return pids;
  }

  void supportPids(const std::vector<uint8_t>& pids) {
    std::vector<uint8_t> group0;
    std::vector<uint8_t> group20;
    std::vector<uint8_t> group40;
    for (uint8_t pid : pids) {
      switch (obdSupportMapBase(pid)) {
        case 0x00:
          group0.push_back(pid);
          break;
        case 0x20:
          group20.push_back(pid);
          break;
        default:
          group40.push_back(pid);
          break;
      }
    }
    transport.onMap(0x00, obdMap(buildMap(0x00, group0)));
    transport.onMap(0x20, obdMap(buildMap(0x20, group20)));
    transport.onMap(0x40, obdMap(buildMap(0x40, group40)));
  }

  void valuesOk(double rpm = 1000, double speedKph = 0, double coolantC = 90,
                double throttlePercent = 10, double mapKpa = 100, double volts = 12.6) {
    transport.onPid(0x0C, obdOk(rpm));
    transport.onPid(0x0D, obdOk(speedKph));
    transport.onPid(0x05, obdOk(coolantC));
    transport.onPid(0x11, obdOk(throttlePercent));
    transport.onPid(0x0B, obdOk(mapKpa));
    transport.onAdapterVoltage(obdOk(volts));
    transport.onPid(0x42, obdOk(volts));
  }

  const SensorReading& reading(SensorId id) const {
    return session.snapshot().sensors[sensorIndex(id)];
  }

  uint32_t publishesOf(const RecordingPublisher&) const { return publisher.publishes; }

  FakeClock clock;
  FakeTransport transport;
  RecordingPublisher publisher;
  RecordingLog log;
  ObdRuntime runtime;
  ObdSession session;

 private:
  ObdRuntime make(TimingConfig timing, VoltageSource voltage, ObdLogLevel logLevel,
                  uint8_t maxConnectAttempts) {
    ObdRuntime runtime;
    runtime.transport = &transport;
    runtime.log = &log;
    runtime.clock = clock.provider();
    runtime.clockContext = clock.context();
    runtime.config.timing = timing;
    runtime.config.voltageSource = voltage;
    runtime.config.logLevel = logLevel;
    runtime.config.btMaxConnectAttempts = maxConnectAttempts;
    return runtime;
  }
};

void runSessionSuite();
void runRecoverySuite();
void runSchedulerSuite();
void runConfigSuite();

}  // namespace onix::test
