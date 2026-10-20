// obd_transport_esp32.cpp — transporte OBD real do ESP32: BluetoothSerial (SPP master) + ELMduino
// (ADR-0003, ADR-0004, SPEC-0001). Dono único do BluetoothSerial e da instância ELM327; a task
// OBD (runObdTask) é a única que chama estes métodos (ADR-0004).
//
// Política de comandos: SOMENTE Serviço 01 (leitura de PIDs, mapas de suporte) e `AT RV`
// (tensão do adaptador). Nunca Serviço 04 / resetDTC nem qualquer escrita na ECU. Os comandos
// `AT` de inicialização do ELMduino (begin) e `ATDPN` são configuração do adaptador, não da ECU.
//
// Compilação: o corpo fica sob a mesma guarda de main.cpp. O env `native` compila src/ para os
// testes Unity, então o build nativo recebe apenas um placeholder.

#if defined(ARDUINO) || defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <ELMduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_gap_bt_api.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <cstring>

#include "bt_config.h"
#include "api.h"  // monotonicUptimeMs(): mesma base de tempo da API (updatedAtMs, ageMs)
#include "log.h"
#include "obd.h"
#include "obd_elm.h"

// Os códigos ONIX_ELM_* precisam ser exatamente os do ELMduino 3.4.1 instalado.
static_assert(onix::ONIX_ELM_SUCCESS == ELM_SUCCESS, "ELM_SUCCESS difere do ELMduino");
static_assert(onix::ONIX_ELM_NO_RESPONSE == ELM_NO_RESPONSE, "ELM_NO_RESPONSE difere do ELMduino");
static_assert(onix::ONIX_ELM_BUFFER_OVERFLOW == ELM_BUFFER_OVERFLOW,
              "ELM_BUFFER_OVERFLOW difere do ELMduino");
static_assert(onix::ONIX_ELM_GARBAGE == ELM_GARBAGE, "ELM_GARBAGE difere do ELMduino");
static_assert(onix::ONIX_ELM_UNABLE_TO_CONNECT == ELM_UNABLE_TO_CONNECT,
              "ELM_UNABLE_TO_CONNECT difere do ELMduino");
static_assert(onix::ONIX_ELM_NO_DATA == ELM_NO_DATA, "ELM_NO_DATA difere do ELMduino");
static_assert(onix::ONIX_ELM_STOPPED == ELM_STOPPED, "ELM_STOPPED difere do ELMduino");
static_assert(onix::ONIX_ELM_TIMEOUT == ELM_TIMEOUT, "ELM_TIMEOUT difere do ELMduino");
static_assert(onix::ONIX_ELM_GETTING_MSG == ELM_GETTING_MSG, "ELM_GETTING_MSG difere do ELMduino");
static_assert(onix::ONIX_ELM_MSG_RXD == ELM_MSG_RXD, "ELM_MSG_RXD difere do ELMduino");
static_assert(onix::ONIX_ELM_GENERAL_ERROR == ELM_GENERAL_ERROR,
              "ELM_GENERAL_ERROR difere do ELMduino");

namespace onix {
namespace {

// Margem sobre o prazo da consulta. O próprio ELMduino expira `timeout_ms` após o último envio;
// esta margem é só a rede de segurança caso o adaptador continue enviando bytes sem o '>'.
constexpr uint64_t kElmDeadlineMarginMs = 200;
// Período do log de folga da pilha da task.
constexpr uint64_t kStackLogPeriodMs = 60000;

// Configuração do scanner em uso (ADR-0014): main.cpp grava (boot e /api/obd/config), a task OBD lê
// a cada conexão. A geração avisa o transporte para esquecer o MAC achado e reaplicar o PIN.
std::mutex g_scannerConfigMutex;
BtScannerConfig g_scannerConfig = defaultBtScannerConfig();
std::atomic<uint32_t> g_scannerConfigGeneration{0};

BtScannerConfig currentScannerConfig(uint32_t& generation) {
  std::lock_guard<std::mutex> lock(g_scannerConfigMutex);
  generation = g_scannerConfigGeneration.load();
  return g_scannerConfig;
}

// Resultado da última busca pedida pelo painel (/api/obd/scan).
std::mutex g_scanMutex;
BtScanResult g_scanResult;

void setScanState(BtScanState state) {
  std::lock_guard<std::mutex> lock(g_scanMutex);
  g_scanResult.state = state;
}

// Encaminha as linhas da sessão OBD ao log do firmware (ADR-0011) até o limite configurado
// (Quiet < Transition < Verbose). O prefixo "[obd] " ou "[bt] " das linhas vira a tag.
class SerialLogSink : public ObdLogSink {
 public:
  explicit SerialLogSink(ObdLogLevel threshold) : threshold_(threshold) {}

