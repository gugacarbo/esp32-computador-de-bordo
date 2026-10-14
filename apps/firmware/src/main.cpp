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
#include <WebServer.h>
#include <WiFi.h>

#include "api.h"
#include "config.h"
#include "dashboard.h"
#include "snapshot.h"

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
void handleWithApiRouter() {
  HttpRequest request;
  request.method = mapHttpMethod(g_server.method());
  request.path = g_server.uri().c_str();

  const HttpResponse response = g_apiRouter.handle(request);
  for (const auto& header : response.headers) {
    g_server.sendHeader(header.name.c_str(), header.value.c_str());
  }
  g_server.send(response.status, response.contentType.c_str(), String(response.body.c_str()));
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

  // Rotas em HTTP_ANY (padrão do on(uri, fn)): o handler é chamado para QUALQUER método e o
  // ApiRouter responde 405 para não-GET; rotas desconhecidas caem em onNotFound -> 404.
  g_server.on(onix::API_ROUTE_ROOT, handleWithApiRouter);
  g_server.on(onix::API_ROUTE_DATA, handleWithApiRouter);
  g_server.onNotFound(handleWithApiRouter);
  g_server.begin();
  Serial.printf("[onix] HTTP na porta %u\n", static_cast<unsigned>(onix::ONIX_HTTP_PORT));

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
  delay(1);                 // cede fatia ao IDLE/watchdog entre iterações
}

#else

// Build nativo (env `native`, test_build_src=yes compila src/): main.cpp é específico do
// ESP32/Arduino e não compila nada aqui. Os testes usam api/snapshot/obd sem transporte.
namespace onix {
constexpr int kMainCppNativePlaceholder = 0;
}

#endif
