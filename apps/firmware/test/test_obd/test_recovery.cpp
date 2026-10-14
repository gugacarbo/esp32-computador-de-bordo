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

void test_recuperacao_cresce_e_respeita_teto() {
  ObdHarness h;
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

}  // namespace

void runRecoverySuite() {
  RUN_TEST(test_recuperacao_cresce_e_respeita_teto);
  RUN_TEST(test_sem_tentativas_continuas);
  RUN_TEST(test_boot_sem_scanner_nao_trava_e_recupera);
  RUN_TEST(test_desconexao_durante_consulta_invalida_sessao);
  RUN_TEST(test_recuperacao_sem_reboot_reavalia_suporte_e_publica);
  RUN_TEST(test_falha_de_conexao_nao_derruba_estado_anterior);
}

}  // namespace onix::test
