// main.cpp — integração do firmware ONIX no ESP32 (SPEC-0002, ADR-0004, ADR-0005, ADR-0009,
// ADR-0011, ADR-0012).
//
// PONTO ÚNICO de contato com Arduino/ESP32: SoftAP/STA, WebServer, ArduinoOTA, NVS, o sink Serial do
// log e a task FreeRTOS do OBD.
// A política HTTP (rotas, 404/405, cabeçalhos, serialização) vive em ApiRouter (api.cpp);
// aqui apenas traduzimos HttpRequest/HttpResponse em chamadas do WebServer.
//
// Portátil: o env `native` (test_build_src=yes) compila src/ para os testes Unity, então
// TODO o código Arduino fica sob guarda — o build nativo não vê WiFi/WebServer/FreeRTOS.

#if defined(ARDUINO) || defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <cstring>
#include <optional>

#include "esp_system.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"

#include "api.h"
#include "config.h"
#include "dashboard.h"
#include "log.h"
#include "obd.h"
#include "snapshot.h"
#include "static_asset_policy.h"
#include "wifi_config.h"
#include "wifi_policy.h"
#include "ws_broadcast.h"

namespace {

using onix::ApiRouter;
using onix::HttpRequest;
using onix::HttpResponse;
using onix::HttpMethod;
using onix::SnapshotStore;

// Dono do snapshot: a task OBD publica; o ApiRouter copia sob mutex de duração curta
// (ADR-0004). A ordem de declaração garante a inicialização antes de g_apiRouter.
SnapshotStore g_snapshotStore;
const onix::TimingConfig g_timing = onix::defaultTimingConfig();

// Rede (ADR-0012): WifiPolicy decide, applyWifiAction() executa com a API WiFi do Arduino.
onix::WifiPolicy g_wifiPolicy(onix::defaultWifiTimings());
onix::WifiCredentials g_wifiCredentials;
bool g_hasWifiCredentials = false;
bool g_wifiScanRunning = false;
bool g_restartPending = false;
uint32_t g_restartRequestedMs = 0;

constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kObdPrefsNamespace[] = "obd";

void scheduleRestart() {
  g_restartPending = true;
  g_restartRequestedMs = millis();
}

// Credenciais do hotspot na NVS para /api/wifi. Gravar ou apagar agenda o reinício, que só
// acontece depois de a resposta HTTP sair (maybeRestart no loop()).
class NvsWifiConfig : public onix::WifiConfigPort {
 public:
  onix::WifiStatus status() const override {
    onix::WifiStatus result;
    const onix::WifiMode mode = g_wifiPolicy.mode();
    result.mode = onix::wifiModeName(mode);
    result.configured = g_hasWifiCredentials;
    result.ssid = g_wifiCredentials.ssid;
    if (mode == onix::WifiMode::AccessPoint) {
      result.ip = WiFi.softAPIP().toString().c_str();
    } else if (WiFi.status() == WL_CONNECTED) {
      result.ip = WiFi.localIP().toString().c_str();
    }
    return result;
  }

  bool save(const onix::WifiCredentials& credentials) override {
    Preferences prefs;
    if (!prefs.begin(kWifiPrefsNamespace, false)) {
      return false;
    }
    const bool ssidOk =
        prefs.putString("ssid", credentials.ssid.c_str()) == credentials.ssid.size();
    // putString devolve 0 também para string vazia: rede aberta remove a chave.
    const bool passOk = credentials.password.empty()
                            ? (!prefs.isKey("pass") || prefs.remove("pass"))
                            : prefs.putString("pass", credentials.password.c_str()) ==
                                  credentials.password.size();
    prefs.end();
    if (!ssidOk || !passOk) {
      return false;
    }
    ONIX_LOGW("wifi", "hotspot '%s' cadastrado; reiniciando", credentials.ssid.c_str());
    scheduleRestart();
    return true;
  }

