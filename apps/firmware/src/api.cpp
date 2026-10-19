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
 out+=",\"btSearchStopped\":";out+=boolean(s.btSearchStopped);
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
void ApiRouter::setObdReconnectHandler(ActionHandler handler,void* context){obdReconnect_=handler;obdReconnectContext_=context;}
ApiRouter::ApiRouter(const SnapshotStore& s,const TimingConfig& t,const char* html,UptimeProvider u,void* ctx,const LogBuffer* logs,WifiConfigPort* wifi):store_(s),timing_(t),dashboardHtml_(html),uptime_(u),uptimeContext_(ctx),logs_(logs),wifi_(wifi){}
std::string ApiRouter::buildLogsJson()const {
 const auto now=uptime_ ? uptime_(uptimeContext_) : monotonicUptimeMs();
 // Sem buffer injetado, a rota responde com lista vazia (os testes e builds sem log usam isso).
 if(!logs_) return serializeLogsJson(std::vector<LogEntry>(),0,now);
 return serializeLogsJson(logs_->snapshot(),logs_->droppedCount(),now);
}
std::string ApiRouter::buildDataJson()const {
 const auto copy=store_.copy();
 const auto now=uptime_ ? uptime_(uptimeContext_) : monotonicUptimeMs();
 return serializeSnapshotJson(copy,timing_,now,now);
}
namespace {
// Respostas JSON de /api/wifi: sempre no-store, inclusive erros {"ok":false,"error":"..."}.
HttpResponse wifiJson(int status,const std::string& body){return {status,ONIX_CONTENT_TYPE_JSON,{{ONIX_HEADER_CACHE_CONTROL,ONIX_CACHE_CONTROL_NO_STORE}},body};}
HttpResponse wifiError(int status,const char* error){return wifiJson(status,std::string("{\"ok\":false,\"error\":\"")+error+"\"}");}
// GET/POST/DELETE /api/wifi (ADR-0012). A senha OTA é checada antes de validar credenciais.
HttpResponse handleWifi(const HttpRequest& req,WifiConfigPort& wifi){
 const char restarted[]="{\"ok\":true,\"restarting\":true}";
 if(req.method==HttpMethod::Get)return wifiJson(200,serializeWifiStatusJson(wifi.status()));
 if(req.method==HttpMethod::Post){
  const auto body=parseWifiRequestBody(req.body);
  if(!body)return wifiError(400,"invalid-json");
  if(body->otaPassword!=ONIX_OTA_PASSWORD)return wifiError(403,"forbidden");
  const WifiCredentials credentials{body->ssid,body->password};
  if(!isValidWifiCredentials(credentials))return wifiError(400,"invalid-credentials");
  if(!wifi.save(credentials))return wifiError(500,"storage");
  return wifiJson(200,restarted);
 }
 if(req.method==HttpMethod::Delete){
  const auto body=parseWifiRequestBody(req.body);
  if(!body)return wifiError(400,"invalid-json");
  if(body->otaPassword!=ONIX_OTA_PASSWORD)return wifiError(403,"forbidden");
  if(!wifi.clear())return wifiError(500,"storage");
  return wifiJson(200,restarted);
 }
 return {405,ONIX_CONTENT_TYPE_TEXT,{{"Allow","GET, POST, DELETE"}},"Method not allowed"};
}
}
HttpResponse ApiRouter::handle(const HttpRequest& req)const {
 HttpResponse response;
 // /api/obd/reconnect existe sempre: 405 para outros métodos, 503 sem handler, 202 com handler.
 if(req.path==API_ROUTE_OBD_RECONNECT){
  if(req.method!=HttpMethod::Post)return {405,ONIX_CONTENT_TYPE_TEXT,{{"Allow","POST"}},"Method not allowed"};
  if(!obdReconnect_)return {503,ONIX_CONTENT_TYPE_JSON,{{ONIX_HEADER_CACHE_CONTROL,ONIX_CACHE_CONTROL_NO_STORE}},"{\"ok\":false}"};
  obdReconnect_(obdReconnectContext_);
  return {202,ONIX_CONTENT_TYPE_JSON,{{ONIX_HEADER_CACHE_CONTROL,ONIX_CACHE_CONTROL_NO_STORE}},"{\"ok\":true}"};
 }
 // /api/wifi só existe quando há WifiConfigPort injetado; sem ele é rota desconhecida (404).
 const bool wifiRoute=wifi_ && req.path==API_ROUTE_WIFI;
 if(req.path!="/" && req.path!="/api/data" && req.path!="/api/logs" && !wifiRoute)return {404,ONIX_CONTENT_TYPE_TEXT,{},"Not found"};
 if(wifiRoute)return handleWifi(req,*wifi_);
 if(req.method!=HttpMethod::Get)return {405,ONIX_CONTENT_TYPE_TEXT,{{"Allow","GET"}},"Method not allowed"};
 if(req.path=="/")return {200,ONIX_CONTENT_TYPE_HTML,{},dashboardHtml_ ? dashboardHtml_ : ""};
 if(req.path=="/api/logs")return {200,ONIX_CONTENT_TYPE_JSON,{{ONIX_HEADER_CACHE_CONTROL,ONIX_CACHE_CONTROL_NO_STORE}},buildLogsJson()};
 return {200,ONIX_CONTENT_TYPE_JSON,{{ONIX_HEADER_CACHE_CONTROL,ONIX_CACHE_CONTROL_NO_STORE}},buildDataJson()};
}
}
