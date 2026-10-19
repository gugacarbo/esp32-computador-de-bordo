// test_wifi.cpp — testes Unity da política de Wi-Fi (ADR-0012): transições STA/AP, scan e IP fixo.
// Lógica pura (wifi_policy.h): nenhum include Arduino, roda no env native.
#include <cstdint>
#include <optional>
#include <vector>

#include "unity.h"
#include "wifi_policy.h"

namespace {

using onix::Ipv4;
using onix::WifiAction;
using onix::WifiMode;
using onix::WifiPolicy;
using onix::WifiTimings;

constexpr WifiTimings kTimings{15000, 30000, 20000};

// Estado do sondador de ARP falso: endereços "em uso" e número de chamadas.
struct ProbeState {
  std::vector<Ipv4> used;
  int calls = 0;
};

bool probeFake(const Ipv4& address, void* context) {
  auto* state = static_cast<ProbeState*>(context);
  ++state->calls;
  for (const Ipv4& other : state->used) {
    if (other == address) {
      return true;
    }
  }
  return false;
}

void test_begin_sem_credencial_vai_ao_ap_e_nunca_procura() {
  WifiPolicy policy(kTimings);
  TEST_ASSERT_TRUE(policy.begin(false, 0) == WifiAction::StartAccessPoint);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::AccessPoint);

  // Mesmo muito depois, sem credencial não há scan.
  TEST_ASSERT_TRUE(policy.tick(10000000, false, 0, false) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(20000000, false, 0, false) == WifiAction::None);
}

void test_begin_com_credencial_inicia_station() {
  WifiPolicy policy(kTimings);
  TEST_ASSERT_TRUE(policy.begin(true, 0) == WifiAction::StartStation);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::Connecting);
}

void test_conectar_vai_a_station_up() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  TEST_ASSERT_TRUE(policy.tick(1000, true, 0, false) == WifiAction::StationUp);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::Station);
  TEST_ASSERT_TRUE(policy.tick(2000, true, 0, false) == WifiAction::None);
}

void test_timeout_de_conexao_em_15000_sobe_ap() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 1000);
  TEST_ASSERT_TRUE(policy.tick(15999, false, 0, false) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(16000, false, 0, false) == WifiAction::StartAccessPoint);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::AccessPoint);
}

void test_sem_scan_antes_do_intervalo() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  // Timeout em 15000 entra no AP e marca o último scan nesse instante.
  TEST_ASSERT_TRUE(policy.tick(15000, false, 0, false) == WifiAction::StartAccessPoint);
  TEST_ASSERT_TRUE(policy.tick(34999, false, 0, false) == WifiAction::None);
}

void test_scan_no_intervalo_sem_clientes() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(15000, false, 0, false);
  TEST_ASSERT_TRUE(policy.tick(35000, false, 0, false) == WifiAction::StartScan);
  // Após o StartScan, o próximo só vem um intervalo depois.
  TEST_ASSERT_TRUE(policy.tick(54999, false, 0, false) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(55000, false, 0, false) == WifiAction::StartScan);
}

void test_sem_scan_com_clientes_no_ap() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(15000, false, 0, false);
  TEST_ASSERT_TRUE(policy.tick(40000, false, 1, false) == WifiAction::None);
}

void test_sem_scan_com_scan_em_curso() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(15000, false, 0, false);
  TEST_ASSERT_TRUE(policy.tick(40000, false, 0, true) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(40001, false, 0, false) == WifiAction::StartScan);
}

void test_scan_encontrado_inicia_station() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(15000, false, 0, false);
  policy.tick(35000, false, 0, false);
  TEST_ASSERT_TRUE(policy.onScanResult(35100, true, 0) == WifiAction::StartStation);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::Connecting);
  TEST_ASSERT_TRUE(policy.tick(35200, true, 0, false) == WifiAction::StationUp);
}

