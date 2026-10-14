// test_main.cpp — runner Unity dos testes nativos de obd.h (SPEC-0001 DoD).
// Ponto único de entrada do env native/test_obd: chama as suites de sessão,
// recuperação, escalonador e configuração.

#include "unity.h"

#include "tests.h"

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  onix::test::runSessionSuite();
  onix::test::runRecoverySuite();
  onix::test::runSchedulerSuite();
  onix::test::runConfigSuite();
  return UNITY_END();
}