  bool clear() override {
    Preferences prefs;
    if (!prefs.begin(kWifiPrefsNamespace, false)) {
      return false;
    }
    const bool ok = prefs.clear();
    prefs.end();
    if (ok) {
      ONIX_LOGW("wifi", "hotspot esquecido; reiniciando");
      scheduleRestart();
    }
    return ok;
  }
};

NvsWifiConfig g_wifiConfig;

// Scanner Bluetooth na NVS para /api/obd/config (ADR-0014). Diferente do Wi-Fi, aplica sem
// reiniciar: setObdScannerConfig avisa a task OBD, que reconecta com a configuração nova.
class NvsBtConfig : public onix::BtConfigPort {
 public:
  void load() {
    onix::BtScannerConfig config = onix::defaultBtScannerConfig();
    Preferences prefs;
    if (prefs.begin(kObdPrefsNamespace, true)) {
      custom_ = prefs.isKey("custom");
      if (custom_) {
        config.name = prefs.getString("name", "").c_str();
        config.pin = prefs.getString("pin", "").c_str();
        config.mac = prefs.getString("mac", "").c_str();
      }
      prefs.end();
    }
    if (!onix::isValidBtScannerConfig(config)) {
      custom_ = false;
      config = onix::defaultBtScannerConfig();
    }
    onix::setObdScannerConfig(config);
    if (custom_) {
      ONIX_LOGI("bt", "scanner da NVS: '%s' %s", config.name.c_str(), config.mac.c_str());
    }
  }

  onix::BtConfigStatus status() const override {
    onix::BtConfigStatus result;
    result.custom = custom_;
    result.config = onix::obdScannerConfig();
    result.scan = onix::obdScanResult();
    return result;
  }

  bool save(const onix::BtScannerConfig& config) override {
    Preferences prefs;
    if (!prefs.begin(kObdPrefsNamespace, false)) {
      return false;
    }
    // putString devolve 0 para string vazia: grava só os campos preenchidos e remove os demais.
    auto put = [&prefs](const char* key, const std::string& value) {
      if (value.empty()) return !prefs.isKey(key) || prefs.remove(key);
      return prefs.putString(key, value.c_str()) == value.size();
    };
    const bool ok = put("name", config.name) && put("pin", config.pin) &&
                    put("mac", config.mac) && prefs.putBool("custom", true) == 1;
    prefs.end();
    if (!ok) {
      return false;
    }
    custom_ = true;
    onix::setObdScannerConfig(config);
    ONIX_LOGW("bt", "scanner configurado: '%s' %s", config.name.c_str(), config.mac.c_str());
    return true;
  }

  bool clear() override {
    Preferences prefs;
    if (!prefs.begin(kObdPrefsNamespace, false)) {
      return false;
    }
    const bool ok = prefs.clear();
    prefs.end();
    if (ok) {
      custom_ = false;
      onix::setObdScannerConfig(onix::defaultBtScannerConfig());
      ONIX_LOGW("bt", "scanner volta aos padroes");
    }
    return ok;
  }

  bool startScan() override { return onix::requestObdScan(); }

 private:
  bool custom_ = false;
};

NvsBtConfig g_btConfig;
ApiRouter g_apiRouter(g_snapshotStore, g_timing, onix::DASHBOARD_HTML, nullptr, nullptr,
                      &onix::globalLogBuffer(), &g_wifiConfig);
WebServer g_server(onix::ONIX_HTTP_PORT);
WebSocketsServer g_webSocket(onix::ONIX_WS_PORT);
uint32_t g_lastWsBroadcastMs = 0;
TaskHandle_t g_obdTaskHandle = nullptr;
uint32_t g_lastMetricsMs = 0;

HttpMethod mapHttpMethod(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return HttpMethod::Get;
    case HTTP_HEAD:
      return HttpMethod::Head;
    case HTTP_POST:
      return HttpMethod::Post;
    case HTTP_PUT:
      return HttpMethod::Put;
    case HTTP_PATCH:
      return HttpMethod::Patch;
    case HTTP_DELETE:
      return HttpMethod::Delete;
    case HTTP_OPTIONS:
      return HttpMethod::Options;
    default:
      return HttpMethod::Other;
  }
}

