// test_session.cpp — casos 1–7, 9 da SPEC-0001: sessão, suporte, falha isolada e zero válido.

#include "unity.h"

#include "obd_fakes.h"
#include "tests.h"

namespace onix::test {
namespace {

void test_boot_without_scanner_keeps_state_without_readings() {
  ObdHarness h;
  h.transport.onConnect(false, false);  // não há scanner no boot

  for (int i = 0; i < 5; ++i) {
    h.step();
  }

  const Snapshot& s = h.session.snapshot();
  TEST_ASSERT_FALSE(s.btConnected);
  TEST_ASSERT_FALSE(s.adapterReady);
  TEST_ASSERT_FALSE(s.hasEcuResponse);
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    TEST_ASSERT_FALSE(s.sensors[i].hasValue);
    TEST_ASSERT_FALSE(s.sensors[i].unsupported);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Unavailable),
                          static_cast<int>(s.sensors[i].lastStatus));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::Connect) >= 1);
  TEST_ASSERT_TRUE(h.log.count("bt") >= 1);
}

void test_conectar_nao_comprova_ecu() {
  ObdHarness h;

  // Bluetooth e adaptador respondem, mas nenhuma consulta de mapa responde.
  h.transport.setDefault(obdTimeout());
  h.stepUntil(ObdPhase::Polling);

  const Snapshot& s = h.session.snapshot();
  TEST_ASSERT_TRUE(s.btConnected);
  TEST_ASSERT_TRUE(s.adapterReady);
  TEST_ASSERT_FALSE(s.hasEcuResponse);
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    TEST_ASSERT_FALSE(s.sensors[i].hasValue);
    TEST_ASSERT_FALSE(s.sensors[i].unsupported);
  }
}

void test_mapa_declara_pid_ausente_e_retira_do_polling() {
  ObdHarness h;
  h.supportPids({0x0C, 0x0D, 0x05, 0x11});  // MAP (0x0B) ausente no mapa válido
  h.valuesOk();
  TEST_ASSERT_TRUE(h.stepUntil(ObdPhase::Polling));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Supported),
                        static_cast<int>(h.session.support(SensorId::Rpm)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Unsupported),
                        static_cast<int>(h.session.support(SensorId::Map)));

  h.runMs(3000);
  TEST_ASSERT_EQUAL_UINT32(0, h.transport.countPid(0x0B));

  const SensorReading& map = h.reading(SensorId::Map);
  TEST_ASSERT_FALSE(map.hasValue);
  TEST_ASSERT_TRUE(map.unsupported);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Unsupported),
                        static_cast<int>(map.lastStatus));
  TEST_ASSERT_TRUE(h.transport.countPid(0x0C) >= 3);  // os demais seguem sendo lidos
}

void test_no_data_nao_significa_nao_suportado() {
  ObdHarness h;
  h.supportPids(ObdHarness::allPids());
  h.valuesOk();
  h.transport.onPid(0x05, obdNoData());  // Coolant: mapa diz suportado, resposta é NO DATA
  TEST_ASSERT_TRUE(h.stepUntil(ObdPhase::Polling));

  h.runMs(4000);

  const SensorReading& coolant = h.reading(SensorId::Coolant);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Supported),
                        static_cast<int>(h.session.support(SensorId::Coolant)));
  TEST_ASSERT_FALSE(coolant.unsupported);
  TEST_ASSERT_FALSE(coolant.hasValue);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Timeout),
                        static_cast<int>(coolant.lastStatus));
  TEST_ASSERT_TRUE(h.transport.countPid(0x05) >= 2);  // continua consultando
  TEST_ASSERT_TRUE(h.session.snapshot().btConnected);
  TEST_ASSERT_TRUE(h.session.snapshot().hasEcuResponse);
}

void test_zero_legitimo_e_leitura_valida() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(800, 0, 88, 4, 99, 12.4);  // velocidade zero, parado
  const uint64_t before = h.clock.now();
  h.runMs(1200);

  const SensorReading& speed = h.reading(SensorId::Speed);
  TEST_ASSERT_TRUE(speed.hasValue);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, speed.value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorStatus::Ok),
                        static_cast<int>(speed.lastStatus));
  TEST_ASSERT_TRUE(speed.updatedAtMs >= before);
  TEST_ASSERT_FALSE(speed.unsupported);
}

void test_perda_do_transporte_invalida_sessao_e_conserva_leituras() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(1500, 40, 90, 20, 100, 13.2);
  h.runMs(600);

  const SensorReading before = h.reading(SensorId::Rpm);
  TEST_ASSERT_TRUE(before.hasValue);

  h.transport.cutTransport();
  h.step();

  const Snapshot& s = h.session.snapshot();
  TEST_ASSERT_FALSE(s.btConnected);
  TEST_ASSERT_FALSE(s.adapterReady);
  TEST_ASSERT_FALSE(s.hasEcuResponse);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));

  const SensorReading& after = h.reading(SensorId::Rpm);
  TEST_ASSERT_TRUE(after.hasValue);
  TEST_ASSERT_EQUAL_DOUBLE(before.value, after.value);
  TEST_ASSERT_EQUAL_UINT64(before.updatedAtMs, after.updatedAtMs);
}

void test_readings_survive_while_scanner_is_away() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(900, 0, 85, 3, 98, 12.2);
  h.runMs(600);

  h.transport.cutTransport();
  h.runMs(30000);

  TEST_ASSERT_TRUE(h.reading(SensorId::Rpm).hasValue);
  TEST_ASSERT_FALSE(h.session.snapshot().btConnected);
  TEST_ASSERT_FALSE(h.session.snapshot().hasEcuResponse);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Disconnected),
                        static_cast<int>(h.session.phase()));
}

