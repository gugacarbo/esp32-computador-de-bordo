#include "api.h"
#include "static_asset_policy.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <optional>
using namespace onix;
static uint64_t clockNow(void* ctx) { return *static_cast<uint64_t*>(ctx); }

// Fake de WifiConfigPort: grava o que foi salvo e permite forçar falhas de NVS.
class FakeWifiPort : public WifiConfigPort {
 public:
  WifiStatus status() const override { return statusValue; }
  bool save(const WifiCredentials& credentials) override {
    ++saveCount;
    saved = credentials;
    return saveResult;
  }
  bool clear() override {
    ++clearCount;
    return clearResult;
  }

  WifiStatus statusValue;
  WifiCredentials saved;
  int saveCount = 0;
  int clearCount = 0;
  bool saveResult = true;
  bool clearResult = true;
};

int main() {
  assert(onix::selectAssetPath("/assets/app-Ab12cd.js", true) == "/assets/app-Ab12cd.js.gz");
  assert(onix::selectAssetPath("/index.html", false) == "/index.html");
  assert(onix::cacheControlForAsset("/assets/app-Ab12cd.js") ==
         std::string("public, max-age=31536000, immutable"));
  assert(onix::cacheControlForAsset("/index.html") == std::string("no-cache"));
  auto timing = defaultTimingConfig();
  SnapshotStore store;
  uint64_t now = 10000;
  ApiRouter api(store, timing, "<html>ONIX</html>", clockNow, &now);
  auto offline = api.handle({HttpMethod::Get, "/api/data"});
  assert(offline.status == 200);
  assert(offline.contentType == "application/json");
  assert(offline.headerValue("Cache-Control") == "no-store");
  assert(offline.body.find("\"value\":null") != std::string::npos);
  // Envelope: btSearchStopped logo após obdConnected (contrato com o painel).
  assert(offline.body.find("\"obdConnected\":false,\"btSearchStopped\":false") != std::string::npos);
  for (auto key : {"rpm", "speedKph", "coolantC", "throttlePercent", "mapKpa", "voltage"})
    assert(offline.body.find(std::string("\"") + key + "\":") != std::string::npos);
  assert(api.handle({HttpMethod::Post, "/api/data"}).status == 405);
  assert(api.handle({HttpMethod::Head, "/"}).status == 405);
  assert(api.handle({HttpMethod::Post, "/missing"}).status == 404);
  assert(api.handle({HttpMethod::Get, "/"}).body == "<html>ONIX</html>");
  // GET /api/logs sem LogBuffer injetado: lista vazia, mesmo contrato de /api/data.
  auto emptyLogs = api.handle({HttpMethod::Get, "/api/logs"});
  assert(emptyLogs.status == 200);
  assert(emptyLogs.contentType == "application/json");
  assert(emptyLogs.headerValue("Cache-Control") == "no-store");
  assert(emptyLogs.body == "{\"uptimeMs\":10000,\"dropped\":0,\"entries\":[]}");
  assert(api.handle({HttpMethod::Post, "/api/logs"}).status == 405);
  assert(api.handle({HttpMethod::Post, "/api/logs"}).headerValue("Allow") == "GET");
  assert(api.handle({HttpMethod::Get, "/api/data"}).status == 200);
  // Com LogBuffer injetado, a entrada gravada aparece no corpo JSON.
  onix::LogBuffer logs;
  logs.append(10000, onix::LogLevel::Warn, "bt", "falha");
  ApiRouter apiWithLogs(store, timing, "<html>ONIX</html>", clockNow, &now, &logs);
  auto loggedBody = apiWithLogs.handle({HttpMethod::Get, "/api/logs"}).body;
  assert(loggedBody.find("\"level\":\"warn\",\"tag\":\"bt\",\"message\":\"falha\"") !=
         std::string::npos);
  Snapshot s;
  s.btConnected = s.adapterReady = s.hasEcuResponse = true;
  s.lastEcuResponseMs = now;
  auto& rpm = s.sensors[sensorIndex(SensorId::Rpm)];
  rpm = {0, true, now, SensorStatus::Ok, false};
  store.publish(s);
  assert(isSensorValid(s, SensorId::Rpm, timing, now));
  auto json = api.handle({HttpMethod::Get, "/api/data"}).body;
  assert(json.find("\"rpm\":{\"value\":0,\"valid\":true") != std::string::npos);
  now += 3001;
  assert(resolveSensorStatus(s, SensorId::Rpm, timing, now) == SensorStatus::Stale);
  assert(api.handle({HttpMethod::Get, "/api/data"}).body.find("\"status\":\"stale\"") != std::string::npos);
  rpm.lastStatus = SensorStatus::Timeout;
  assert(resolveSensorStatus(s, SensorId::Rpm, timing, now) == SensorStatus::Timeout);
  rpm.lastStatus = SensorStatus::Error;
  assert(resolveSensorStatus(s, SensorId::Rpm, timing, now) == SensorStatus::Error);
  rpm.unsupported = true;
  s.btConnected = false;
  assert(resolveSensorStatus(s, SensorId::Rpm, timing, now) == SensorStatus::Unsupported);
  rpm.unsupported = false;
  assert(resolveSensorStatus(s, SensorId::Rpm, timing, now) == SensorStatus::Unavailable);
  s.btConnected = true;
  now += 2000;
  auto& voltage = s.sensors[sensorIndex(SensorId::Voltage)];
  voltage = {14.1, true, now, SensorStatus::Ok, false};
  assert(!deriveSessionPhases(s, timing, now).ecuResponding);
  assert(deriveSessionPhases(s, timing, now).adapterReady);
  assert(isSensorValid(s, SensorId::Voltage, timing, now));
  s.voltageSource = VoltageSource::Ecu;
  assert(!isSensorValid(s, SensorId::Voltage, timing, now));
  assert(formatJsonNumber(NAN) == "null");
  assert(formatJsonNumber(INFINITY) == "null");
  now = (uint64_t{1} << 32) + 100;
  s.lastEcuResponseMs = now;
  rpm = {900, true, now - 50, SensorStatus::Ok, false};
  assert(sensorAgeMs(rpm, now) == 50);
  assert(isSensorValid(s, SensorId::Rpm, timing, now));
  std::atomic<bool> done{false};
  Snapshot baseline;
  baseline.lastEcuResponseMs = 1;
  for (auto& r : baseline.sensors) r.updatedAtMs = 1;
  store.publish(baseline);
  std::thread producer([&] {
    for (uint64_t i = 2; i < 10000; ++i) {
      Snapshot v; v.lastEcuResponseMs = i;
      for (auto& r : v.sensors) r.updatedAtMs = i;
      store.publish(v);
    }
    done = true;
  });
  while (!done) {
    // SPEC-0002 caso 7: a cópia é coerente entre si (sem campos parcialmente atualizados).
    // Não exigir igualdade com lastEcuResponseMs: o leitor pode ver um snapshot anterior
    // ainda coerente enquanto o produtor avança.
    auto copy = store.copy();
    const uint64_t first = copy.sensors[0].updatedAtMs;
    for (auto& r : copy.sensors) assert(r.updatedAtMs == first);
  }
  producer.join();
  // /api/wifi sem WifiConfigPort injetado é rota desconhecida (ADR-0012).
  assert(api.handle({HttpMethod::Get, "/api/wifi"}).status == 404);

  // /api/wifi com fake: GET, POST e DELETE (SPEC do contrato em api.h).
  FakeWifiPort wifi;
  wifi.statusValue = {"station", true, "Casa \"X\"", "192.168.43.50"};
  ApiRouter apiWifi(store, timing, "<html>ONIX</html>", clockNow, &now, nullptr, &wifi);
  const std::string restarted = R"JSON({"ok":true,"restarting":true})JSON";
  const std::string storageError = R"JSON({"ok":false,"error":"storage"})JSON";
  auto wifiPost = [&](const std::string& body) {
    return apiWifi.handle({HttpMethod::Post, "/api/wifi", body});
  };
  auto wifiDelete = [&](const std::string& body) {
    return apiWifi.handle({HttpMethod::Delete, "/api/wifi", body});
  };

  auto status = apiWifi.handle({HttpMethod::Get, "/api/wifi"});
  assert(status.status == 200);
  assert(status.contentType == "application/json");
  assert(status.headerValue("Cache-Control") == "no-store");
  assert(status.body ==
         "{\"mode\":\"station\",\"configured\":true,\"ssid\":\"Casa \\\"X\\\"\",\"ip\":\"192.168.43.50\"}");

  // POST com sucesso grava exatamente ssid/senha e responde 200 (saveCount: 1).
  auto saved = wifiPost(R"JSON({"ssid":"Casa","password":"senha1234","otaPassword":"onix-ota"})JSON");
  assert(saved.status == 200);
  assert(saved.contentType == "application/json");
  assert(saved.headerValue("Cache-Control") == "no-store");
  assert(saved.body == restarted);
  assert(wifi.saveCount == 1 && wifi.saved.ssid == "Casa" && wifi.saved.password == "senha1234");

  // Senha OTA errada: 403 e nada gravado; a checagem vem antes de validar credenciais.
  auto forbidden = wifiPost(R"JSON({"ssid":"Outra","password":"senha1234","otaPassword":"errada"})JSON");
  assert(forbidden.status == 403);
  assert(forbidden.body == R"JSON({"ok":false,"error":"forbidden"})JSON");
  assert(forbidden.headerValue("Cache-Control") == "no-store");
  assert(wifiPost(R"JSON({"ssid":"","otaPassword":"errada"})JSON").status == 403);
  assert(wifi.saveCount == 1);

  // JSON inválido: 400 (corpo vazio e objeto truncado).
  auto badJson = wifiPost(R"JSON({"ssid":"Casa",)JSON");
  assert(badJson.status == 400);
  assert(badJson.body == R"JSON({"ok":false,"error":"invalid-json"})JSON");
  assert(wifiPost("").status == 400);
  assert(wifi.saveCount == 1);

  // SSID vazio e senha de 7 bytes: 400 invalid-credentials, nada gravado.
  auto emptySsid = wifiPost(R"JSON({"ssid":"","password":"senha1234","otaPassword":"onix-ota"})JSON");
  assert(emptySsid.status == 400);
  assert(emptySsid.body == R"JSON({"ok":false,"error":"invalid-credentials"})JSON");
  assert(wifiPost(R"JSON({"ssid":"Casa","password":"1234567","otaPassword":"onix-ota"})JSON").status == 400);
  assert(wifi.saveCount == 1);

  // Senha vazia (rede aberta) é válida (saveCount: 2).
  auto open = wifiPost(R"JSON({"ssid":"Aberta","password":"","otaPassword":"onix-ota"})JSON");
  assert(open.status == 200);
  assert(wifi.saveCount == 2 && wifi.saved.ssid == "Aberta" && wifi.saved.password.empty());

  // Falha na NVS: 500 storage (saveCount: 3).
  wifi.saveResult = false;
  auto storageFail = wifiPost(R"JSON({"ssid":"Casa","password":"senha1234","otaPassword":"onix-ota"})JSON");
  assert(storageFail.status == 500);
  assert(storageFail.body == storageError);
  assert(storageFail.headerValue("Cache-Control") == "no-store");
  assert(wifi.saveCount == 3);
  wifi.saveResult = true;

  // DELETE: sucesso chama clear(); senha errada não limpa; JSON inválido 400; falha 500.
  auto cleared = wifiDelete(R"JSON({"otaPassword":"onix-ota"})JSON");
  assert(cleared.status == 200);
  assert(cleared.body == restarted);
  assert(cleared.headerValue("Cache-Control") == "no-store");
  assert(wifi.clearCount == 1);
  auto clearForbidden = wifiDelete(R"JSON({"otaPassword":"errada"})JSON");
  assert(clearForbidden.status == 403);
  assert(clearForbidden.body == R"JSON({"ok":false,"error":"forbidden"})JSON");
  assert(wifi.clearCount == 1);
  auto clearBad = wifiDelete("nao-json");
  assert(clearBad.status == 400);
  assert(clearBad.body == R"JSON({"ok":false,"error":"invalid-json"})JSON");
  wifi.clearResult = false;
  auto clearFail = wifiDelete(R"JSON({"otaPassword":"onix-ota"})JSON");
  assert(clearFail.status == 500);
  assert(clearFail.body == storageError);
  assert(clearFail.headerValue("Cache-Control") == "no-store");
  assert(wifi.clearCount == 2);
  wifi.clearResult = true;

  // Outros métodos em /api/wifi: 405 com Allow completo.
  auto put = apiWifi.handle({HttpMethod::Put, "/api/wifi", R"JSON({})JSON"});
  assert(put.status == 405);
  assert(put.headerValue("Allow") == "GET, POST, DELETE");
  assert(apiWifi.handle({HttpMethod::Head, "/api/wifi"}).status == 405);
  assert(wifi.saveCount == 3 && wifi.clearCount == 2);

  // Regras de credenciais: limites de 32 bytes no SSID e 8..63 na senha.
  assert(isValidWifiCredentials({std::string(32, 's'), ""}));
  assert(!isValidWifiCredentials({std::string(33, 's'), ""}));
  assert(isValidWifiCredentials({"Casa", std::string(8, 'p')}));
  assert(isValidWifiCredentials({"Casa", std::string(63, 'p')}));
  assert(!isValidWifiCredentials({"Casa", std::string(64, 'p')}));

  // Serialização do status: controle < 0x20 vira \u00XX.
  WifiStatus odd{"connecting", false, std::string("a\x01" "b"), ""};
  assert(serializeWifiStatusJson(odd) ==
         "{\"mode\":\"connecting\",\"configured\":false,\"ssid\":\"a\\u0001b\",\"ip\":\"\"}");

  // parseWifiRequestBody: escapes JSON e \uXXXX viram UTF-8.
  auto escaped = parseWifiRequestBody(R"JSON({"ssid":"a\"b\\c\/d\né","password":"éé"})JSON");
  assert(escaped.has_value());
  assert(escaped->ssid == std::string("a\"b\\c/d\n\xC3\xA9"));
  assert(escaped->password == std::string("\xC3\xA9\xC3\xA9"));

  // Chaves desconhecidas são ignoradas, duplicadas valem a última, espaços são aceitos.
  auto lenient = parseWifiRequestBody(R"JSON( { "foo" : "bar", "ssid":"X", "otaPassword":"y", "ssid":"Z" } )JSON");
  assert(lenient.has_value());
  assert(lenient->ssid == "Z" && lenient->otaPassword == "y" && lenient->password.empty());
  assert(parseWifiRequestBody("  {\n \"ssid\" : \"X\" }\r\n").has_value());
  assert(parseWifiRequestBody("{}").has_value());

  // Entradas recusadas: lixo depois do objeto, valores não-string, corpo vazio, sem chaves.
  assert(!parseWifiRequestBody(R"JSON({"ssid":"X"} x)JSON").has_value());
  assert(!parseWifiRequestBody(R"JSON({"ssid":12})JSON").has_value());
  assert(!parseWifiRequestBody(R"JSON({"ssid":true})JSON").has_value());
  assert(!parseWifiRequestBody("").has_value());
  assert(!parseWifiRequestBody("   ").has_value());
  assert(!parseWifiRequestBody(R"JSON({"ssid":"X",})JSON").has_value());

  // POST /api/obd/reconnect: 503 sem handler; 202 com handler (chamado uma vez); GET -> 405.
  ApiRouter apiRecon(store, timing, "<html>ONIX</html>", clockNow, &now);
  auto noHandler = apiRecon.handle({HttpMethod::Post, API_ROUTE_OBD_RECONNECT});
  assert(noHandler.status == 503);
  assert(noHandler.contentType == "application/json");
  assert(noHandler.body == "{\"ok\":false}");
  int reconnectCalls = 0;
  apiRecon.setObdReconnectHandler([](void* context) { ++*static_cast<int*>(context); },
                                  &reconnectCalls);
  auto accepted = apiRecon.handle({HttpMethod::Post, API_ROUTE_OBD_RECONNECT});
  assert(accepted.status == 202);
  assert(accepted.contentType == "application/json");
  assert(accepted.headerValue("Cache-Control") == "no-store");
  assert(accepted.body == "{\"ok\":true}");
  assert(reconnectCalls == 1);
  auto getReconnect = apiRecon.handle({HttpMethod::Get, API_ROUTE_OBD_RECONNECT});
  assert(getReconnect.status == 405);
  assert(getReconnect.headerValue("Allow") == "POST");
  assert(reconnectCalls == 1);
  assert(apiRecon.handle({HttpMethod::Get, "/api/data"}).status == 200);

  std::cout << "API and snapshot behavioral tests passed\n";
}
