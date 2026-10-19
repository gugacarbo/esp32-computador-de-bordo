// test_recovery.cpp — casos 2, 3, 6, 7 e 8 da SPEC-0001: recuperação crescente com teto,
// recuperação sem reboot, desconexão durante consulta e ausência de tentativas contínuas.

#include <vector>

#include "unity.h"

#include "obd_fakes.h"
#include "tests.h"

namespace onix::test {
namespace {

// Coleta a espera armada após cada tentativa de recuperação (relógio controlado, sem espera real).
std::vector<uint32_t> collectRecoveryWaits(ObdHarness& h, int attempts) {
  std::vector<uint32_t> waits;
  uint64_t at = h.clock.now();
  h.step();  // primeira tentativa
  waits.push_back(static_cast<uint32_t>(h.session.nextAttemptMs() - at));
  for (int i = 1; i < attempts; ++i) {
    at = h.session.nextAttemptMs();
    h.clock.set(at);
    h.step();
    waits.push_back(static_cast<uint32_t>(h.session.nextAttemptMs() - at));
  }
  return waits;
}

// Uma tentativa de conexão no instante liberado pelo backoff (ou agora, se já liberado).
void tentaConexaoQuandoLiberada(ObdHarness& h) {
  if (h.session.nextAttemptMs() > h.clock.now()) {
    h.clock.set(h.session.nextAttemptMs());
  }
  h.step();
}

void test_recuperacao_cresce_e_respeita_teto() {
  // Sem limite de tentativas (0): o backoff precisa chegar ao teto de recuperação (SPEC-0001).
  ObdHarness h(defaultTimingConfig(), ONIX_DEFAULT_VOLTAGE_SOURCE, ObdLogLevel::Transition, 0);
  h.transport.onConnect(false, false);
  const TimingConfig t = defaultTimingConfig();

  const std::vector<uint32_t> waits = collectRecoveryWaits(h, 8);

  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(t.recoveryInitialMs), waits[0]);
  TEST_ASSERT_EQUAL_UINT32(t.recoveryInitialMs * 2, waits[1]);
  TEST_ASSERT_EQUAL_UINT32(t.recoveryInitialMs * 4, waits[2]);
  TEST_ASSERT_EQUAL_UINT32(t.recoveryInitialMs * 8, waits[3]);
  for (std::size_t i = 1; i < waits.size(); ++i) {
    TEST_ASSERT_TRUE(waits[i] >= waits[i - 1]);          // nunca encurta
    TEST_ASSERT_TRUE(waits[i] <= t.recoveryMaxMs);       // teto respeitado
  }
  TEST_ASSERT_EQUAL_UINT32(t.recoveryMaxMs, waits.back());
  TEST_ASSERT_EQUAL_UINT32(8, h.transport.count(FakeCall::Kind::Connect));
  TEST_ASSERT_FALSE(h.session.searchStopped());
}

void test_sem_tentativas_continuas() {
  ObdHarness h;
  h.transport.onConnect(false, false);

  for (int i = 0; i < 20; ++i) {
    h.advanceAndStep(37);  // passos curtos, muito mais frequentes que a espera de recuperação
  }

  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Connect) <= 2);
  TEST_ASSERT_TRUE(h.session.nextAttemptMs() > h.clock.now() - 1);
}

void test_boot_sem_scanner_nao_trava_e_recupera() {
  ObdHarness h;
  h.transport.onConnect(false, false);

  for (int i = 0; i < 3; ++i) {
    const uint32_t wait = h.step();
    TEST_ASSERT_TRUE(wait <= ONIX_OBD_IDLE_WAIT_MS || wait <= defaultTimingConfig().recoveryMaxMs);
  }
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Connect) >= 1);

  h.transport.restoreTransport();
  h.runMs(10000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Polling),
                        static_cast<int>(h.session.phase()));
  TEST_ASSERT_TRUE(h.session.snapshot().btConnected);
}

