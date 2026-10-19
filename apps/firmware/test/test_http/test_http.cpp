#include "api.h"
#include "static_asset_policy.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
using namespace onix;
static uint64_t clockNow(void* ctx) { return *static_cast<uint64_t*>(ctx); }
int main() {
  assert(onix::acceptsGzipEncoding("gzip, deflate, br"));
  assert(onix::acceptsGzipEncoding("br, GZIP;q=0.5"));
  assert(!onix::acceptsGzipEncoding("gzip;q=0, br"));
  assert(!onix::acceptsGzipEncoding("gzip;q=0.000"));
  assert(!onix::acceptsGzipEncoding("br, deflate"));
  assert(onix::selectAssetPath("/assets/app-Ab12cd.js", true, true) ==
         "/assets/app-Ab12cd.js.gz");
  assert(onix::selectAssetPath("/assets/app-Ab12cd.js", false, true) ==
         "/assets/app-Ab12cd.js");
  assert(onix::selectAssetPath("/assets/app-Ab12cd.js", true, false) ==
         "/assets/app-Ab12cd.js");
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
  for (auto key : {"rpm", "speedKph", "coolantC", "throttlePercent", "mapKpa", "voltage"})
    assert(offline.body.find(std::string("\"") + key + "\":") != std::string::npos);
  assert(api.handle({HttpMethod::Post, "/api/data"}).status == 405);
  assert(api.handle({HttpMethod::Head, "/"}).status == 405);
  assert(api.handle({HttpMethod::Post, "/missing"}).status == 404);
  assert(api.handle({HttpMethod::Get, "/"}).body == "<html>ONIX</html>");
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
  std::cout << "API and snapshot behavioral tests passed\n";
}