// Traduz a requisição atual do WebServer em HttpRequest, consulta o ApiRouter e materializa
// a HttpResponse: status, Content-Type, cabeçalhos (Cache-Control: no-store) e corpo —
// inclusive as respostas 404/405, decididas pelo roteador, nunca aqui.
void sendApiResponse(const HttpResponse& response) {
  for (const auto& header : response.headers) {
    g_server.sendHeader(header.name.c_str(), header.value.c_str());
  }
  g_server.send(response.status, response.contentType.c_str(), String(response.body.c_str()));
}

// Content-Type mínimo para os estáticos do frontend externo (web:stage -> data/).
const char* contentTypeForPath(const char* path) {
  const char* ext = strrchr(path, '.');
  if (ext == nullptr) {
    return "application/octet-stream";
  }
  if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
  if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
  if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
  if (strcmp(ext, ".json") == 0) return "application/json";
  if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
  if (strcmp(ext, ".png") == 0) return "image/png";
  if (strcmp(ext, ".ico") == 0) return "image/x-icon";
  return "application/octet-stream";
}

// Tenta servir um arquivo do LittleFS; retorna false se não existir ou não abrir.
// Rejeita ".." para não escapar do filesystem. Havendo sidecar .gz, ele é servido sempre
// (ADR-0010); streamFile acrescenta Content-Encoding: gzip e mantém o Content-Type original.
bool tryServeLittleFS(const char* path) {
  if (strstr(path, "..") != nullptr) {
    return false;
  }
  const String compressedPath = String(path) + ".gz";
  const std::string selectedPath =
      onix::selectAssetPath(path, LittleFS.exists(compressedPath));
  if (!LittleFS.exists(selectedPath.c_str())) {
    return false;
  }
  File file = LittleFS.open(selectedPath.c_str(), "r");
  if (!file) {
    return false;
  }
  g_server.sendHeader("Cache-Control", onix::cacheControlForAsset(path));
  g_server.streamFile(file, contentTypeForPath(path));
  file.close();
  return true;
}

void handleWithApiRouter() {
  HttpRequest request;
  request.method = mapHttpMethod(g_server.method());
  request.path = g_server.uri().c_str();
  if (request.method == HttpMethod::Post || request.method == HttpMethod::Delete) {
    request.body = g_server.arg("plain").c_str();  // corpo cru do WebServer
  }

  // GET / prefere o bundle externo (/index.html via web:stage); sem o arquivo, cai no
  // dashboard.h embarcado via ApiRouter (fallback offline, nunca removido).
  if (request.method == HttpMethod::Get && request.path == "/") {
    if (tryServeLittleFS("/index.html")) {
      return;
    }
  }

  const HttpResponse response = g_apiRouter.handle(request);
  // Demais estáticos do frontend externo quando existirem; senão, vale o 404 do roteador.
  // /api/data nunca chega aqui como arquivo: o ApiRouter responde 200 antes.
  if (response.status == 404 && request.method == HttpMethod::Get && request.path != "/") {
    if (tryServeLittleFS(request.path.c_str())) {
      return;
    }
  }
  sendApiResponse(response);
}

// Eventos do WebSocket: no CONNECT envia o snapshot atual só ao recém-chegado (mesmo JSON
// de GET /api/data, sem duplicar serialização); o broadcast periódico está em
// maybeBroadcastWs(). Desconexões/frames não exigem ação.
void onWsEvent(uint8_t num, WStype_t type, uint8_t* /*payload*/, size_t /*length*/) {
  if (type == WStype_CONNECTED) {
    const std::string json = g_apiRouter.buildDataJson();
    g_webSocket.sendTXT(num, json.c_str());
    g_lastWsBroadcastMs = millis();
    ONIX_LOGI("ws", "cliente #%u conectado", static_cast<unsigned>(num));
  } else if (type == WStype_DISCONNECTED) {
    ONIX_LOGI("ws", "cliente #%u desconectado", static_cast<unsigned>(num));
  }
}