void test_desconexao_durante_consulta_invalida_sessao() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(1300, 30, 90, 15, 100, 13.1);
  h.runMs(600);

  const SensorReading rpmBefore = h.reading(SensorId::Rpm);
  TEST_ASSERT_TRUE(rpmBefore.hasValue);

  h.transport.cutTransport();  // retirada do scanner com transação em curso
  h.advance(200);
  h.step();

  const Snapshot& s = h.session.snapshot();
  TEST_ASSERT_FALSE(s.btConnected);
  TEST_ASSERT_FALSE(s.adapterReady);
  TEST_ASSERT_FALSE(s.hasEcuResponse);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));
  TEST_ASSERT_EQUAL_DOUBLE(rpmBefore.value, h.reading(SensorId::Rpm).value);
  TEST_ASSERT_EQUAL_UINT64(rpmBefore.updatedAtMs, h.reading(SensorId::Rpm).updatedAtMs);
  TEST_ASSERT_FALSE(h.reading(SensorId::Rpm).unsupported);
}

void test_recuperacao_sem_reboot_reavalia_suporte_e_publica() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(900, 0, 80, 2, 95, 12.0);
  h.runMs(600);

  h.transport.cutTransport();
  h.step();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));

  // Scanner recolocado; RPM volta a responder com outro valor.
  h.transport.restoreTransport();
  h.supportPids(ObdHarness::allPids());
  h.valuesOk(2400, 55, 88, 25, 105, 13.6);
  h.runMs(20000);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Polling),
                        static_cast<int>(h.session.phase()));
  const Snapshot& s = h.session.snapshot();
  TEST_ASSERT_TRUE(s.btConnected);
  TEST_ASSERT_TRUE(s.adapterReady);
  TEST_ASSERT_TRUE(s.hasEcuResponse);
  TEST_ASSERT_EQUAL_DOUBLE(2400.0, h.reading(SensorId::Rpm).value);
  TEST_ASSERT_EQUAL_DOUBLE(55.0, h.reading(SensorId::Speed).value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Supported),
                        static_cast<int>(h.session.support(SensorId::Rpm)));
}

void test_falha_de_conexao_nao_derruba_estado_anterior() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(1500, 20, 90, 12, 100, 13.0);
  h.runMs(600);

  const double rpm = h.reading(SensorId::Rpm).value;
  h.transport.cutTransport();
  h.runMs(120000);  // bem além do teto de recuperação

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));
  TEST_ASSERT_FALSE(h.session.snapshot().btConnected);
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Connect) >= 2);
  TEST_ASSERT_EQUAL_DOUBLE(rpm, h.reading(SensorId::Rpm).value);
  TEST_ASSERT_TRUE(h.reading(SensorId::Rpm).hasValue);
}

// Bluetooth conecta mas o adaptador não inicializa (ex.: carro desligado): a espera cresce como na
// falha de conexão, sem reconexão Bluetooth incessante (SPEC-0001 §Recuperação).
void test_falha_na_inicializacao_do_adaptador_aplica_espera_crescente() {
  ObdHarness h;
  h.transport.onConnect(true, false);
  const TimingConfig t = defaultTimingConfig();

  std::vector<uint32_t> waits;
  for (int i = 0; i < 7; ++i) {
    h.step();  // conexão Bluetooth bem-sucedida
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Initializing),
                          static_cast<int>(h.session.phase()));
    h.step();  // inicialização do adaptador falha
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                          static_cast<int>(h.session.phase()));
    waits.push_back(h.session.recoveryWaitMs());
    h.clock.set(h.session.nextAttemptMs());
  }

  TEST_ASSERT_EQUAL_UINT32(t.recoveryInitialMs, waits[0]);
  TEST_ASSERT_EQUAL_UINT32(t.recoveryInitialMs * 2, waits[1]);
  TEST_ASSERT_EQUAL_UINT32(t.recoveryInitialMs * 4, waits[2]);
  for (std::size_t i = 1; i < waits.size(); ++i) {
    TEST_ASSERT_TRUE(waits[i] >= waits[i - 1]);
    TEST_ASSERT_TRUE(waits[i] <= t.recoveryMaxMs);
  }
  TEST_ASSERT_EQUAL_UINT32(t.recoveryMaxMs, waits.back());
}

