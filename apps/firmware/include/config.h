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
constexpr uint32_t ONIX_SERIAL_BAUD = 115200;

// Cabeçalhos do contrato HTTP (SPEC-0002 §Rede e recursos).
constexpr char ONIX_CONTENT_TYPE_HTML[] = "text/html; charset=utf-8";
constexpr char ONIX_CONTENT_TYPE_JSON[] = "application/json";
constexpr char ONIX_CONTENT_TYPE_TEXT[] = "text/plain; charset=utf-8";
constexpr char ONIX_CACHE_CONTROL_NO_STORE[] = "no-store";
constexpr char ONIX_HEADER_CACHE_CONTROL[] = "Cache-Control";

// Origem padrão da tensão (SPEC-0001: `AT RV`; PID 42 exige configuração explícita).
constexpr VoltageSource ONIX_DEFAULT_VOLTAGE_SOURCE = VoltageSource::Adapter;

// Task única de aplicação para OBD (ADR-0004). Valores apenas informativos: a task é criada em main.cpp.
constexpr uint32_t ONIX_OBD_TASK_STACK_SIZE = 8192;
constexpr uint32_t ONIX_OBD_TASK_PRIORITY = 1;

}  // namespace onix