// Broadcast a cada ONIX_WS_BROADCAST_MS SOMENTE com clientes conectados; throttle por
// millis() (ws_broadcast.h), sem delay — nunca bloqueia o loop().
void maybeBroadcastWs() {
  const bool hasClients = g_webSocket.connectedClients(false) > 0;
  if (!onix::shouldBroadcast(millis(), g_lastWsBroadcastMs, onix::ONIX_WS_BROADCAST_MS,
                             hasClients, false)) {
    return;
  }
  const std::string json = g_apiRouter.buildDataJson();
  g_webSocket.broadcastTXT(json.c_str());
  g_lastWsBroadcastMs = millis();
}

// Task OBD única (ADR-0004): ÚNICA produtora do SnapshotStore; HTTP só copia.
// runObdTask() (obd_transport_esp32.cpp) é o loop de produção: BluetoothSerial + ELMduino e a
// máquina ObdSession, que publica o snapshot a cada passo e espera o tempo sugerido pela sessão.
void obdTask(void* parameter) {
  auto& store = *static_cast<SnapshotStore*>(parameter);
  onix::runObdTask(store);
}

// ArduinoOTA (ADR-0009): espota grava firmware (U_FLASH) no slot livre ou o LittleFS (U_SPIFFS).
// Durante a gravação a task OBD fica em pausa cooperativa (cancela a busca BT e libera o rádio),
// e o LittleFS é desmontado antes de ser sobrescrito. Sucesso reinicia a placa; erro retoma a task.
void setupOta() {
  ArduinoOTA.setHostname(onix::ONIX_OTA_HOSTNAME);
  ArduinoOTA.setPassword(onix::ONIX_OTA_PASSWORD);
  ArduinoOTA.setPort(onix::ONIX_OTA_PORT);
  ArduinoOTA.onStart([]() {
    const bool filesystem = ArduinoOTA.getCommand() == U_SPIFFS;
    onix::setObdPaused(true);
    if (filesystem) {
      LittleFS.end();
    }
    ONIX_LOGW("ota", "iniciada (%s)", filesystem ? "LittleFS" : "firmware");
  });
  ArduinoOTA.onEnd([]() { ONIX_LOGW("ota", "concluída; reiniciando"); });
  ArduinoOTA.onError([](ota_error_t error) {
    ONIX_LOGE("ota", "falhou (erro %u)", static_cast<unsigned>(error));
    onix::setObdPaused(false);
  });
  ArduinoOTA.begin();
  ONIX_LOGI("ota", "porta %u", static_cast<unsigned>(onix::ONIX_OTA_PORT));
}

// Sink do log no Serial (ADR-0011): toda linha também fica no buffer lido por GET /api/logs.
void serialLogSink(const char* line) { Serial.println(line); }

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_SW:
      return "software (OTA ou restart)";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "watchdog de interrupção";
    case ESP_RST_TASK_WDT:
      return "watchdog de task";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout (queda de tensão)";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_EXT:
      return "pino de reset";
    default:
      return "desconhecido";
  }
}

// Motivo do reset e contador de boots na NVS: no carro, quedas de tensão e watchdog reiniciam a
// placa sem ninguém olhando a serial; o contador sobrevive a resets e regravações.
void logBootInfo() {
  Preferences prefs;
  uint32_t boots = 0;
  if (prefs.begin("onix", false)) {
    boots = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", boots);
    prefs.end();
  }
  const esp_reset_reason_t reason = esp_reset_reason();
  const bool abnormal = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
                        reason == ESP_RST_BROWNOUT;
  onix::logf(abnormal ? onix::LogLevel::Warn : onix::LogLevel::Info, "boot",
             "boot #%lu, reset: %s", static_cast<unsigned long>(boots), resetReasonName(reason));
}

