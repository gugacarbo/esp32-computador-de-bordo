// api.h — contrato HTTP do firmware (SPEC-0002, ADR-0005), sem transporte.
// Esta camada deriva saúde da ECU, frescor e status dos sensores a partir de UMA cópia do
// snapshot; ela não conhece ELMduino, Bluetooth nem WebServer. O adaptador para o WebServer
// fica em main.cpp, que apenas traduz HttpRequest/HttpResponse em chamadas do WebServer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bt_config.h"
#include "config.h"
#include "log.h"
#include "snapshot.h"
#include "wifi_config.h"

namespace onix {

enum class HttpMethod { Get, Head, Post, Put, Patch, Delete, Options, Other };

struct HttpRequest {
  HttpMethod method = HttpMethod::Get;
  std::string path;
  std::string body;  // corpo cru (POST/DELETE /api/wifi); vazio nos demais
};

struct HttpHeader {
  std::string name;
  std::string value;
};

// Resposta já materializada em RAM: status, cabeçalhos e corpo. Nenhuma escrita de socket
// acontece aqui, o que permite testar rotas, métodos e cabeçalhos com g++ nativo.
struct HttpResponse {
  int status = 200;
  std::string contentType;
  std::vector<HttpHeader> headers;
  std::string body;

  bool hasHeader(const std::string& name) const;
  std::string headerValue(const std::string& name) const;
};

constexpr char API_ROUTE_ROOT[] = "/";
constexpr char API_ROUTE_DATA[] = "/api/data";
constexpr char API_ROUTE_LOGS[] = "/api/logs";
constexpr char API_ROUTE_WIFI[] = "/api/wifi";
constexpr char API_ROUTE_OBD_CONFIG[] = "/api/obd/config";
constexpr char API_ROUTE_OBD_SCAN[] = "/api/obd/scan";
constexpr char API_ROUTE_OBD_RECONNECT[] = "/api/obd/reconnect";

// Base monotônica em milissegundos desde o boot (esp_timer no ESP32, steady_clock no host).
// É a mesma base de `uptimeMs`, `updatedAtMs`, `ageMs` e das janelas de saúde/frescor.
// A task OBD (obd.h) deve usar esta função para carimbar `updatedAtMs`.
uint64_t monotonicUptimeMs();

// Fonte de relógio injetável: os testes usam um relógio controlado (SPEC-0002 DoD).
using UptimeProvider = uint64_t (*)(void* context);

// Ação sem argumentos disparada pela rota POST (ex.: pedir reconexão à task OBD).
using ActionHandler = void (*)(void* context);

const char* sensorJsonKey(SensorId id);       // "rpm", "speedKph", ...
const char* sensorStatusName(SensorStatus s); // "unavailable", ..., "ok"
const char* voltageSourceName(VoltageSource s);  // "adapter" | "ecu"

// Saúde derivada da cópia + relógio atual (SPEC-0002 §Envelope JSON).
// adapterReady/ecuResponding expiram sozinhos: a expiração não depende de o produtor publicar
// uma mudança (ADR-0004) nem de `AT RV` (SPEC-0002 caso 9).
struct SessionPhases {
  bool btConnected = false;
  bool adapterReady = false;
  bool ecuResponding = false;
  bool obdConnected = false;
};

SessionPhases deriveSessionPhases(const Snapshot& snapshot, const TimingConfig& timing, uint64_t nowMs);

// Idade não negativa; 0 quando o carimbo está no futuro (relógio controlado/testes).
uint64_t sensorAgeMs(const SensorReading& reading, uint64_t nowMs);

// Precedência de SPEC-0002 §Estados do sensor: suporte ausente; origem indisponível;
// falha da última consulta; expiração; leitura disponível e recente.
SensorStatus resolveSensorStatus(const Snapshot& snapshot, SensorId id, const TimingConfig& timing,
                                 uint64_t nowMs);
bool isSensorValid(const Snapshot& snapshot, SensorId id, const TimingConfig& timing, uint64_t nowMs);

// Número JSON finito; nunca emite NaN ou Infinity.
std::string formatJsonNumber(double value);

// Envelope JSON completo de SPEC-0002, com os seis campos sempre presentes e `voltage.source`
// sempre explícito. `nowMs` e `uptimeMs` vêm da mesma base monotônica.
std::string serializeSnapshotJson(const Snapshot& snapshot, const TimingConfig& timing,
                                  uint64_t uptimeMs, uint64_t nowMs);

// Roteador testável: decide status/cabeçalhos/corpo sem transporte.
// Fluxo obrigatório (ADR-0004): copiar o snapshot, liberar o mutex e só então serializar.
class ApiRouter {
 public:
  ApiRouter(const SnapshotStore& store, const TimingConfig& timing, const char* dashboardHtml,
            UptimeProvider uptime = nullptr, void* uptimeContext = nullptr,
            const LogBuffer* logs = nullptr, WifiConfigPort* wifi = nullptr);

