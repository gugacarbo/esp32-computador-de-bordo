#include "api.h"
#include <cmath>
#include <cstdio>
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "esp_timer.h"
#else
#include <chrono>
#endif
namespace onix {
uint64_t monotonicUptimeMs() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
 return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
#else
 static const auto boot = std::chrono::steady_clock::now();
 return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-boot).count();
#endif
}
const char* sensorJsonKey(SensorId id) {
 static const char* keys[] = {"rpm","speedKph","coolantC","throttlePercent","mapKpa","voltage"};
 return keys[sensorIndex(id)];
}
const char* sensorStatusName(SensorStatus s) {
 switch(s) {
 case SensorStatus::Unavailable:return "unavailable";
 case SensorStatus::Unsupported:return "unsupported";
 case SensorStatus::Timeout:return "timeout";
 case SensorStatus::Error:return "error";
 case SensorStatus::Stale:return "stale";
 case SensorStatus::Ok:return "ok";
 }
 return "error";
}
const char* voltageSourceName(VoltageSource s) { return s == VoltageSource::Adapter ? "adapter" : "ecu"; }
SessionPhases deriveSessionPhases(const Snapshot& s,const TimingConfig& t,uint64_t now) {
 SessionPhases p;
 p.btConnected=s.btConnected;
 p.adapterReady=p.btConnected && s.adapterReady;
 p.ecuResponding=p.adapterReady && s.hasEcuResponse && now>=s.lastEcuResponseMs && now-s.lastEcuResponseMs<=t.ecuHealthMs;
 p.obdConnected=p.btConnected && p.adapterReady && p.ecuResponding;
 return p;
}
uint64_t sensorAgeMs(const SensorReading& r,uint64_t now) { return now>=r.updatedAtMs ? now-r.updatedAtMs : 0; }
SensorStatus resolveSensorStatus(const Snapshot& s,SensorId id,const TimingConfig& t,uint64_t now) {
 const auto& r=s.sensors[sensorIndex(id)];
 if(r.unsupported) return SensorStatus::Unsupported;
 auto phases=deriveSessionPhases(s,t,now);
 const bool origin=(id==SensorId::Voltage && s.voltageSource==VoltageSource::Adapter) ? phases.adapterReady : phases.ecuResponding;
 if(!origin) return SensorStatus::Unavailable;
 if(r.lastStatus==SensorStatus::Timeout || r.lastStatus==SensorStatus::Error) return r.lastStatus;
 if(!r.hasValue) return SensorStatus::Unavailable;
 if(!std::isfinite(r.value)) return SensorStatus::Error;
 if(now<r.updatedAtMs || sensorAgeMs(r,now)>t.freshnessMs[sensorIndex(id)]) return SensorStatus::Stale;
 return SensorStatus::Ok;
}
bool isSensorValid(const Snapshot& s,SensorId id,const TimingConfig& t,uint64_t now) { return resolveSensorStatus(s,id,t,now)==SensorStatus::Ok; }
std::string formatJsonNumber(double v) {
 if(!std::isfinite(v)) return "null";
 char buffer[48]; std::snprintf(buffer,sizeof(buffer),"%.10g",v);
 return buffer;
}
std::string serializeSnapshotJson(const Snapshot& s,const TimingConfig& t,uint64_t uptime,uint64_t now) {
 const auto p=deriveSessionPhases(s,t,now);
 auto boolean=[](bool v){return v ? "true" : "false";};
 std::string out="{\"uptimeMs\":"+std::to_string(uptime);
 out+=",\"btConnected\":";out+=boolean(p.btConnected);
 out+=",\"adapterReady\":";out+=boolean(p.adapterReady);
 out+=",\"ecuResponding\":";out+=boolean(p.ecuResponding);
 out+=",\"obdConnected\":";out+=boolean(p.obdConnected);
 for(size_t i=0;i<SENSOR_COUNT;++i) {
  auto id=static_cast<SensorId>(i); const auto& r=s.sensors[i];
  const auto status=resolveSensorStatus(s,id,t,now);
  out+=",\"";out+=sensorJsonKey(id);out+="\":{\"value\":";
  out+=r.hasValue ? formatJsonNumber(r.value) : "null";
  out+=",\"valid\":";out+=boolean(status==SensorStatus::Ok);
  out+=",\"status\":\"";out+=sensorStatusName(status);out+="\",\"updatedAtMs\":";
  out+=r.hasValue ? std::to_string(r.updatedAtMs) : "null";
  out+=",\"ageMs\":";out+=r.hasValue ? std::to_string(sensorAgeMs(r,now)) : "null";
  if(id==SensorId::Voltage){out+=",\"source\":\"";out+=voltageSourceName(s.voltageSource);out+='"';}
  out+='}';
 }
 return out+'}';
}
bool HttpResponse::hasHeader(const std::string& name)const {for(const auto& h:headers)if(h.name==name)return true;return false;}
std::string HttpResponse::headerValue(const std::string& name)const {for(const auto& h:headers)if(h.name==name)return h.value;return {};}
ApiRouter::ApiRouter(const SnapshotStore& s,const TimingConfig& t,const char* html,UptimeProvider u,void* ctx):store_(s),timing_(t),dashboardHtml_(html),uptime_(u),uptimeContext_(ctx){}
std::string ApiRouter::buildDataJson()const {
 const auto copy=store_.copy();
 const auto now=uptime_ ? uptime_(uptimeContext_) : monotonicUptimeMs();
 return serializeSnapshotJson(copy,timing_,now,now);
}
HttpResponse ApiRouter::handle(const HttpRequest& req)const {
 HttpResponse response;
 if(req.path!="/" && req.path!="/api/data")return {404,ONIX_CONTENT_TYPE_TEXT,{},"Not found"};
 if(req.method!=HttpMethod::Get)return {405,ONIX_CONTENT_TYPE_TEXT,{{"Allow","GET"}},"Method not allowed"};
 if(req.path=="/")return {200,ONIX_CONTENT_TYPE_HTML,{},dashboardHtml_ ? dashboardHtml_ : ""};
 return {200,ONIX_CONTENT_TYPE_JSON,{{ONIX_HEADER_CACHE_CONTROL,ONIX_CACHE_CONTROL_NO_STORE}},buildDataJson()};
}
}
