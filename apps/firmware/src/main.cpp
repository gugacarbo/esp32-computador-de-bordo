// main.cpp — integração do firmware ONIX no ESP32 (SPEC-0002, ADR-0004, ADR-0005, ADR-0009,
// ADR-0011).
//
// PONTO ÚNICO de contato com Arduino/ESP32: SoftAP, WebServer, ArduinoOTA, NVS, o sink Serial do
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

#include "esp_system.h"

#include "api.h"
#include "config.h"
#include "dashboard.h"
#include "log.h"
#include "obd.h"
#include "snapshot.h"
#include "static_asset_policy.h"
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
ApiRouter g_apiRouter(g_snapshotStore, g_timing, onix::DASHBOARD_HTML, nullptr, nullptr,
                      &onix::globalLogBuffer());
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

}  // namespace

void setup() {
  Serial.begin(onix::ONIX_SERIAL_BAUD);
  delay(200);  // aguarda a serial subir antes dos primeiros logs
  onix::setLogSink(serialLogSink);
  logBootInfo();

  WiFi.mode(WIFI_AP);
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
  const IPAddress effectiveIp = WiFi.softAPIP();
  ONIX_LOGI("wifi", "SoftAP '%s' canal %u | IP efetivo: %s", onix::ONIX_AP_SSID,
            static_cast<unsigned>(onix::ONIX_AP_CHANNEL), effectiveIp.toString().c_str());

  // LittleFS (frontend externo via web:stage -> data/): sem /index.html, GET / usa o
  // dashboard.h embarcado (fallback em handleWithApiRouter). Falha na montagem é logada;
  // o HTTP segue de pé com o fallback.
  if (!LittleFS.begin()) {
    ONIX_LOGW("fs", "falha ao montar o LittleFS; usando fallback embarcado");
  } else {
    ONIX_LOGI("fs", "LittleFS montado");
  }

  // Rotas em HTTP_ANY (padrão do on(uri, fn)): o handler é chamado para QUALQUER método e o
  // ApiRouter responde 405 para não-GET; rotas desconhecidas caem em onNotFound -> 404
  // (ou estático do LittleFS, se existir).
  g_server.on(onix::API_ROUTE_ROOT, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_DATA, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_LOGS, handleWithApiRouter);
  g_server.onNotFound(handleWithApiRouter);
  g_server.begin();
  ONIX_LOGI("http", "porta %u", static_cast<unsigned>(onix::ONIX_HTTP_PORT));

  g_webSocket.begin();
  g_webSocket.onEvent(onWsEvent);
  ONIX_LOGI("ws", "porta %u (broadcast a cada %lu ms)", static_cast<unsigned>(onix::ONIX_WS_PORT),
            static_cast<unsigned long>(onix::ONIX_WS_BROADCAST_MS));

  // Task OBD única, pinada ao núcleo ONIX_OBD_TASK_CORE (1), longe do IDLE0. O task WDT do
  // ESP32 vigia o IDLE0 (panic=y, 5 s): o ELMduino faz busy-wait de até 30 s durante a busca
  // de protocolo (sendCommand_Blocking), o que travaria o IDLE0 e dispararia o reset. O loop
  // HTTP/WebSocket fica no núcleo 1 ao lado da task OBD. Stack e prioridade vêm de config.h.
  // Falha na criação é logada; o HTTP segue de pé.
  const BaseType_t created =
      xTaskCreatePinnedToCore(obdTask, "onix-obd", onix::ONIX_OBD_TASK_STACK_SIZE,
                              &g_snapshotStore, onix::ONIX_OBD_TASK_PRIORITY, &g_obdTaskHandle,
                              onix::ONIX_OBD_TASK_CORE);
  if (created != pdPASS) {
    ONIX_LOGE("obd", "falha ao criar a task OBD");
  }

  setupOta();
}

void loop() {
  g_server.handleClient();  // não bloqueia: a coleta OBD pertence exclusivamente à task
  g_webSocket.loop();       // idem: bombeia frames sem bloquear
  maybeBroadcastWs();       // throttle por millis(), só com clientes; sem delay
  ArduinoOTA.handle();      // só bloqueia durante uma gravação OTA em andamento
  maybeLogMetrics();        // throttle por millis(); sem delay
}

#else

// Build nativo (env `native`, test_build_src=yes compila src/): main.cpp é específico do
// ESP32/Arduino e não compila nada aqui. Os testes usam api/snapshot/obd sem transporte.
namespace onix {
constexpr int kMainCppNativePlaceholder = 0;
}

#endif