  // GET /           -> 200 text/html; charset=utf-8 + HTML do painel (dashboard.h)
  // GET /api/data   -> 200 application/json + Cache-Control: no-store
  // GET /api/logs   -> 200 application/json + Cache-Control: no-store (buffer de log; vazio se não houver buffer)
  // /api/wifi (ADR-0012; sem WifiConfigPort injetado a rota é desconhecida -> 404):
  //   GET    -> 200 JSON serializeWifiStatusJson + no-store
  //   POST   -> corpo inválido 400; credencial inválida 400; save() falhou 500; sucesso 200
  //             {"ok":true,"restarting":true}
  //   DELETE -> corpo ignorado; clear() falhou 500; sucesso 200 {"ok":true,"restarting":true}
  //   outro  -> 405 com Allow: GET, POST, DELETE
  //   Todas as respostas JSON de /api/wifi (inclusive erros {"ok":false,"error":"..."}) levam
  //   Cache-Control: no-store.
  // /api/obd/reconnect (rota sempre conhecida):
  //   POST  -> com handler injetado: 202 application/json {"ok":true} + no-store; sem handler: 503 {"ok":false}
  //   outro -> 405 com Allow: POST
  // /api/obd/config e /api/obd/scan (ADR-0014; sem BtConfigPort injetado são desconhecidas -> 404):
  //   GET    /api/obd/config -> 200 JSON serializeBtConfigStatusJson + no-store
  //   POST   /api/obd/config -> corpo inválido 400 invalid-json; config inválida 400 invalid-config;
  //                             save() falhou 500 storage; sucesso 200 {"ok":true}
  //   DELETE /api/obd/config -> clear() falhou 500 storage; sucesso 200 {"ok":true}
  //   POST   /api/obd/scan   -> 202 {"ok":true}; busca já pendente 409 {"ok":false,"error":"busy"}
  //   outro método -> 405 com Allow: GET, POST, DELETE (config) ou POST (scan); JSON sempre no-store.
  // método != GET em rota conhecida -> 405, sem ler o snapshot
  // rota desconhecida -> 404, sem ler o snapshot
  HttpResponse handle(const HttpRequest& request) const;

  const SnapshotStore& store() const { return store_; }
  const TimingConfig& timing() const { return timing_; }

  // Mesmo JSON de GET /api/data, reutilizado pelo broadcast WebSocket (porta 81):
  // copia o snapshot, libera o mutex e só então serializa (ADR-0004). Público para não
  // duplicar serialização em main.cpp.
  std::string buildDataJson() const;

  // Corpo de GET /api/logs: copia as entradas do buffer e serializa fora do mutex do buffer.
  std::string buildLogsJson() const;

  // Injeta o handler de POST /api/obd/reconnect. Sem handler a rota responde 503.
  void setObdReconnectHandler(ActionHandler handler, void* context);

  // Injeta a porta de /api/obd/config e /api/obd/scan. Sem porta as rotas respondem 404.
  void setBtConfigPort(BtConfigPort* port) { btConfig_ = port; }

 private:
  const SnapshotStore& store_;
  const TimingConfig& timing_;
  const char* dashboardHtml_;
  UptimeProvider uptime_;
  void* uptimeContext_;
  const LogBuffer* logs_;
  WifiConfigPort* wifi_;
  ActionHandler obdReconnect_ = nullptr;
  void* obdReconnectContext_ = nullptr;
  BtConfigPort* btConfig_ = nullptr;
};

}  // namespace onix