void test_scan_nao_encontrado_nao_faz_nada() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(15000, false, 0, false);
  policy.tick(35000, false, 0, false);
  TEST_ASSERT_TRUE(policy.onScanResult(35100, false, 0) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::AccessPoint);
}

void test_scan_encontrado_com_clientes_nao_troca() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(15000, false, 0, false);
  policy.tick(35000, false, 0, false);
  TEST_ASSERT_TRUE(policy.onScanResult(35100, true, 2) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::AccessPoint);
}

void test_queda_da_station_vai_a_station_down() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(1000, true, 0, false);
  TEST_ASSERT_TRUE(policy.tick(2000, false, 0, false) == WifiAction::StationDown);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::StationLost);
}

void test_reconexao_dentro_de_30s_volta_a_station() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(1000, true, 0, false);
  policy.tick(2000, false, 0, false);
  TEST_ASSERT_TRUE(policy.tick(31999, false, 0, false) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(20000, true, 0, false) == WifiAction::StationUp);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::Station);
}

void test_timeout_de_reconexao_sobe_ap_e_depois_procura() {
  WifiPolicy policy(kTimings);
  policy.begin(true, 0);
  policy.tick(1000, true, 0, false);
  policy.tick(2000, false, 0, false);
  TEST_ASSERT_TRUE(policy.tick(31999, false, 0, false) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(32000, false, 0, false) == WifiAction::StartAccessPoint);
  TEST_ASSERT_TRUE(policy.mode() == WifiMode::AccessPoint);

  // Último scan = entrada no AP (32000): o primeiro scan vem 20 s depois.
  TEST_ASSERT_TRUE(policy.tick(51999, false, 0, false) == WifiAction::None);
  TEST_ASSERT_TRUE(policy.tick(52000, false, 0, false) == WifiAction::StartScan);
}

void test_nomes_dos_modos() {
  TEST_ASSERT_EQUAL_STRING("connecting", onix::wifiModeName(WifiMode::Connecting));
  TEST_ASSERT_EQUAL_STRING("station", onix::wifiModeName(WifiMode::Station));
  TEST_ASSERT_EQUAL_STRING("station-lost", onix::wifiModeName(WifiMode::StationLost));
  TEST_ASSERT_EQUAL_STRING("access-point", onix::wifiModeName(WifiMode::AccessPoint));
}

