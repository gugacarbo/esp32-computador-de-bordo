// dashboard.h — painel offline do ESP32 (SPEC-0003, ADR-0005): HTML, CSS e JavaScript locais.
// Documento único em literal bruto: os testes Playwright extraem EXATAMENTE o que o firmware
// serve em GET / (web-tests/dashboard-source.ts) e validam estados, polling e layout.
// Restrições do literal: nenhum "//" em qualquer trecho (o padrão de recursos externos dos
// testes proíbe), nenhum ")HTML\"" e nenhuma dependência de rede — tudo embutido.
#pragma once

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#define ONIX_DASHBOARD_FLASH PROGMEM
#else
#define ONIX_DASHBOARD_FLASH
#endif

namespace onix {

static const char DASHBOARD_HTML[] ONIX_DASHBOARD_FLASH = R"HTML(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ONIX</title>
<style>
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:#0e1116;color:#f2f5f9;padding:12px;min-height:100vh}
header{display:flex;flex-direction:column;gap:2px;margin:4px 2px 10px}
header h1{font-size:1.5rem;letter-spacing:.08em;margin:0}
header p{margin:0;color:#b9c4cf;font-size:.9rem}
#panel-status{margin:0 0 12px;font-size:1rem;font-weight:600;padding:8px 10px;border-radius:8px;background:#1a2129;border-left:4px solid #8b98a5}
#panel-status[data-state="connected"]{border-left-color:#58d68d}
#panel-status[data-state="adapter"],#panel-status[data-state="ecu"]{border-left-color:#ffd166}
#panel-status[data-state="comm-error"]{border-left-color:#ff8fa3}
#grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
@media (min-width:720px){#grid{grid-template-columns:repeat(3,minmax(0,1fr))}}
.card{background:#1a2129;border:1px solid #2a3440;border-radius:10px;padding:10px 12px;display:flex;flex-direction:column;gap:6px;min-width:0}
.card h2{margin:0;font-size:.78rem;font-weight:600;text-transform:uppercase;letter-spacing:.04em;color:#b9c4cf;overflow-wrap:anywhere}
.card .reading{margin:0;font-size:clamp(1.5rem,9vw,2.1rem);line-height:1.1;font-variant-numeric:tabular-nums}
.card .reading .unit{font-size:.9rem;color:#b9c4cf;margin-left:4px}
.card .state{margin:0;font-size:.85rem;font-weight:600;color:#9fb0be;min-height:1.05em}
.card .note{margin:0;font-size:.78rem;color:#9fb0be;min-height:1em}
.card[data-status="ok"] .state{color:#6fe3a1}
.card[data-status="stale"] .state{color:#ffd166}
.card[data-status="timeout"] .state,.card[data-status="error"] .state{color:#ff8fa3}
</style>
</head>
<body>
<header>
<h1>ONIX</h1>
<p>Computador de bordo</p>
</header>
<p id="panel-status" data-state="loading" aria-live="polite">Carregando dados</p>
<main id="grid">
<section class="card" id="card-rpm" data-sensor="rpm" data-status="unknown">
<h2>RPM</h2>
<p class="reading"><span class="value" id="value-rpm">—</span><span class="unit">rpm</span></p>
<p class="state" id="state-rpm"></p>
<p class="note" id="note-rpm"></p>
</section>
<section class="card" id="card-speedKph" data-sensor="speedKph" data-status="unknown">
<h2>Velocidade</h2>
<p class="reading"><span class="value" id="value-speedKph">—</span><span class="unit">km/h</span></p>
<p class="state" id="state-speedKph"></p>
<p class="note" id="note-speedKph"></p>
</section>
<section class="card" id="card-coolantC" data-sensor="coolantC" data-status="unknown">
<h2>Temperatura do líquido de arrefecimento</h2>
<p class="reading"><span class="value" id="value-coolantC">—</span><span class="unit">°C</span></p>
<p class="state" id="state-coolantC"></p>
<p class="note" id="note-coolantC"></p>
</section>
<section class="card" id="card-throttlePercent" data-sensor="throttlePercent" data-status="unknown">
<h2>Borboleta</h2>
<p class="reading"><span class="value" id="value-throttlePercent">—</span><span class="unit">%</span></p>
<p class="state" id="state-throttlePercent"></p>
<p class="note" id="note-throttlePercent"></p>
</section>
<section class="card" id="card-mapKpa" data-sensor="mapKpa" data-status="unknown">
<h2>MAP</h2>
<p class="reading"><span class="value" id="value-mapKpa">—</span><span class="unit">kPa</span></p>
<p class="state" id="state-mapKpa"></p>
<p class="note" id="note-mapKpa"></p>
</section>
<section class="card" id="card-voltage" data-sensor="voltage" data-status="unknown">
<h2 id="label-voltage">Tensão</h2>
<p class="reading"><span class="value" id="value-voltage">—</span><span class="unit">V</span></p>
<p class="state" id="state-voltage"></p>
<p class="note" id="note-voltage"></p>
</section>
</main>
<script>
(function(){
"use strict";
var POLL_MS=500;
var TIMEOUT_MS=4000;
var UNITS={rpm:"rpm",speedKph:"km/h",coolantC:"°C",throttlePercent:"%",mapKpa:"kPa",voltage:"V"};
var KEYS=["rpm","speedKph","coolantC","throttlePercent","mapKpa","voltage"];
var STATUSES=["unavailable","unsupported","timeout","error","stale","ok"];
var last=null;
var busy=false;
var timer=null;
function el(id){return document.getElementById(id)}
function formatValue(key,value){return key==="voltage"?value.toFixed(1):String(Math.round(value))}
function setPanel(state,text){var p=el("panel-status");p.setAttribute("data-state",state);p.textContent=text}
function renderSensor(key,s){
var card=el("card-"+key);
var value="—";
var state="";
var note="";
var okReading=s.valid&&s.status==="ok";
if(okReading){value=formatValue(key,s.value)}
else if(s.status==="unsupported"){state="Não suportado"}
else if(s.status==="stale"){state="Desatualizado"}
else if(s.status==="timeout"||s.status==="error"){state="Falha na leitura"}
else{state="Sem leitura"}
if(!okReading&&typeof s.value==="number"){note="Leitura anterior: "+formatValue(key,s.value)+" "+UNITS[key]}
card.setAttribute("data-status",okReading?"ok":s.status);
el("value-"+key).textContent=value;
el("state-"+key).textContent=state;
el("note-"+key).textContent=note;
}
function applyEnvelope(data){
last=data;
if(!data.btConnected){setPanel("disconnected","OBD desconectado")}
else if(!data.adapterReady){setPanel("adapter","Inicializando adaptador")}
else if(!data.ecuResponding){setPanel("ecu","ECU sem resposta")}
else{setPanel("connected","OBD conectado")}
el("label-voltage").textContent=data.voltage.source==="ecu"?"Tensão da ECU":"Tensão do adaptador";
for(var i=0;i<KEYS.length;i++){renderSensor(KEYS[i],data[KEYS[i]])}
}
function onCommFailure(){
setPanel("comm-error","Sem comunicação com o ESP32");
for(var i=0;i<KEYS.length;i++){
var key=KEYS[i];
el("card-"+key).setAttribute("data-status","offline");
el("value-"+key).textContent="—";
el("state-"+key).textContent="Sem dados atuais";
var previous=last&&last[key]&&typeof last[key].value==="number"?formatValue(key,last[key].value):null;
el("note-"+key).textContent=previous?"Leitura anterior: "+previous+" "+UNITS[key]:"";
}
}
function isSensor(s){
if(!s||typeof s!=="object"){return false}
if(s.value!==null&&!(typeof s.value==="number"&&isFinite(s.value))){return false}
if(typeof s.valid!=="boolean"||typeof s.status!=="string"){return false}
if(STATUSES.indexOf(s.status)<0){return false}
if(s.valid!==(s.status==="ok")){return false}
if(s.updatedAtMs!==null&&typeof s.updatedAtMs!=="number"){return false}
if(s.ageMs!==null&&typeof s.ageMs!=="number"){return false}
return true;
}
function envelopeValid(d){
if(!d||typeof d!=="object"){return false}
if(typeof d.uptimeMs!=="number"){return false}
if(typeof d.btConnected!=="boolean"||typeof d.adapterReady!=="boolean"||typeof d.ecuResponding!=="boolean"||typeof d.obdConnected!=="boolean"){return false}
for(var i=0;i<KEYS.length;i++){if(!isSensor(d[KEYS[i]])){return false}}
if(d.voltage.source!=="adapter"&&d.voltage.source!=="ecu"){return false}
return true;
}
function schedule(delay){
if(timer!==null){clearTimeout(timer)}
timer=setTimeout(tick,delay);
}
function tick(){
timer=null;
if(busy||document.visibilityState==="hidden"){return}
busy=true;
var ctrl=new AbortController();
var timeoutHandle=setTimeout(function(){ctrl.abort()},TIMEOUT_MS);
fetch("/api/data",{cache:"no-store",signal:ctrl.signal}).then(function(res){
if(!res.ok){throw new Error("http")}
return res.text();
}).then(function(text){
var data;
try{data=JSON.parse(text)}catch(e){throw new Error("json")}
if(!envelopeValid(data)){throw new Error("shape")}
applyEnvelope(data);
}).catch(function(){onCommFailure()}).then(function(){
clearTimeout(timeoutHandle);
busy=false;
schedule(POLL_MS);
});
}
document.addEventListener("visibilitychange",function(){
if(document.visibilityState==="visible"&&!busy&&timer===null){schedule(0)}
});
schedule(0);
})();
</script>
</body>
</html>)HTML";

}  // namespace onix

#undef ONIX_DASHBOARD_FLASH