  void log(ObdLogLevel level, const char* line) override {
    if (line == nullptr || static_cast<int>(level) > static_cast<int>(threshold_)) {
      return;
    }
    char tag[ONIX_LOG_TAG_MAX] = "obd";
    const char* message = line;
    const char* close = line[0] == '[' ? std::strchr(line, ']') : nullptr;
    if (close != nullptr && close[1] == ' ' &&
        static_cast<std::size_t>(close - line - 1) < sizeof(tag)) {
      const std::size_t length = static_cast<std::size_t>(close - line - 1);
      std::memcpy(tag, line + 1, length);
      tag[length] = '\0';
      message = close + 2;
    }
    const LogLevel mapped = level == ObdLogLevel::Quiet        ? LogLevel::Warn
                            : level == ObdLogLevel::Transition ? LogLevel::Info
                                                               : LogLevel::Debug;
    logf(mapped, tag, "%s", message);
  }

 private:
  ObdLogLevel threshold_;
};

// Adaptador para o dono do snapshot: publicação curta, sem I/O (ADR-0004).
class StorePublisher : public SnapshotPublisher {
 public:
  explicit StorePublisher(SnapshotStore& store) : store_(store) {}

  void publish(const Snapshot& snapshot) override { store_.publish(snapshot); }

 private:
  SnapshotStore& store_;
};

uint64_t obdClock(void* /*context*/) { return monotonicUptimeMs(); }

class Elm327BtTransport : public ObdTransport {
 public:
  bool isConnected() override {
    // connected(0) consulta o evento SPP sem esperar; hasClient() só existe no lado servidor.
    return SerialBT.connected(0);
  }

  bool connect(uint32_t timeoutMs) override {
    // BluetoothSerial::connect() não aceita timeout: usa o prazo interno da biblioteca
    // (READY_TIMEOUT ~10 s para conectar pelo MAC). timeoutMs fica como teto documentado.
    // connect(nome) não é usado: ele refaz uma busca de ~20 s a cada tentativa e, sem scanner por
    // perto, o rádio passava a maior parte do tempo buscando e o SoftAP ficava fora do ar.
    (void)timeoutMs;
    uint32_t generation = 0;
    const BtScannerConfig config = currentScannerConfig(generation);
    if (generation != appliedGeneration_) {
      // Configuração nova (painel): o MAC achado era do scanner antigo e o PIN pode ter mudado.
      appliedGeneration_ = generation;
      haveScannerMac_ = false;
      macFailures_ = 0;
      pinApplied_ = false;
    }
    if (!ensureStarted()) {
      return false;
    }
    if (!pinApplied_ && !config.pin.empty()) {
      SerialBT.setPin(config.pin.c_str());
    }
    pinApplied_ = true;
    if (SerialBT.connected(0)) {
      SerialBT.disconnect();
    }
    if (!config.mac.empty()) {
      std::array<uint8_t, 6> mac{};
      if (!parseBtMac(config.mac, mac)) {
        ONIX_LOGE("bt", "MAC do scanner invalido: %s", config.mac.c_str());
        return false;
      }
      return SerialBT.connect(mac.data());
    }
    if (!haveScannerMac_ || macFailures_ >= ONIX_BT_MAC_RETRIES) {
      if (!discoverScanner(config.name)) {
        return false;
      }
    }
    if (SerialBT.connect(scannerMac_)) {
      macFailures_ = 0;
      return true;
    }
    ++macFailures_;
    return false;
  }