// Métricas a cada ONIX_METRICS_INTERVAL_MS, por millis() e sem delay (mesmo padrão do broadcast).
void maybeLogMetrics() {
  const uint32_t now = millis();
  if (now - g_lastMetricsMs < onix::ONIX_METRICS_INTERVAL_MS) {
    return;
  }
  g_lastMetricsMs = now;
  const unsigned obdStackFree =
      g_obdTaskHandle != nullptr
          ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_obdTaskHandle) * sizeof(StackType_t))
          : 0;
  ONIX_LOGI("sys", "heap livre %u B (mín %u, maior bloco %u) | pilha OBD livre %u B | uptime %lu s",
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()), obdStackFree,
            static_cast<unsigned long>(now / 1000));
}

void loadWifiCredentials() {
  Preferences prefs;
  // Somente leitura falha quando o namespace ainda não existe: sem credencial.
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return;
  }
  g_wifiCredentials.ssid = prefs.getString("ssid", "").c_str();
  g_wifiCredentials.password = prefs.getString("pass", "").c_str();
  prefs.end();
  g_hasWifiCredentials = onix::isValidWifiCredentials(g_wifiCredentials);
}

// SoftAP de config.h. Com hotspot cadastrado, a interface STA fica ligada (sem conectar) para o
// scan periódico; sem cadastro, só o AP.
void startSoftAp() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(g_hasWifiCredentials ? WIFI_AP_STA : WIFI_AP);
  const IPAddress apIp(onix::ONIX_AP_IP_OCTETS[0], onix::ONIX_AP_IP_OCTETS[1],
                       onix::ONIX_AP_IP_OCTETS[2], onix::ONIX_AP_IP_OCTETS[3]);
  const IPAddress apNetmask(onix::ONIX_AP_NETMASK_OCTETS[0], onix::ONIX_AP_NETMASK_OCTETS[1],
                            onix::ONIX_AP_NETMASK_OCTETS[2], onix::ONIX_AP_NETMASK_OCTETS[3]);
  if (!WiFi.softAPConfig(apIp, apIp, apNetmask)) {
    ONIX_LOGE("wifi", "falha ao configurar o SoftAP");
  }
  if (!WiFi.softAP(onix::ONIX_AP_SSID, onix::ONIX_AP_PASSWORD, onix::ONIX_AP_CHANNEL,
                   onix::ONIX_AP_HIDDEN ? 1 : 0, onix::ONIX_AP_MAX_CLIENTS)) {
    ONIX_LOGE("wifi", "falha ao iniciar o SoftAP");
  }
  ONIX_LOGI("wifi", "SoftAP '%s' canal %u | IP efetivo: %s", onix::ONIX_AP_SSID,
            static_cast<unsigned>(onix::ONIX_AP_CHANNEL), WiFi.softAPIP().toString().c_str());
}

// Cliente do hotspot por DHCP; o IP fixo é escolhido em StationUp.
void startStation() {
  WiFi.softAPdisconnect(true);
  WiFi.setHostname(onix::ONIX_STA_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_wifiCredentials.ssid.c_str(), g_wifiCredentials.password.c_str());
  ONIX_LOGI("wifi", "conectando ao hotspot '%s'", g_wifiCredentials.ssid.c_str());
}

// Sonda ARP de um candidato a IP fixo. As funções etharp_* só podem rodar na thread do lwIP:
// cada passo vai por tcpip_callback e o loop() espera o semáforo.
struct ArpProbe {
  ip4_addr_t address;
  bool hasNetif = false;
  bool found = false;
  SemaphoreHandle_t done = nullptr;
};

struct netif* findStationNetif() {
  for (struct netif* item = netif_list; item != nullptr; item = item->next) {
    if (item->name[0] == 's' && item->name[1] == 't') {  // "st" = WIFI_STA_DEF no ESP-IDF
      return item;
    }
  }
  return nullptr;
}

