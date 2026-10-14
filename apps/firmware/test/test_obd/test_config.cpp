// test_config.cpp — suite de configuração centralizada (SPEC-0001 §Configuração).
// Valida as invariantes dos padrões de config.h, sem números mágicos duplicados:
// freshness > timeout + interval por sensor; ecuHealth > timeout; recoveryInitial < recoveryMax.

#include "unity.h"

#include "config.h"
#include "snapshot.h"
#include "tests.h"

namespace onix::test {
namespace {

void test_defaults_respeitam_invariantes() {
  const TimingConfig t = defaultTimingConfig();
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    TEST_ASSERT_TRUE(t.freshnessMs[i] > t.obdTimeoutMs + t.intervalsMs[i]);
    TEST_ASSERT_TRUE(t.intervalsMs[i] > 0);
    TEST_ASSERT_TRUE(t.freshnessMs[i] > 0);
  }
  TEST_ASSERT_TRUE(t.ecuHealthMs > t.obdTimeoutMs);
  TEST_ASSERT_TRUE(t.recoveryInitialMs < t.recoveryMaxMs);
  TEST_ASSERT_TRUE(t.recoveryInitialMs > 0);
  TEST_ASSERT_TRUE(t.obdTimeoutMs > 0);
}

void test_origem_padrao_tensao_e_adapter() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(VoltageSource::Adapter),
                        static_cast<int>(ONIX_DEFAULT_VOLTAGE_SOURCE));
}

}  // namespace

void runConfigSuite() {
  RUN_TEST(test_defaults_respeitam_invariantes);
  RUN_TEST(test_origem_padrao_tensao_e_adapter);
}

}  // namespace onix::test