  bool adapterInit(uint32_t timeoutMs) override {
    // begin() bloqueia pelo tempo do próprio ELMduino (busca automática de protocolo, até 30 s);
    // timeoutMs é o teto documentado e não interrompe a biblioteca.
    (void)timeoutMs;
    // begin() aloca o buffer de payload a cada chamada: libera o anterior para não vazar memória
    // a cada reconexão.
    std::free(elm_.payload);
    elm_.payload = nullptr;
    if (!elm_.begin(SerialBT, false, ONIX_OBD_AT_TIMEOUT_MS, ONIX_OBD_PROTOCOL,
                    ONIX_OBD_PAYLOAD_LEN)) {
      std::strcpy(protocol_, "?");
      return false;
    }
    // Protocolo efetivo, só para log. ATDPN é leitura de configuração; falha não derruba a sessão.
    protocol_[0] = '\0';
    if (elm_.sendCommand_Blocking("ATDPN") == ELM_SUCCESS && elm_.payload != nullptr) {
      std::strncpy(protocol_, elm_.payload, sizeof(protocol_) - 1);
      protocol_[sizeof(protocol_) - 1] = '\0';
    }
    if (protocol_[0] == '\0') {
      std::strcpy(protocol_, "?");
    }
    return true;
  }

  ObdReply readPid(uint8_t pid, uint32_t timeoutMs) override {
    if (!isConnected()) {
      return obdDisconnected();
    }
    for (const SensorPidSpec& spec : OBD_SENSOR_PIDS) {
      if (spec.pid == pid) {
        return transaction(
            timeoutMs,
            [&]() {
              return elm_.processPID(0x01, pid, 1, spec.expectedBytes, spec.scale, spec.bias);
            },
            [](double value) { return obdOk(value); });
      }
    }
    return obdError();  // PID fora da tabela de sensores: não é consulta permitida
  }

  ObdReply readSupportMap(uint8_t mapBase, uint32_t timeoutMs) override {
    if (!isConnected()) {
      return obdDisconnected();
    }
    // Serviço 01, PID de suporte (0x00/0x20/0x40): 4 bytes, escala 1, bias 0.
    return transaction(
        timeoutMs, [&]() { return elm_.processPID(0x01, mapBase, 1, 4, 1.0, 0.0); },
        [](double value) { return obdMap(static_cast<uint32_t>(value)); });
  }

  ObdReply readAdapterVoltage(uint32_t timeoutMs) override {
    if (!isConnected()) {
      return obdDisconnected();
    }
    // AT RV: tensão medida pelo adaptador, sem consulta à ECU.
    return transaction(
        timeoutMs, [&]() { return elm_.batteryVoltage(); },
        [](double value) { return obdOk(value); });
  }

  const char* protocol() const override { return protocol_; }

 private:
 public:
  // Busca pedida pelo painel (/api/obd/scan): lista nome/MAC/RSSI dos dispositivos próximos.
  // Só a task OBD chama; com o scanner conectado a pilha recusa a busca (estado busy).
  void scanDevices() {
    if (isConnected()) {
      setScanState(BtScanState::Busy);
      return;
    }
    if (!ensureStarted()) {
      setScanState(BtScanState::Failed);
      return;
    }
    setScanState(BtScanState::Scanning);
    BTScanResults* results = SerialBT.discover(ONIX_BT_DISCOVERY_MS);
    std::vector<BtDevice> devices;
    if (results != nullptr) {
      for (int i = 0; i < results->getCount(); ++i) {
        BTAdvertisedDevice* found = results->getDevice(i);
        if (found == nullptr) continue;
        BtDevice device;
        device.name = found->haveName() ? found->getName() : "";
        std::array<uint8_t, 6> mac{};
        std::memcpy(mac.data(), *found->getAddress().getNative(), mac.size());
        device.mac = formatBtMac(mac);
        device.hasRssi = found->haveRSSI();
        device.rssi = device.hasRssi ? found->getRSSI() : 0;
        devices.push_back(device);
      }
    }
    std::lock_guard<std::mutex> lock(g_scanMutex);
    g_scanResult.devices = std::move(devices);
    g_scanResult.state = results != nullptr ? BtScanState::Done : BtScanState::Failed;
    ONIX_LOGI("bt", "busca do painel: %u dispositivos",
              static_cast<unsigned>(g_scanResult.devices.size()));
  }

 private:
  bool ensureStarted() {
    if (!btStarted_) {
      if (!SerialBT.begin(String(ONIX_BT_LOCAL_NAME), true)) {  // modo master
        return false;
      }
      btStarted_ = true;
    }
    return true;
  }