void test_falha_na_inicializacao_do_adaptador_sem_reconexao_incessante() {
  ObdHarness h;
  h.transport.onConnect(true, false);

  h.runMs(60000);

  // Backoff 1+2+4+8+16+30 s: no máximo 7 tentativas no minuto, nunca uma por passo.
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Connect) <= 7);
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Connect) >= 2);
  TEST_ASSERT_EQUAL_UINT32(h.transport.count(FakeCall::Kind::Connect),
                           h.transport.count(FakeCall::Kind::AdapterInit));
}

// Sensor marcado não suportado numa sessão volta a ser avaliado na seguinte (SPEC-0001 §Suporte:
// "reavaliar suporte após uma nova inicialização bem-sucedida").
void test_nova_sessao_limpa_nao_suportado_e_reavalia() {
  ObdHarness h;
  h.supportPids({0x05, 0x0C, 0x0D, 0x11});  // MAP (0x0B) ausente no mapa
  h.valuesOk();
  TEST_ASSERT_TRUE(h.stepUntil(ObdPhase::Polling));
  TEST_ASSERT_TRUE(h.reading(SensorId::Map).unsupported);

  h.transport.cutTransport();
  h.step();
  TEST_ASSERT_FALSE(h.reading(SensorId::Map).unsupported);
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(SensorStatus::Unsupported),
                        static_cast<int>(h.reading(SensorId::Map).lastStatus));

  h.transport.restoreTransport();
  h.supportPids(ObdHarness::allPids());
  h.runMs(20000);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Polling),
                        static_cast<int>(h.session.phase()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Supported),
                        static_cast<int>(h.session.support(SensorId::Map)));
  TEST_ASSERT_FALSE(h.reading(SensorId::Map).unsupported);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Ok),
                        static_cast<int>(h.reading(SensorId::Map).lastStatus));
  TEST_ASSERT_TRUE(h.transport.countPid(0x0B) > 0);
}

// Falha na leitura do mapa na nova sessão mantém suporte desconhecido, não o "não suportado" antigo.
void test_nova_sessao_sem_mapa_nao_herda_nao_suportado() {
  ObdHarness h;
  h.supportPids({0x05, 0x0C, 0x0D, 0x11});
  h.valuesOk();
  TEST_ASSERT_TRUE(h.stepUntil(ObdPhase::Polling));
  TEST_ASSERT_TRUE(h.reading(SensorId::Map).unsupported);

  h.transport.cutTransport();
  h.step();
  h.transport.restoreTransport();
  h.transport.onMap(0x00, obdTimeout());
  h.runMs(20000);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Unknown),
                        static_cast<int>(h.session.support(SensorId::Map)));
  TEST_ASSERT_FALSE(h.reading(SensorId::Map).unsupported);
  TEST_ASSERT_TRUE(h.transport.countPid(0x0B) > 0);
}

// Busca parada após ONIX_BT_MAX_CONNECT_ATTEMPTS falhas seguidas de connect (painel decide retomar).
void test_busca_para_apos_max_falhas_e_nao_tenta_mais() {
  ObdHarness h;
  h.transport.onConnect(false, false);

  for (uint32_t i = 0; i < ONIX_BT_MAX_CONNECT_ATTEMPTS - 1; ++i) {
    tentaConexaoQuandoLiberada(h);
  }
  TEST_ASSERT_FALSE(h.session.searchStopped());

  tentaConexaoQuandoLiberada(h);
  TEST_ASSERT_TRUE(h.session.searchStopped());
  TEST_ASSERT_TRUE(h.session.snapshot().btSearchStopped);
  TEST_ASSERT_EQUAL_UINT32(1, h.log.count("busca parada apos 5 tentativas"));

  const uint32_t connectsAtStop = h.transport.count(FakeCall::Kind::Connect);
  h.runMs(600000);  // dez minutos: bem além de qualquer espera de recuperação
  TEST_ASSERT_EQUAL_UINT32(connectsAtStop, h.transport.count(FakeCall::Kind::Connect));
  TEST_ASSERT_TRUE(h.session.searchStopped());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));
}