void test_timings_padrao_vem_do_config() {
  const WifiTimings timings = onix::defaultWifiTimings();
  TEST_ASSERT_EQUAL_UINT32(15000, timings.connectTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(30000, timings.reconnectTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(20000, timings.scanIntervalMs);
}

void test_ip_fixo_primeiro_livre_da_faixa() {
  ProbeState state;
  const Ipv4 gateway{192, 168, 43, 1};
  const Ipv4 mask{255, 255, 255, 0};
  const Ipv4 current{192, 168, 43, 17};
  const std::optional<Ipv4> chosen =
      onix::selectStaticHost(gateway, mask, current, 50, 10, probeFake, &state);
  TEST_ASSERT_TRUE(chosen.has_value());
  TEST_ASSERT_TRUE(*chosen == (Ipv4{192, 168, 43, 50}));
  TEST_ASSERT_EQUAL_INT(1, state.calls);
}

void test_ip_fixo_pula_enderecos_em_uso() {
  ProbeState state;
  state.used = {Ipv4{192, 168, 43, 50}, Ipv4{192, 168, 43, 51}};
  const std::optional<Ipv4> chosen =
      onix::selectStaticHost(Ipv4{192, 168, 43, 1}, Ipv4{255, 255, 255, 0}, Ipv4{192, 168, 43, 17},
                             50, 10, probeFake, &state);
  TEST_ASSERT_TRUE(chosen.has_value());
  TEST_ASSERT_TRUE(*chosen == (Ipv4{192, 168, 43, 52}));
  TEST_ASSERT_EQUAL_INT(3, state.calls);
}

void test_ip_fixo_toda_faixa_em_uso_devolve_nullopt() {
  ProbeState state;
  for (uint8_t host = 50; host < 60; ++host) {
    state.used.push_back(Ipv4{192, 168, 43, host});
  }
  const std::optional<Ipv4> chosen =
      onix::selectStaticHost(Ipv4{192, 168, 43, 1}, Ipv4{255, 255, 255, 0}, Ipv4{192, 168, 43, 17},
                             50, 10, probeFake, &state);
  TEST_ASSERT_FALSE(chosen.has_value());
  TEST_ASSERT_EQUAL_INT(10, state.calls);
}

void test_ip_fixo_atual_ja_candidato_nao_sonda() {
  ProbeState state;
  state.used = {Ipv4{192, 168, 43, 50}};
  const Ipv4 current{192, 168, 43, 55};
  const std::optional<Ipv4> chosen =
      onix::selectStaticHost(Ipv4{192, 168, 43, 1}, Ipv4{255, 255, 255, 0}, current, 50, 10,
                             probeFake, &state);
  TEST_ASSERT_TRUE(chosen.has_value());
  TEST_ASSERT_TRUE(*chosen == current);
  TEST_ASSERT_EQUAL_INT(0, state.calls);
}

void test_ip_fixo_mascara_28_sem_candidatos_validos() {
  ProbeState state;
  const std::optional<Ipv4> chosen =
      onix::selectStaticHost(Ipv4{172, 20, 10, 1}, Ipv4{255, 255, 255, 240}, Ipv4{172, 20, 10, 5},
                             50, 10, probeFake, &state);
  TEST_ASSERT_FALSE(chosen.has_value());
  TEST_ASSERT_EQUAL_INT(0, state.calls);
}

void test_ip_fixo_pula_o_gateway() {
  ProbeState state;
  const std::optional<Ipv4> chosen =
      onix::selectStaticHost(Ipv4{192, 168, 43, 50}, Ipv4{255, 255, 255, 0}, Ipv4{192, 168, 43, 17},
                             50, 10, probeFake, &state);
  TEST_ASSERT_TRUE(chosen.has_value());
  TEST_ASSERT_TRUE(*chosen == (Ipv4{192, 168, 43, 51}));
}

}  // namespace

void setUp() {}

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_begin_sem_credencial_vai_ao_ap_e_nunca_procura);
  RUN_TEST(test_begin_com_credencial_inicia_station);
  RUN_TEST(test_conectar_vai_a_station_up);
  RUN_TEST(test_timeout_de_conexao_em_15000_sobe_ap);
  RUN_TEST(test_sem_scan_antes_do_intervalo);
  RUN_TEST(test_scan_no_intervalo_sem_clientes);
  RUN_TEST(test_sem_scan_com_clientes_no_ap);
  RUN_TEST(test_sem_scan_com_scan_em_curso);
  RUN_TEST(test_scan_encontrado_inicia_station);
  RUN_TEST(test_scan_nao_encontrado_nao_faz_nada);
  RUN_TEST(test_scan_encontrado_com_clientes_nao_troca);
  RUN_TEST(test_queda_da_station_vai_a_station_down);
  RUN_TEST(test_reconexao_dentro_de_30s_volta_a_station);
  RUN_TEST(test_timeout_de_reconexao_sobe_ap_e_depois_procura);
  RUN_TEST(test_nomes_dos_modos);
  RUN_TEST(test_timings_padrao_vem_do_config);
  RUN_TEST(test_ip_fixo_primeiro_livre_da_faixa);
  RUN_TEST(test_ip_fixo_pula_enderecos_em_uso);
  RUN_TEST(test_ip_fixo_toda_faixa_em_uso_devolve_nullopt);
  RUN_TEST(test_ip_fixo_atual_ja_candidato_nao_sonda);
  RUN_TEST(test_ip_fixo_mascara_28_sem_candidatos_validos);
  RUN_TEST(test_ip_fixo_pula_o_gateway);
  return UNITY_END();
}