  // Busca curta (ONIX_BT_DISCOVERY_MS) pelo nome configurado e guarda o MAC do scanner.
  // Só funciona antes de qualquer connect(nome) — por isso o transporte nunca o chama.
  bool discoverScanner(const std::string& name) {
    BTScanResults* results = SerialBT.discover(ONIX_BT_DISCOVERY_MS);
    if (results == nullptr) {
      ONIX_LOGW("bt", "busca indisponivel");
      return false;
    }
    for (int i = 0; i < results->getCount(); ++i) {
      BTAdvertisedDevice* device = results->getDevice(i);
      if (device != nullptr && device->haveName() && device->getName() == name) {
        std::memcpy(scannerMac_, *device->getAddress().getNative(), sizeof(scannerMac_));
        haveScannerMac_ = true;
        macFailures_ = 0;
        ONIX_LOGI("bt", "scanner '%s' encontrado: %s", name.c_str(),
                  device->getAddress().toString().c_str());
        return true;
      }
    }
    ONIX_LOGI("bt", "scanner '%s' nao encontrado (%d dispositivos na busca)", name.c_str(),
              results->getCount());
    return false;
  }

  // Executa uma consulta não bloqueante do ELMduino até o fim ou até o prazo. `step` chama
  // processPID/batteryVoltage uma vez; `make` converte o valor em ObdReply quando Ok.
  template <typename Step, typename Make>
  ObdReply transaction(uint32_t timeoutMs, Step step, Make make) {
    const uint64_t deadline = monotonicUptimeMs() + timeoutMs + kElmDeadlineMarginMs;
    elm_.timeout_ms = static_cast<uint16_t>(timeoutMs);  // timeout por consulta (público no ELMduino)

    // Resíduo de consulta anterior interrompida (prazo ou perda de link). O estado interno do
    // ELMduino (nb_query_state, privado) só volta a SEND_COMMAND quando a resposta pendente chega
    // ou expira. Drena antes de enviar a nova; senão a nova leria a resposta velha.
    if (pending_) {
      elm_.nb_rx_state = ELM_GETTING_MSG;
      while (elm_.nb_rx_state == ELM_GETTING_MSG) {
        if (!isConnected()) {
          return obdDisconnected();  // pending_ segue true: drena na próxima transação
        }
        if (monotonicUptimeMs() >= deadline) {
          return obdTimeout();
        }
        step();  // em WAITING_RESP só lê o adaptador; não envia nada
        if (elm_.nb_rx_state == ELM_GETTING_MSG) {
          vTaskDelay(1);
        }
      }
      pending_ = false;
    }

    // A primeira chamada só envia o comando; as seguintes leem a resposta. nb_rx_state é
    // preparado aqui porque pode conter o resultado da consulta anterior.
    elm_.nb_rx_state = ELM_GETTING_MSG;
    for (;;) {
      const double value = step();
      if (elm_.nb_rx_state != ELM_GETTING_MSG) {
        pending_ = false;  // ELMduino já voltou a SEND_COMMAND
        switch (mapElmStatus(elm_.nb_rx_state)) {
          case ObdOutcome::Ok:
            return make(value);
          case ObdOutcome::NoData:
            return obdNoData();
          case ObdOutcome::Timeout:
            return obdTimeout();
          case ObdOutcome::Disconnected:
            return obdDisconnected();
          case ObdOutcome::Error:
            break;
        }
        return obdError();
      }
      if (!isConnected()) {
        pending_ = true;  // consulta ainda em voo no ELMduino
        return obdDisconnected();
      }
      if (monotonicUptimeMs() >= deadline) {
        pending_ = true;
        return obdTimeout();
      }
      vTaskDelay(1);
    }
  }

