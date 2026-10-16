// test_integration.cpp — integração da sessão OBD com a derivação da API (SPEC-0001 §Estado,
// SPEC-0002 §Envelope JSON) e escala dos PIDs de OBD_SENSOR_PIDS (SAE J1979).
// Usa o caminho de produção: ObdSession publica -> cópia do snapshot -> resolveSensorStatus e
// serializeSnapshotJson, com o mesmo relógio controlado e a mesma TimingConfig padrão.

#include <string>

#include "unity.h"

#include "api.h"
#include "obd_fakes.h"
#include "tests.h"

namespace onix::test {
namespace {

// Confere que, com ECU viva, RPM, velocidade e temperatura do motor estão Ok na API.
void expectLiveSensorsOk(const ObdHarness& h) {
  const Snapshot& snapshot = h.publisher.last;
  const TimingConfig timing = defaultTimingConfig();
  const uint64_t now = h.clock.now();

  TEST_ASSERT_TRUE(deriveSessionPhases(snapshot, timing, now).ecuResponding);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Ok),
                        static_cast<int>(resolveSensorStatus(snapshot, SensorId::Rpm, timing, now)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Ok),
                        static_cast<int>(resolveSensorStatus(snapshot, SensorId::Speed, timing, now)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Ok),
                        static_cast<int>(resolveSensorStatus(snapshot, SensorId::Coolant, timing, now)));

  const std::string json = serializeSnapshotJson(snapshot, timing, now, now);
  TEST_ASSERT_TRUE(json.find("\"ecuResponding\":true") != std::string::npos);
}

void test_ecu_viva_deixa_rpm_velocidade_e_temperatura_ok() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(1000);
  expectLiveSensorsOk(h);
}

void test_ecu_viva_alem_da_janela_de_saude_continua_ok() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  // Passa de ecuHealthMs (5 s) com respostas frescas: a ECU segue viva e nada deve expirar.
  h.runMs(defaultTimingConfig().ecuHealthMs + 2000);
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Pid) > 0);
  expectLiveSensorsOk(h);
}

// --- escala dos PIDs (bytes brutos -> valor físico) ---

void test_escala_rpm_pid_0c_quarto_de_rotacao() {
  const SensorPidSpec& spec = obdSpec(SensorId::Rpm);
  TEST_ASSERT_EQUAL_HEX8(0x0C, spec.pid);
  const double raw = 0x1A * 256.0 + 0xF8;  // (256*A + B)
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1726.0, raw * spec.scale + spec.bias);
}

void test_escala_temperatura_motor_pid_05_a_menos_40() {
  const SensorPidSpec& spec = obdSpec(SensorId::Coolant);
  TEST_ASSERT_EQUAL_HEX8(0x05, spec.pid);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 83.0, 0x7B * spec.scale + spec.bias);
}

void test_escala_tensao_pid_42_milivolts() {
  const SensorPidSpec& spec = obdSpec(SensorId::Voltage);
  TEST_ASSERT_EQUAL_HEX8(0x42, spec.pid);
  const double raw = 0x37 * 256.0 + 0x14;  // (256*A + B)
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 14.1, raw * spec.scale + spec.bias);
}

}  // namespace

void runIntegrationSuite() {
  RUN_TEST(test_ecu_viva_deixa_rpm_velocidade_e_temperatura_ok);
  RUN_TEST(test_ecu_viva_alem_da_janela_de_saude_continua_ok);
  RUN_TEST(test_escala_rpm_pid_0c_quarto_de_rotacao);
  RUN_TEST(test_escala_temperatura_motor_pid_05_a_menos_40);
  RUN_TEST(test_escala_tensao_pid_42_milivolts);
}

}  // namespace onix::test
