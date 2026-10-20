// config.h — configuração centralizada do firmware (SPEC-0001 §Configuração, ADR-0002).
// Sem números mágicos nos módulos: rede, HTTP, agendamento, frescor e recuperação vivem aqui.
// Cabeçalho portátil (compila no ESP32/Arduino e em testes nativos com g++).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "snapshot.h"

namespace onix {

// Janelas de tempo do agendamento OBD e da derivação de saúde/frescor da API (SPEC-0001, SPEC-0002).
// A estrutura não aplica política: quem aplica é a task OBD (agendamento) e a API (derivação).
struct TimingConfig {
  std::array<uint32_t, SENSOR_COUNT> intervalsMs{};  // intervalo pretendido de polling por sensor
  std::array<uint32_t, SENSOR_COUNT> freshnessMs{};  // prazo de frescor por sensor
  uint32_t ecuHealthMs = 0;                          // janela de saúde da ECU sem nova resposta
  uint32_t obdTimeoutMs = 0;                         // prazo de uma consulta OBD
  uint32_t recoveryInitialMs = 0;                    // espera inicial entre tentativas de recuperação
  uint32_t recoveryMaxMs = 0;                        // teto da espera de recuperação
};

constexpr std::array<uint32_t, SENSOR_COUNT> defaultIntervalsMs() {
  return {200,   // Rpm: ~5 Hz
          500,   // Speed: ~2 Hz
          1000,  // Coolant: ~1 Hz
          200,   // Throttle
          200,   // Map
          1000}; // Voltage
}

constexpr std::array<uint32_t, SENSOR_COUNT> defaultFreshnessMs() {
  return {3000,  // Rpm
          3000,  // Speed
          6000,  // Coolant
          3000,  // Throttle
          3000,  // Map
          6000}; // Voltage
}

// Invariantes dos padrões: freshnessMs > obdTimeoutMs + intervalsMs; ecuHealthMs > obdTimeoutMs;
// recoveryInitialMs < recoveryMaxMs. Enquanto uma consulta ainda pode concluir dentro de
// obdTimeoutMs, o sensor não é declarado vencido.
constexpr TimingConfig defaultTimingConfig() {
  return TimingConfig{defaultIntervalsMs(), defaultFreshnessMs(),
                      5000,  // ecuHealthMs
                      1500,  // obdTimeoutMs
                      1000,  // recoveryInitialMs
                      30000}; // recoveryMaxMs
}

// Rede local (SPEC-0002 §Rede e recursos). Origem única de SSID, senha e endereço.
constexpr char ONIX_AP_SSID[] = "Onix-MVP";
constexpr char ONIX_AP_PASSWORD[] = "onix1234";
constexpr uint8_t ONIX_AP_IP_OCTETS[4] = {192, 168, 4, 1};
constexpr uint8_t ONIX_AP_NETMASK_OCTETS[4] = {255, 255, 255, 0};
constexpr uint8_t ONIX_AP_CHANNEL = 1;
constexpr uint8_t ONIX_AP_MAX_CLIENTS = 4;
constexpr bool ONIX_AP_HIDDEN = false;
constexpr uint16_t ONIX_HTTP_PORT = 80;
constexpr uint16_t ONIX_WS_PORT = 81;
constexpr uint32_t ONIX_WS_BROADCAST_MS = 500;
constexpr uint32_t ONIX_SERIAL_BAUD = 115200;

// Cliente Wi-Fi no hotspot do celular (ADR-0012): credenciais na NVS via POST /api/wifi; sem
// conexão, o SoftAP acima sobe e a placa procura o hotspot periodicamente (só sem clientes no AP).
constexpr uint32_t ONIX_STA_CONNECT_TIMEOUT_MS = 15000;    // boot/scan: tenta a STA antes do AP
constexpr uint32_t ONIX_STA_RECONNECT_TIMEOUT_MS = 30000;  // STA caiu: espera antes de subir o AP
constexpr uint32_t ONIX_STA_SCAN_INTERVAL_MS = 20000;      // em AP sem clientes: busca o SSID
constexpr uint8_t ONIX_STA_STATIC_HOST_FIRST = 50;         // IP fixo .50–.59 na faixa do gateway
constexpr uint8_t ONIX_STA_STATIC_HOST_COUNT = 10;
constexpr uint32_t ONIX_STA_ARP_PROBE_MS = 300;            // espera da resposta ARP por candidato
constexpr uint32_t ONIX_WIFI_RESTART_DELAY_MS = 800;       // após POST/DELETE /api/wifi
constexpr char ONIX_STA_HOSTNAME[] = "onix";

// Atualização por Wi-Fi (ADR-0009): ArduinoOTA na rede do SoftAP. A senha precisa bater com
// `--auth` do env `ota` em platformio.ini; quem está no AP sem ela não grava firmware.
constexpr char ONIX_OTA_HOSTNAME[] = "onix";
constexpr char ONIX_OTA_PASSWORD[] = "onix-ota";
constexpr uint16_t ONIX_OTA_PORT = 3232;

// Logging (ADR-0011): buffer circular em RAM lido por GET /api/logs. 64 x ~140 B ≈ 9 KB fixos.
// ONIX_LOG_MIN_LEVEL: 0=error, 1=warn, 2=info, 3=debug (entradas acima são descartadas).
constexpr std::size_t ONIX_LOG_CAPACITY = 64;
constexpr std::size_t ONIX_LOG_MESSAGE_MAX = 120;
constexpr std::size_t ONIX_LOG_TAG_MAX = 8;
constexpr uint8_t ONIX_LOG_MIN_LEVEL = 2;
constexpr uint32_t ONIX_METRICS_INTERVAL_MS = 30000;  // heap/stack/uptime no log

// Histórico persistido no LittleFS (ADR-0018, SPEC-0004): só warn+ e o resumo de boot vão para o
// arquivo, então o volume é mínimo e o desgaste de flash fica viável. Cota fixa de 4 x 12 KB =
// 48 KB (~9% dos 512 KB do LittleFS, que ainda hospeda o bundle do painel). O flush do loop()
// grava o lote em blocos, nunca no caminho quente do log (ADR-0004/0011).
constexpr std::size_t ONIX_LOG_FILE_SEGMENT_BYTES = 12 * 1024;  // teto-alvo por segmento
constexpr std::size_t ONIX_LOG_FILE_SEGMENTS = 4;               // cota total (48 KB)
constexpr std::size_t ONIX_LOG_FILE_BATCH = 16;                 // linhas pendentes em RAM
constexpr uint32_t ONIX_LOG_FILE_FLUSH_MS = 60000;              // período do flush (loop())
constexpr std::size_t ONIX_LOG_FILE_MIN_FREE_BYTES = 64 * 1024; // piso de espaço livre no FS
constexpr char ONIX_LOG_FILE_PATH_PREFIX[] = "/logs.";

// Cabeçalhos do contrato HTTP (SPEC-0002 §Rede e recursos).
constexpr char ONIX_CONTENT_TYPE_HTML[] = "text/html; charset=utf-8";
constexpr char ONIX_CONTENT_TYPE_JSON[] = "application/json";
constexpr char ONIX_CONTENT_TYPE_TEXT[] = "text/plain; charset=utf-8";
constexpr char ONIX_CACHE_CONTROL_NO_STORE[] = "no-store";
constexpr char ONIX_HEADER_CACHE_CONTROL[] = "Cache-Control";

// Origem padrão da tensão (SPEC-0001: `AT RV`; PID 42 exige configuração explícita).
constexpr VoltageSource ONIX_DEFAULT_VOLTAGE_SOURCE = VoltageSource::Adapter;

// Identidade do scanner Bluetooth (SPEC-0001 §Configuração; ADR-0003 não presume nome nem PIN universais).
constexpr char ONIX_BT_DEVICE_NAME[] = "OBDII";
constexpr char ONIX_BT_DEVICE_PIN[] = "1234";
constexpr char ONIX_BT_DEVICE_MAC[] = "";  // vazio = conectar pelo nome; "AA:BB:CC:DD:EE:FF" quando conhecido
constexpr char ONIX_BT_LOCAL_NAME[] = "Onix-CB";
constexpr uint32_t ONIX_BT_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t ONIX_BT_INIT_TIMEOUT_MS = 30000;  // ADR-0004: busca automática de protocolo
// Busca BT curta (múltiplo de 1,28 s) só para achar o MAC pelo nome; depois conecta direto pelo MAC.
// A busca ocupa o rádio e derruba o SoftAP enquanto dura, por isso é curta e rara.
constexpr uint32_t ONIX_BT_DISCOVERY_MS = 5120;
// Busca do botão "Buscar" do painel: mais longa para listar mais dispositivos (8 x 1,28 s).
constexpr uint32_t ONIX_BT_PANEL_SCAN_MS = 10240;
constexpr uint8_t ONIX_BT_MAC_RETRIES = 3;  // falhas seguidas pelo MAC antes de buscar de novo
// Falhas seguidas de conexão BT antes de parar a busca (cada uma ocupa o rádio e derruba o Wi-Fi);
// a busca só volta com POST /api/obd/reconnect (painel) ou reboot.
constexpr uint8_t ONIX_BT_MAX_CONNECT_ATTEMPTS = 5;
// Sem tentar conectar ao scanner até este tempo depois do último arquivo estático servido: a busca
// BT ocupa o rádio e travava o envio do painel por ~5 s (o polling de /api/data e o WS não contam).
constexpr uint32_t ONIX_BT_HTTP_QUIET_MS = 10000;

// Protocolo do ELM327: '0' = busca automática; '6' = ISO 15765-4 CAN 11 bit 500 kbps (Onix 2019) pula a busca.
constexpr char ONIX_OBD_PROTOCOL = '0';

// Task única de aplicação para OBD (ADR-0004). Usados por main.cpp na criação da task.
constexpr uint32_t ONIX_OBD_TASK_STACK_SIZE = 8192;
constexpr uint32_t ONIX_OBD_TASK_PRIORITY = 1;
// OBD/BT ficam no núcleo 0 com as pilhas Wi-Fi/BT (prioridades maiores preemptam a task); o
// núcleo 1 fica só para o loop() (HTTP/WS/OTA). O watchdog do IDLE0 é desligado em main.cpp
// porque o ELMduino faz busy-wait sem yield na busca de protocolo.
constexpr uint32_t ONIX_OBD_TASK_CORE = 0;

}  // namespace onix