void arpRequestOnTcpip(void* context) {
  auto* probe = static_cast<ArpProbe*>(context);
  struct netif* stationNetif = findStationNetif();
  probe->hasNetif = stationNetif != nullptr;
  if (probe->hasNetif) {
    etharp_request(stationNetif, &probe->address);
  }
  xSemaphoreGive(probe->done);
}

void arpLookupOnTcpip(void* context) {
  auto* probe = static_cast<ArpProbe*>(context);
  struct netif* stationNetif = findStationNetif();
  struct eth_addr* mac = nullptr;
  const ip4_addr_t* cached = nullptr;
  probe->found = stationNetif != nullptr &&
                 etharp_find_addr(stationNetif, &probe->address, &mac, &cached) >= 0;
  xSemaphoreGive(probe->done);
}

// Sem netif ou sem semáforo o candidato conta como ocupado: na dúvida, fica no DHCP.
bool hostInUseByArp(const onix::Ipv4& address, void* /*context*/) {
  ArpProbe probe;
  IP4_ADDR(&probe.address, address[0], address[1], address[2], address[3]);
  probe.done = xSemaphoreCreateBinary();
  if (probe.done == nullptr) {
    return true;
  }
  bool inUse = true;
  if (tcpip_callback(arpRequestOnTcpip, &probe) == ERR_OK) {
    xSemaphoreTake(probe.done, portMAX_DELAY);
    if (probe.hasNetif) {
      delay(onix::ONIX_STA_ARP_PROBE_MS);
      if (tcpip_callback(arpLookupOnTcpip, &probe) == ERR_OK) {
        xSemaphoreTake(probe.done, portMAX_DELAY);
        inUse = probe.found;
      }
    }
  }
  vSemaphoreDelete(probe.done);
  return inUse;
}

onix::Ipv4 toIpv4(const IPAddress& address) {
  return {address[0], address[1], address[2], address[3]};
}

// Conectou ao hotspot: troca o IP do DHCP por .50–.59 na faixa do gateway (ADR-0012), para o
// endereço do painel ser previsível mesmo com a faixa sorteada pelo Android.
void applyStaticHost() {
  const IPAddress current = WiFi.localIP();
  const IPAddress gateway = WiFi.gatewayIP();
  const IPAddress netmask = WiFi.subnetMask();
  const std::optional<onix::Ipv4> chosen = onix::selectStaticHost(
      toIpv4(gateway), toIpv4(netmask), toIpv4(current), onix::ONIX_STA_STATIC_HOST_FIRST,
      onix::ONIX_STA_STATIC_HOST_COUNT, hostInUseByArp, nullptr);
  if (!chosen.has_value()) {
    ONIX_LOGW("wifi", "hotspot conectado; .%u-.%u ocupados ou fora da faixa, IP do DHCP %s",
              static_cast<unsigned>(onix::ONIX_STA_STATIC_HOST_FIRST),
              static_cast<unsigned>(onix::ONIX_STA_STATIC_HOST_FIRST +
                                    onix::ONIX_STA_STATIC_HOST_COUNT - 1),
              current.toString().c_str());
    return;
  }
  const IPAddress target((*chosen)[0], (*chosen)[1], (*chosen)[2], (*chosen)[3]);
  if (target == current) {
    ONIX_LOGI("wifi", "hotspot conectado; IP %s (DHCP já na faixa fixa)",
              current.toString().c_str());
    return;
  }
  const IPAddress dns = WiFi.dnsIP(0) == IPAddress(0, 0, 0, 0) ? gateway : WiFi.dnsIP(0);
  if (!WiFi.config(target, gateway, netmask, dns)) {
    ONIX_LOGW("wifi", "hotspot conectado; falha ao fixar %s, IP do DHCP %s",
              target.toString().c_str(), current.toString().c_str());
    return;
  }
  ONIX_LOGI("wifi", "hotspot conectado; IP fixo %s (gateway %s)", target.toString().c_str(),
            gateway.toString().c_str());
}

