// test_ws.cpp — testes Unity da política de throttle do broadcast WebSocket.
// Lógica pura (ws_broadcast.h): nenhum include Arduino, roda no env native.
#include <cstdint>

#include "unity.h"
#include "ws_broadcast.h"

namespace {

using onix::shouldBroadcast;

void test_sem_clientes_nunca_broadcast() {
  TEST_ASSERT_FALSE(shouldBroadcast(1000, 0, 500, false, false));
  TEST_ASSERT_FALSE(shouldBroadcast(1000, 0, 500, false, true));  // dirty não supera ausência
  TEST_ASSERT_FALSE(shouldBroadcast(1000, 0, 0, false, true));    // nem intervalo zero
}

void test_intervalo_bloqueia_antes_e_libera_depois() {
  TEST_ASSERT_FALSE(shouldBroadcast(499, 0, 500, true, false));
  TEST_ASSERT_TRUE(shouldBroadcast(500, 0, 500, true, false));
  TEST_ASSERT_TRUE(shouldBroadcast(1500, 1000, 500, true, false));
}

void test_dirty_forca_imediato_uma_vez_por_ms() {
  TEST_ASSERT_TRUE(shouldBroadcast(1001, 1000, 500, true, true));
  TEST_ASSERT_FALSE(shouldBroadcast(1000, 1000, 500, true, true));  // já enviado neste ms
}

void test_wraparound_de_millis() {
  // lastMs perto de 2^32, nowMs após o estouro: diferença real 0x200 = 512 ms.
  TEST_ASSERT_TRUE(shouldBroadcast(0x00000100u, 0xFFFFFF00u, 500, true, false));
  // Diferença real 10 ms < intervalo: não envia.
  TEST_ASSERT_FALSE(shouldBroadcast(0x0000000Au, 0xFFFFFF00u, 500, true, false));
}

void test_intervalo_zero_envia_sempre_com_clientes() {
  TEST_ASSERT_TRUE(shouldBroadcast(1000, 1000, 0, true, false));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sem_clientes_nunca_broadcast);
  RUN_TEST(test_intervalo_bloqueia_antes_e_libera_depois);
  RUN_TEST(test_dirty_forca_imediato_uma_vez_por_ms);
  RUN_TEST(test_wraparound_de_millis);
  RUN_TEST(test_intervalo_zero_envia_sempre_com_clientes);
  return UNITY_END();
}
