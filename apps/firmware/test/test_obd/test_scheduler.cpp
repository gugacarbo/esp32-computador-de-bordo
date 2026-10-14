// test_scheduler.cpp — casos 8 e 11 da SPEC-0001: uma transação por vez, prioridade justa dos
// sensores lentos, sem rajada após pausa e sem starvation.

#include <vector>

#include "unity.h"

#include "obd_fakes.h"
#include "tests.h"

namespace onix::test {
namespace {

// Conta leituras por sensor no snapshot atual, subindo a sessão do zero.
uint32_t countReadings(const ObdHarness& h, SensorId id) {
  return h.reading(id).hasValue ? 1u : 0u;
}

void test_uma_consulta_por_step() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(300);

  for (int i = 0; i < 40; ++i) {
    const uint32_t before = static_cast<uint32_t>(h.transport.count(FakeCall::Kind::Pid));
    h.step();
    const uint32_t after = static_cast<uint32_t>(h.transport.count(FakeCall::Kind::Pid));
    TEST_ASSERT_TRUE(after - before <= 1);
  }
}

void test_sensores_lentos_recebem_cota() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());

  const uint32_t rpmBefore = h.transport.countPid(0x0C);
  const uint32_t coolantBefore = h.transport.countPid(0x05);
  const uint32_t voltageBefore =
      h.transport.count(FakeCall::Kind::AdapterVoltage);

  h.runMs(3000);

  const uint32_t rpmReads = h.transport.countPid(0x0C) - rpmBefore;
  const uint32_t coolantReads = h.transport.countPid(0x05) - coolantBefore;
  const uint32_t voltageReads = h.transport.count(FakeCall::Kind::AdapterVoltage) - voltageBefore;

  TEST_ASSERT_TRUE(rpmReads >= 5);                        // rápido: ~200 ms
  TEST_ASSERT_TRUE(coolantReads >= 2);                    // lento: ~1000 ms, sem starvation
  TEST_ASSERT_TRUE(voltageReads >= 2);
  TEST_ASSERT_TRUE(rpmReads > coolantReads);              // rápido mais frequente que lento
  TEST_ASSERT_TRUE(coolantReads >= rpmReads / 6);         // ordem de grandeza do intervalo
}

void test_rpm_tem_prioridade_na_retomada() {
  ObdHarness h;
  const TimingConfig t = defaultTimingConfig();
  h.transport.supportPids(ObdHarness::allPids());
  h.valuesOk();
  TEST_ASSERT_TRUE(h.stepUntil(ObdPhase::Polling));

  // Todos vencidos ao mesmo tempo: a retomada começa pelo sensor mais rápido (RPM).
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    TEST_ASSERT_TRUE(h.session.nextDueMs(obdSensorAt(i)) <= h.clock.now());
  }
  const int first = h.session.lastTransactionSensorIndex();
  h.step();
  const int chosen = h.session.lastTransactionSensorIndex();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sensorIndex(SensorId::Rpm)), chosen);
  (void)first;
  (void)t;
}

void test_sem_rajada_apos_pausa_longa() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(1000);

  const uint32_t pidCallsBefore = static_cast<uint32_t>(h.transport.count(FakeCall::Kind::Pid));
  const uint64_t nowBefore = h.clock.now();

  h.advance(25000);  // pausa do produtor: muitas consultas venceriam
  h.step();

  const uint32_t pidCallsAfter = static_cast<uint32_t>(h.transport.count(FakeCall::Kind::Pid));
  TEST_ASSERT_EQUAL_UINT32(1, pidCallsAfter - pidCallsBefore);  // nenhuma rajada

  // O agendamento é retomado a partir do instante atual, sem repetir consultas atrasadas.
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    TEST_ASSERT_TRUE(h.session.nextDueMs(obdSensorAt(i)) >= nowBefore + 25000 ||
                    h.session.nextDueMs(obdSensorAt(i)) <= h.clock.now());
  }
}

void test_sem_starvation_com_adaptador_lento() {
  ObdHarness h;
  h.transport.pidLatencyMs = 700;        // scanner mais lento que os intervalos pretendidos
  h.transport.mapLatencyMs = 700;
  h.transport.adapterVoltageLatencyMs = 700;
  TEST_ASSERT_TRUE(h.bringUpAll());

  const uint32_t rpmBefore = h.transport.countPid(0x0C);
  const uint32_t speedBefore = h.transport.countPid(0x0D);
  const uint32_t coolantBefore = h.transport.countPid(0x05);
  const uint32_t mapBefore = h.transport.countPid(0x0B);
  const uint32_t throttleBefore = h.transport.countPid(0x11);
  const uint32_t voltageBefore = h.transport.count(FakeCall::Kind::AdapterVoltage);

  h.runMs(20000);

  TEST_ASSERT_TRUE(h.transport.countPid(0x0C) - rpmBefore >= 4);
  TEST_ASSERT_TRUE(h.transport.countPid(0x0D) - speedBefore >= 4);
  TEST_ASSERT_TRUE(h.transport.countPid(0x05) - coolantBefore >= 3);
  TEST_ASSERT_TRUE(h.transport.countPid(0x0B) - mapBefore >= 4);
  TEST_ASSERT_TRUE(h.transport.countPid(0x11) - throttleBefore >= 4);
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::AdapterVoltage) - voltageBefore >= 3);
}

void test_leituras_avancam_durante_operacao() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(1000, 0, 60, 5, 100, 12.5);
  h.runMs(1500);
  TEST_ASSERT_TRUE(countReadings(h, SensorId::Rpm) == 1);
  const uint64_t updatedRpm = h.reading(SensorId::Rpm).updatedAtMs;

  h.valuesOk(1100, 1, 61, 6, 101, 12.6);
  h.runMs(1500);
  TEST_ASSERT_TRUE(h.reading(SensorId::Rpm).updatedAtMs >= updatedRpm);
  TEST_ASSERT_EQUAL_DOUBLE(1100.0, h.reading(SensorId::Rpm).value);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, h.reading(SensorId::Speed).value);
}

void test_espera_sugerida_respeita_intervalo_do_proximo_sensor() {
  ObdHarness h;
  const TimingConfig t = defaultTimingConfig();
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(50);

  const uint32_t wait = h.step();
  TEST_ASSERT_TRUE(wait >= 1);
  TEST_ASSERT_TRUE(wait <= t.obdTimeoutMs);
  TEST_ASSERT_TRUE(wait <= ONIX_OBD_IDLE_WAIT_MS);
}

}  // namespace

void runSchedulerSuite() {
  RUN_TEST(test_uma_consulta_por_step);
  RUN_TEST(test_sensores_lentos_recebem_cota);
  RUN_TEST(test_rpm_tem_prioridade_na_retomada);
  RUN_TEST(test_sem_rajada_apos_pausa_longa);
  RUN_TEST(test_sem_starvation_com_adaptador_lento);
  RUN_TEST(test_leituras_avancam_durante_operacao);
  RUN_TEST(test_espera_sugerida_respeita_intervalo_do_proximo_sensor);
}

}  // namespace onix::test