void test_nova_sessao_reavalia_suporte() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(600);
  TEST_ASSERT_TRUE(h.transport.countPid(0x0B) >= 1);

  h.transport.cutTransport();
  h.step();

  // Scanner recolocado; o carro desta vez não declara MAP (0x0B).
  h.transport.restoreTransport();
  h.supportPids({0x0C, 0x0D, 0x05, 0x11});
  const uint32_t mapCallsBefore = h.transport.countPid(0x0B);
  TEST_ASSERT_TRUE(h.stepUntil(ObdPhase::Polling));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdSupport::Unsupported),
                        static_cast<int>(h.session.support(SensorId::Map)));
  h.runMs(3000);
  TEST_ASSERT_EQUAL_UINT32(mapCallsBefore, h.transport.countPid(0x0B));
  TEST_ASSERT_TRUE(h.reading(SensorId::Map).hasValue);  // última leitura preservada
}

void test_ecu_silenciosa_nao_derruba_transporte_nem_leituras() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.valuesOk(1000, 10, 90, 10, 100, 12.6);
  h.runMs(800);
  TEST_ASSERT_TRUE(h.session.snapshot().hasEcuResponse);

  // Carro desligado: scanner conectado, ECU sem responder.
  for (uint8_t pid : ObdHarness::allPids()) {
    h.transport.onPid(pid, obdNoData());
  }
  h.transport.onAdapterVoltage(obdNoData());

  const double rpmBefore = h.reading(SensorId::Rpm).value;
  const uint64_t updatedBefore = h.reading(SensorId::Rpm).updatedAtMs;
  h.runMs(7000);  // > ecuHealthMs

  const Snapshot& s = h.session.snapshot();
  TEST_ASSERT_TRUE(s.btConnected);
  TEST_ASSERT_TRUE(s.adapterReady);
  TEST_ASSERT_FALSE(s.hasEcuResponse);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ObdPhase::Polling),
                        static_cast<int>(h.session.phase()));
  TEST_ASSERT_TRUE(h.reading(SensorId::Rpm).hasValue);
  TEST_ASSERT_EQUAL_DOUBLE(rpmBefore, h.reading(SensorId::Rpm).value);
  TEST_ASSERT_EQUAL_UINT64(updatedBefore, h.reading(SensorId::Rpm).updatedAtMs);

  // ECU voltando a responder: saúde recuperada sem reconexão Bluetooth.
  h.valuesOk(1200, 20, 91, 11, 101, 12.7);
  h.runMs(800);
  TEST_ASSERT_TRUE(h.session.snapshot().hasEcuResponse);
  TEST_ASSERT_TRUE(h.session.snapshot().btConnected);
}

void test_voltage_usa_at_rv_por_padrao() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.transport.onAdapterVoltage(obdOk(13.9));
  h.runMs(1200);

  const SensorReading& v = h.reading(SensorId::Voltage);
  TEST_ASSERT_TRUE(v.hasValue);
  TEST_ASSERT_EQUAL_DOUBLE(13.9, v.value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(VoltageSource::Adapter),
                        static_cast<int>(h.session.snapshot().voltageSource));
  TEST_ASSERT_TRUE(h.transport.count(FakeCall::Kind::AdapterVoltage) >= 1);
  TEST_ASSERT_EQUAL_UINT32(0, h.transport.countPid(0x42));  // PID 42 não é usado por padrão
  TEST_ASSERT_EQUAL_UINT32(0, h.transport.countMap(0x40));  // mapa 0x40 só é preciso com PID 42
}

void test_logs_de_transicao_sem_spam() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(2000);

  TEST_ASSERT_TRUE(h.log.count("bt") >= 1);
  TEST_ASSERT_EQUAL_UINT32(1, h.log.count("bt"));  // transição registrada uma única vez
  TEST_ASSERT_TRUE(h.log.count("adapter") >= 1);
  TEST_ASSERT_TRUE(h.log.count("protocolo") >= 1);
  TEST_ASSERT_TRUE(h.log.lines.size() <= 24);
  TEST_ASSERT_EQUAL_UINT32(0, h.log.countLevel(ObdLogLevel::Verbose));
}

void test_publicacao_ocorre_fora_das_chamadas_bloqueantes() {
  ObdHarness h;
  TEST_ASSERT_TRUE(h.bringUpAll());
  h.runMs(2000);

  TEST_ASSERT_EQUAL_UINT32(0, h.transport.publishesDuringCall);
  TEST_ASSERT_TRUE(h.publisher.publishes > 0);
  TEST_ASSERT_TRUE(h.publisher.last.sensors[sensorIndex(SensorId::Rpm)].hasValue);
}

}  // namespace

void runSessionSuite() {
  RUN_TEST(test_boot_without_scanner_keeps_state_without_readings);
  RUN_TEST(test_conectar_nao_comprova_ecu);
  RUN_TEST(test_mapa_declara_pid_ausente_e_retira_do_polling);
  RUN_TEST(test_no_data_nao_significa_nao_suportado);
  RUN_TEST(test_zero_legitimo_e_leitura_valida);
  RUN_TEST(test_perda_do_transporte_invalida_sessao_e_conserva_leituras);
  RUN_TEST(test_readings_survive_while_scanner_is_away);
  RUN_TEST(test_nova_sessao_reavalia_suporte);
  RUN_TEST(test_ecu_silenciosa_nao_derruba_transporte_nem_leituras);
  RUN_TEST(test_voltage_usa_at_rv_por_padrao);
  RUN_TEST(test_logs_de_transicao_sem_spam);
  RUN_TEST(test_publicacao_ocorre_fora_das_chamadas_bloqueantes);
}

}  // namespace onix::test