  BluetoothSerial SerialBT;  // único dono do Bluetooth Classic (SPP)
  ELM327 elm_;               // única instância do ELMduino; usada só pela task OBD
  bool btStarted_ = false;
  bool pending_ = false;     // consulta interrompida que ainda precisa ser drenada
  uint8_t scannerMac_[6] = {0, 0, 0, 0, 0, 0};  // MAC achado pela busca (RAM; refeito no boot)
  bool haveScannerMac_ = false;
  uint8_t macFailures_ = 0;  // falhas seguidas ao conectar pelo MAC guardado
  uint32_t appliedGeneration_ = 0;  // geração de g_scannerConfig já aplicada
  bool pinApplied_ = false;
  char protocol_[24] = "";
};

}  // namespace

namespace {
std::atomic<bool> g_obdPaused{false};
std::atomic<bool> g_obdReconnectRequested{false};
std::atomic<bool> g_obdScanRequested{false};
std::atomic<uint64_t> g_lastHttpStaticMs{0};

// Painel carregando agora há pouco: adiar conexão/busca BT, que derruba o Wi-Fi enquanto dura.
bool httpBusyRecently() {
  const uint64_t last = g_lastHttpStaticMs.load();
  if (last == 0) return false;
  return monotonicUptimeMs() - last < ONIX_BT_HTTP_QUIET_MS;
}
}  // namespace

void setObdScannerConfig(const BtScannerConfig& config, bool reconnect) {
  {
    std::lock_guard<std::mutex> lock(g_scannerConfigMutex);
    g_scannerConfig = config;
    g_scannerConfigGeneration.fetch_add(1);
  }
  if (reconnect) {
    requestObdReconnect();  // aplica já: reconecta com a nova configuração e retoma a busca parada
  }
}

void noteHttpStaticActivity() { g_lastHttpStaticMs.store(monotonicUptimeMs()); }

BtScannerConfig obdScannerConfig() {
  uint32_t generation = 0;
  return currentScannerConfig(generation);
}

bool requestObdScan() {
  std::lock_guard<std::mutex> lock(g_scanMutex);
  if (g_scanResult.state == BtScanState::Pending || g_scanResult.state == BtScanState::Scanning) {
    return false;
  }
  g_scanResult.state = BtScanState::Pending;
  g_scanResult.devices.clear();
  g_obdScanRequested.store(true);
  return true;
}

BtScanResult obdScanResult() {
  std::lock_guard<std::mutex> lock(g_scanMutex);
  return g_scanResult;
}

void setObdPaused(bool paused) {
  g_obdPaused.store(paused);
  if (paused) {
    // connect(nome) pode estar no meio de uma busca de ~20 s; cancelar a encerra já. Sem a pilha
    // BT ativa, a chamada só retorna erro.
    esp_bt_gap_cancel_discovery();
  }
}

void requestObdReconnect() {
  g_obdReconnectRequested.store(true);  // a task OBD consome o pedido no topo do loop
}

void runObdTask(SnapshotStore& store) {
  static Elm327BtTransport transport;
  static const ObdConfig config{};
  static SerialLogSink log(config.logLevel);
  static StorePublisher publisher(store);

  ObdRuntime runtime;
  runtime.transport = &transport;
  runtime.log = &log;
  runtime.clock = obdClock;  // mesma base monotônica da API (monotonicUptimeMs)
  runtime.config = config;
  ObdSession session(runtime, publisher);

  uint64_t nextStackLogMs = monotonicUptimeMs() + kStackLogPeriodMs;
  for (;;) {
    if (g_obdReconnectRequested.exchange(false)) {
      session.requestReconnect();  // só a task OBD toca na sessão
    }
    if (g_obdPaused.load()) {
      vTaskDelay(pdMS_TO_TICKS(100));  // OTA em andamento: rádio livre para o Wi-Fi
      continue;
    }
    if (g_obdScanRequested.exchange(false)) {
      transport.scanDevices();  // ~5 s de busca; só desconectado (senão busy)
      continue;
    }
    if (session.phase() == ObdPhase::Disconnected && httpBusyRecently()) {
      vTaskDelay(pdMS_TO_TICKS(500));  // conexão adiada até o painel terminar de carregar
      continue;
    }
    const uint32_t wait = session.step();  // no máximo uma operação bloqueante
    const TickType_t ticks = pdMS_TO_TICKS(wait);
    vTaskDelay(ticks > 0 ? ticks : 1);

    const uint64_t now = monotonicUptimeMs();
    if (now >= nextStackLogMs) {
      ONIX_LOGI("obd", "folga minima da pilha: %u bytes",
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
      nextStackLogMs = now + kStackLogPeriodMs;
    }
  }
}

}  // namespace onix

#else

// Build nativo (env `native`, test_build_src=yes compila src/): o transporte ESP32 depende de
// BluetoothSerial e ELMduino e não compila aqui. Os testes usam obd.h e obd_elm.h diretamente.
namespace onix {
constexpr int kObdTransportEsp32NativePlaceholder = 0;
}

#endif