void applyWifiAction(onix::WifiAction action) {
  switch (action) {
    case onix::WifiAction::None:
      return;
    case onix::WifiAction::StartStation:
      startStation();
      return;
    case onix::WifiAction::StationUp:
      applyStaticHost();
      return;
    case onix::WifiAction::StationDown:
      // A reconexão automática do Arduino reusaria o IP fixo; com DHCP, um hotspot religado em
      // outra faixa continua funcionando.
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
      ONIX_LOGW("wifi", "hotspot perdido; reconectando por até %lu s",
                static_cast<unsigned long>(onix::ONIX_STA_RECONNECT_TIMEOUT_MS / 1000));
      return;
    case onix::WifiAction::StartAccessPoint:
      if (g_hasWifiCredentials) {
        ONIX_LOGW("wifi", "hotspot '%s' indisponível; subindo o SoftAP",
                  g_wifiCredentials.ssid.c_str());
      }
      startSoftAp();
      return;
    case onix::WifiAction::StartScan:
      if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        ONIX_LOGD("wifi", "falha ao iniciar o scan");
        return;
      }
      g_wifiScanRunning = true;
      return;
  }
}

// Chamado a cada loop(): colhe o scan assíncrono e aplica a decisão da WifiPolicy.
void tickWifi() {
  const uint64_t now = onix::monotonicUptimeMs();
  const uint8_t apClients = WiFi.softAPgetStationNum();
  if (g_wifiScanRunning) {
    const int16_t count = WiFi.scanComplete();
    if (count != WIFI_SCAN_RUNNING) {
      g_wifiScanRunning = false;
      bool found = false;
      for (int16_t i = 0; i < count; ++i) {
        if (WiFi.SSID(i) == g_wifiCredentials.ssid.c_str()) {
          found = true;
          break;
        }
      }
      WiFi.scanDelete();
      if (found) {
        ONIX_LOGI("wifi", "hotspot '%s' encontrado no scan", g_wifiCredentials.ssid.c_str());
      }
      applyWifiAction(g_wifiPolicy.onScanResult(now, found, apClients));
    }
  }
  applyWifiAction(
      g_wifiPolicy.tick(now, WiFi.status() == WL_CONNECTED, apClients, g_wifiScanRunning));
}

// POST/DELETE /api/wifi: reinicia depois de a resposta sair.
void maybeRestart() {
  if (g_restartPending && millis() - g_restartRequestedMs >= onix::ONIX_WIFI_RESTART_DELAY_MS) {
    ESP.restart();
  }
}

}  // namespace

