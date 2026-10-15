// api.h — contrato HTTP do firmware (SPEC-0002, ADR-0005), sem transporte.
// Esta camada deriva saúde da ECU, frescor e status dos sensores a partir de UMA cópia do
// snapshot; ela não conhece ELMduino, Bluetooth nem WebServer. O adaptador para o WebServer
// fica em main.cpp, que apenas traduz HttpRequest/HttpResponse em chamadas do WebServer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "snapshot.h"

namespace onix {

enum class HttpMethod { Get, Head, Post, Put, Patch, Delete, Options, Other };

struct HttpRequest {
  HttpMethod method = HttpMethod::Get;
  std::string path;
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

// Base monotônica em milissegundos desde o boot (esp_timer no ESP32, steady_clock no host).
// É a mesma base de `uptimeMs`, `updatedAtMs`, `ageMs` e das janelas de saúde/frescor.
// A task OBD (obd.h) deve usar esta função para carimbar `updatedAtMs`.
uint64_t monotonicUptimeMs();

// Fonte de relógio injetável: os testes usam um relógio controlado (SPEC-0002 DoD).
using UptimeProvider = uint64_t (*)(void* context);

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
            UptimeProvider uptime = nullptr, void* uptimeContext = nullptr);

  // GET /           -> 200 text/html; charset=utf-8 + HTML do painel (dashboard.h)
  // GET /api/data   -> 200 application/json + Cache-Control: no-store
  // método != GET em rota conhecida -> 405, sem ler o snapshot
  // rota desconhecida -> 404, sem ler o snapshot
  HttpResponse handle(const HttpRequest& request) const;

  const SnapshotStore& store() const { return store_; }
  const TimingConfig& timing() const { return timing_; }

  // Mesmo JSON de GET /api/data, reutilizado pelo broadcast WebSocket (porta 81):
  // copia o snapshot, libera o mutex e só então serializa (ADR-0004). Público para não
  // duplicar serialização em main.cpp.
  std::string buildDataJson() const;

 private:
  const SnapshotStore& store_;
  const TimingConfig& timing_;
  const char* dashboardHtml_;
  UptimeProvider uptime_;
  void* uptimeContext_;
};

}  // namespace onix