// requestReconnect limpa a parada e a próxima step() tenta conectar no mesmo instante.
void test_requestReconnect_retoma_busca_na_hora() {
  ObdHarness h;
  h.transport.onConnect(false, false);
  for (uint32_t i = 0; i < ONIX_BT_MAX_CONNECT_ATTEMPTS; ++i) {
    tentaConexaoQuandoLiberada(h);
  }
  TEST_ASSERT_TRUE(h.session.searchStopped());
  h.runMs(60000);
  const uint32_t connectsBefore = h.transport.count(FakeCall::Kind::Connect);

  h.transport.restoreTransport();
  h.session.requestReconnect();
  TEST_ASSERT_FALSE(h.session.searchStopped());
  TEST_ASSERT_FALSE(h.session.snapshot().btSearchStopped);

  const uint64_t at = h.clock.now();
  h.step();  // sem avançar o relógio: a tentativa é imediata
  TEST_ASSERT_EQUAL_UINT32(connectsBefore + 1, h.transport.count(FakeCall::Kind::Connect));
  TEST_ASSERT_EQUAL_UINT64(at, h.transport.calls.back().atMs);  // connect no instante do pedido
  TEST_ASSERT_TRUE(h.session.snapshot().btConnected);
  TEST_ASSERT_EQUAL_UINT32(1, h.log.count("reconexao solicitada"));
}

// Um connect bem-sucedido zera a contagem: 4 falhas, 1 sucesso e 4 falhas não param a busca.
void test_sucesso_zera_contagem_de_falhas() {
  ObdHarness h;
  h.transport.onConnect(false, false);
  for (uint32_t i = 0; i < ONIX_BT_MAX_CONNECT_ATTEMPTS - 1; ++i) {
    tentaConexaoQuandoLiberada(h);
  }

  h.transport.onConnect(true, true);
  tentaConexaoQuandoLiberada(h);
  TEST_ASSERT_TRUE(h.session.snapshot().btConnected);

  h.transport.cutTransport();  // scanner some: a sessão cai; a falha de init não conta como connect
  h.step();
  TEST_ASSERT_FALSE(h.session.snapshot().btConnected);

  for (uint32_t i = 0; i < ONIX_BT_MAX_CONNECT_ATTEMPTS - 1; ++i) {
    tentaConexaoQuandoLiberada(h);
  }
  TEST_ASSERT_FALSE(h.session.searchStopped());

  tentaConexaoQuandoLiberada(h);
  TEST_ASSERT_TRUE(h.session.searchStopped());
}

}  // namespace

void runRecoverySuite() {
  RUN_TEST(test_recuperacao_cresce_e_respeita_teto);
  RUN_TEST(test_sem_tentativas_continuas);
  RUN_TEST(test_boot_sem_scanner_nao_trava_e_recupera);
  RUN_TEST(test_desconexao_durante_consulta_invalida_sessao);
  RUN_TEST(test_recuperacao_sem_reboot_reavalia_suporte_e_publica);
  RUN_TEST(test_falha_de_conexao_nao_derruba_estado_anterior);
  RUN_TEST(test_falha_na_inicializacao_do_adaptador_aplica_espera_crescente);
  RUN_TEST(test_falha_na_inicializacao_do_adaptador_sem_reconexao_incessante);
  RUN_TEST(test_nova_sessao_limpa_nao_suportado_e_reavalia);
  RUN_TEST(test_nova_sessao_sem_mapa_nao_herda_nao_suportado);
  RUN_TEST(test_busca_para_apos_max_falhas_e_nao_tenta_mais);
  RUN_TEST(test_requestReconnect_retoma_busca_na_hora);
  RUN_TEST(test_sucesso_zera_contagem_de_falhas);
}

}  // namespace onix::test
