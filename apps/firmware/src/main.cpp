// main.cpp — integração do firmware ONIX no ESP32 (SPEC-0002, ADR-0004, ADR-0005).
//
// PONTO ÚNICO de contato com Arduino/ESP32: SoftAP, WebServer e a task FreeRTOS do OBD.
// A política HTTP (rotas, 404/405, cabeçalhos, serialização) vive em ApiRouter (api.cpp);
// aqui apenas traduzimos HttpRequest/HttpResponse em chamadas do WebServer.
//
// Portátil: o env `native` (test_build_src=yes) compila src/ para os testes Unity, então
// TODO o código Arduino fica sob guarda — o build nativo não vê WiFi/WebServer/FreeRTOS.

#if defined(ARDUINO) || defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <cstring>

#include "api.h"
#include "config.h"
#include "dashboard.h"
#include "snapshot.h"
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
ApiRouter g_apiRouter(g_snapshotStore, g_timing, onix::DASHBOARD_HTML);
WebServer g_server(onix::ONIX_HTTP_PORT);
WebSocketsServer g_webSocket(onix::ONIX_WS_PORT);
uint32_t g_lastWsBroadcastMs = 0;

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
// Rejeita ".." para não escapar do filesystem.
bool tryServeLittleFS(const char* path) {
  if (strstr(path, "..") != nullptr) {
    return false;
  }
  if (!LittleFS.exists(path)) {
    return false;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
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
    Serial.printf("[onix] WS cliente #%u conectado\n", static_cast<unsigned>(num));
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[onix] WS cliente #%u desconectado\n", static_cast<unsigned>(num));
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
// Scaffold cooperativo enquanto runObdTask() não existe em obd.cpp (fase RED): publica um
// snapshot inicial indisponível e espera em ciclos curtos, sem bloquear o loop HTTP.
// Quando runObdTask() for implementado (ou ObdSession::step() ganhar transporte real),
// este corpo passa a chamar onix::runObdTask(store) ou step() cooperativo.
void obdTask(void* parameter) {
  auto& store = *static_cast<SnapshotStore*>(parameter);
  store.publish(onix::Snapshot{});  // estado inicial: sem BT, sem adapter, tudo unavailable
  for (;;) {
    vTaskDelay(1);  // ~200 ms cooperativo (ONIX_OBD_IDLE_WAIT_MS); sem depender de obd.h aqui
  }
}

}  // namespace

void setup() {
  Serial.begin(onix::ONIX_SERIAL_BAUD);
  delay(200);  // aguarda a serial subir antes dos primeiros logs

  WiFi.mode(WIFI_AP);
  const IPAddress apIp(onix::ONIX_AP_IP_OCTETS[0], onix::ONIX_AP_IP_OCTETS[1],
                       onix::ONIX_AP_IP_OCTETS[2], onix::ONIX_AP_IP_OCTETS[3]);
  const IPAddress apNetmask(onix::ONIX_AP_NETMASK_OCTETS[0], onix::ONIX_AP_NETMASK_OCTETS[1],
                            onix::ONIX_AP_NETMASK_OCTETS[2], onix::ONIX_AP_NETMASK_OCTETS[3]);
  if (!WiFi.softAPConfig(apIp, apIp, apNetmask)) {
    Serial.println("[onix] falha ao configurar o SoftAP");
  }
  if (!WiFi.softAP(onix::ONIX_AP_SSID, onix::ONIX_AP_PASSWORD, onix::ONIX_AP_CHANNEL,
                   onix::ONIX_AP_HIDDEN ? 1 : 0, onix::ONIX_AP_MAX_CLIENTS)) {
    Serial.println("[onix] falha ao iniciar o SoftAP");
  }
  const IPAddress effectiveIp = WiFi.softAPIP();
  Serial.printf("[onix] SoftAP '%s' canal %u | IP efetivo: %s\n", onix::ONIX_AP_SSID,
                static_cast<unsigned>(onix::ONIX_AP_CHANNEL), effectiveIp.toString().c_str());

  // LittleFS (frontend externo via web:stage -> data/): sem /index.html, GET / usa o
  // dashboard.h embarcado (fallback em handleWithApiRouter). Falha na montagem é logada;
  // o HTTP segue de pé com o fallback.
  if (!LittleFS.begin()) {
    Serial.println("[onix] LittleFS: falha ao montar; usando fallback embarcado");
  } else {
    Serial.println("[onix] LittleFS montado");
  }

  // Rotas em HTTP_ANY (padrão do on(uri, fn)): o handler é chamado para QUALQUER método e o
  // ApiRouter responde 405 para não-GET; rotas desconhecidas caem em onNotFound -> 404
  // (ou estático do LittleFS, se existir).
  g_server.on(onix::API_ROUTE_ROOT, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_DATA, handleWithApiRouter);
  g_server.onNotFound(handleWithApiRouter);
  g_server.begin();
  Serial.printf("[onix] HTTP na porta %u\n", static_cast<unsigned>(onix::ONIX_HTTP_PORT));

  g_webSocket.begin();
  g_webSocket.onEvent(onWsEvent);
  Serial.printf("[onix] WebSocket na porta %u (broadcast a cada %lu ms)\n",
                static_cast<unsigned>(onix::ONIX_WS_PORT),
                static_cast<unsigned long>(onix::ONIX_WS_BROADCAST_MS));

  // Task OBD única, pinada ao núcleo 0 (loopTask/WiFi do Arduino vivem no 1/0); stack e
  // prioridade vêm de config.h. Falha na criação é logada; o HTTP segue de pé.
  const BaseType_t created =
      xTaskCreatePinnedToCore(obdTask, "onix-obd", onix::ONIX_OBD_TASK_STACK_SIZE,
                              &g_snapshotStore, onix::ONIX_OBD_TASK_PRIORITY, nullptr, 0);
  if (created != pdPASS) {
    Serial.println("[onix] falha ao criar a task OBD");
  }
}

void loop() {
  g_server.handleClient();  // não bloqueia: a coleta OBD pertence exclusivamente à task
  g_webSocket.loop();       // idem: bombeia frames sem bloquear
  maybeBroadcastWs();       // throttle por millis(), só com clientes; sem delay
}

#else

// Build nativo (env `native`, test_build_src=yes compila src/): main.cpp é específico do
// ESP32/Arduino e não compila nada aqui. Os testes usam api/snapshot/obd sem transporte.
namespace onix {
constexpr int kMainCppNativePlaceholder = 0;
}

#endif