void setup() {
  Serial.begin(onix::ONIX_SERIAL_BAUD);
  delay(200);  // aguarda a serial subir antes dos primeiros logs
  onix::setLogSink(serialLogSink);
  logBootInfo();

  // Hotspot cadastrado: tenta a STA por ONIX_STA_CONNECT_TIMEOUT_MS; senão (ou sem cadastro),
  // SoftAP. As transições seguintes ficam em tickWifi() (ADR-0012).
  loadWifiCredentials();
  g_btConfig.load();  // antes da task OBD: a primeira conexão já usa o scanner da NVS
  applyWifiAction(g_wifiPolicy.begin(g_hasWifiCredentials, onix::monotonicUptimeMs()));

  // LittleFS (frontend externo via web:stage -> data/): sem /index.html, GET / usa o
  // dashboard.h embarcado (fallback em handleWithApiRouter). Falha na montagem é logada;
  // o HTTP segue de pé com o fallback.
  if (!LittleFS.begin()) {
    ONIX_LOGW("fs", "falha ao montar o LittleFS; usando fallback embarcado");
  } else {
    ONIX_LOGI("fs", "LittleFS montado");
  }

  // POST /api/obd/reconnect (ADR-0013): o roteador só sinaliza; a task OBD retoma a busca no
  // próprio contexto (requestObdReconnect usa um atômico, a sessão nunca é tocada daqui).
  g_apiRouter.setObdReconnectHandler([](void*) { onix::requestObdReconnect(); }, nullptr);
  g_apiRouter.setBtConfigPort(&g_btConfig);

  // Rotas em HTTP_ANY (padrão do on(uri, fn)): o handler é chamado para QUALQUER método e o
  // ApiRouter responde 405 para não-GET; rotas desconhecidas caem em onNotFound -> 404
  // (ou estático do LittleFS, se existir).
  g_server.on(onix::API_ROUTE_ROOT, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_DATA, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_LOGS, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_WIFI, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_OBD_RECONNECT, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_OBD_CONFIG, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_OBD_SCAN, handleWithApiRouter);
  g_server.onNotFound(handleWithApiRouter);
  g_server.begin();
  ONIX_LOGI("http", "porta %u", static_cast<unsigned>(onix::ONIX_HTTP_PORT));

  g_webSocket.begin();
  g_webSocket.onEvent(onWsEvent);
  ONIX_LOGI("ws", "porta %u (broadcast a cada %lu ms)", static_cast<unsigned>(onix::ONIX_WS_PORT),
            static_cast<unsigned long>(onix::ONIX_WS_BROADCAST_MS));

  // Task OBD única (ADR-0004), pinada ao núcleo ONIX_OBD_TASK_CORE (0), junto das pilhas Wi-Fi
  // e Bluetooth. Layout: o núcleo 0 roda OBD/BT; o núcleo 1 roda só o loop() (HTTP/WS/OTA).
  // Prioridades maiores (Wi-Fi 23, tcpip 18, Bluedroid) preemptam a task OBD (prio 1). O ELMduino
  // faz busy-wait de até 30 s sem yield na busca de protocolo (sendCommand_Blocking) e, com prio 1,
  // starvaria o IDLE0 do núcleo 0; por isso o task WDT do IDLE0 é desligado aqui. Trade-off: sem
  // vigilância do IDLE0, um travamento do núcleo 0 não reinicia mais a placa pelo WDT.
  // Stack e prioridade vêm de config.h. Falha na criação é logada; o HTTP segue de pé.
#if !defined(ONIX_DISABLE_OBD)
  if (onix::ONIX_OBD_TASK_CORE == 0) {
    disableCore0WDT();
  }
  const BaseType_t created =
      xTaskCreatePinnedToCore(obdTask, "onix-obd", onix::ONIX_OBD_TASK_STACK_SIZE,
                              &g_snapshotStore, onix::ONIX_OBD_TASK_PRIORITY, &g_obdTaskHandle,
                              onix::ONIX_OBD_TASK_CORE);
  if (created != pdPASS) {
    ONIX_LOGE("obd", "falha ao criar a task OBD");
  } else {
    ONIX_LOGI("obd", "task no núcleo %u", static_cast<unsigned>(onix::ONIX_OBD_TASK_CORE));
  }
#else
  // Variante de medição (env esp32-no-obd): existe só para medir Wi-Fi/HTTP sem o rádio BT.
  // Sem BT, a economia de energia do Wi-Fi pode ser desligada.
  ONIX_LOGW("obd", "task OBD desativada (ONIX_DISABLE_OBD)");
  WiFi.setSleep(false);
#endif

  setupOta();
}

void loop() {
  g_server.handleClient();  // não bloqueia: a coleta OBD pertence exclusivamente à task
  g_webSocket.loop();       // idem: bombeia frames sem bloquear
  maybeBroadcastWs();       // throttle por millis(), só com clientes; sem delay
  ArduinoOTA.handle();      // só bloqueia durante uma gravação OTA em andamento
  maybeLogMetrics();        // throttle por millis(); sem delay
  tickWifi();               // STA/AP/scan (ADR-0012); só a sonda ARP ao conectar espera
  maybeRestart();           // após POST/DELETE /api/wifi
}

#else

// Build nativo (env `native`, test_build_src=yes compila src/): main.cpp é específico do
// ESP32/Arduino e não compila nada aqui. Os testes usam api/snapshot/obd sem transporte.
namespace onix {
constexpr int kMainCppNativePlaceholder = 0;
}

#endif
