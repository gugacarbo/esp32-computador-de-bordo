// test_elm.cpp — testes Unity do mapeamento de status ELMduino para ObdOutcome.
// Lógica pura (obd_elm.h): nenhum include Arduino, roda no env native.
#include <cstdint>

#include "obd_elm.h"
#include "unity.h"

namespace {

using onix::mapElmStatus;
using onix::ObdOutcome;

// Converte o enum para inteiro para que Unity compare sem depender de operadores de enum class.
constexpr int o(ObdOutcome outcome) { return static_cast<int>(outcome); }

void test_valores_espelham_elmduino() {
  // Valores de ELMduino 3.4.1 (ELMduino.h, ~linhas 251-261).
  TEST_ASSERT_EQUAL_INT(0, onix::ONIX_ELM_SUCCESS);
  TEST_ASSERT_EQUAL_INT(1, onix::ONIX_ELM_NO_RESPONSE);
  TEST_ASSERT_EQUAL_INT(2, onix::ONIX_ELM_BUFFER_OVERFLOW);
  TEST_ASSERT_EQUAL_INT(3, onix::ONIX_ELM_GARBAGE);
  TEST_ASSERT_EQUAL_INT(4, onix::ONIX_ELM_UNABLE_TO_CONNECT);
  TEST_ASSERT_EQUAL_INT(5, onix::ONIX_ELM_NO_DATA);
  TEST_ASSERT_EQUAL_INT(6, onix::ONIX_ELM_STOPPED);
  TEST_ASSERT_EQUAL_INT(7, onix::ONIX_ELM_TIMEOUT);
  TEST_ASSERT_EQUAL_INT(8, onix::ONIX_ELM_GETTING_MSG);
  TEST_ASSERT_EQUAL_INT(9, onix::ONIX_ELM_MSG_RXD);
  TEST_ASSERT_EQUAL_INT(-1, onix::ONIX_ELM_GENERAL_ERROR);
}

void test_sucesso_mapeia_para_ok() {
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Ok), o(mapElmStatus(onix::ONIX_ELM_SUCCESS)));
}

void test_sem_dados_e_sem_conexao_mapeiam_para_nodata() {
  // Ignição desligada ou ECU calada: não é falha do adaptador.
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::NoData), o(mapElmStatus(onix::ONIX_ELM_NO_DATA)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::NoData), o(mapElmStatus(onix::ONIX_ELM_UNABLE_TO_CONNECT)));
}

void test_timeout_e_sem_resposta_mapeiam_para_timeout() {
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Timeout), o(mapElmStatus(onix::ONIX_ELM_TIMEOUT)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Timeout), o(mapElmStatus(onix::ONIX_ELM_NO_RESPONSE)));
}

void test_falhas_de_mensagem_e_pendentes_mapeiam_para_error() {
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(onix::ONIX_ELM_GARBAGE)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(onix::ONIX_ELM_BUFFER_OVERFLOW)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(onix::ONIX_ELM_STOPPED)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(onix::ONIX_ELM_GENERAL_ERROR)));
  // GETTING_MSG: ainda pendente quando o prazo do chamador expirou.
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(onix::ONIX_ELM_GETTING_MSG)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(onix::ONIX_ELM_MSG_RXD)));
}

void test_valor_desconhecido_mapeia_para_error() {
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(42)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(-7)));
  TEST_ASSERT_EQUAL_INT(o(ObdOutcome::Error), o(mapElmStatus(10)));
}

void test_nunca_retorna_disconnected() {
  // A perda do link é detectada pelo transporte, não pelo status do ELMduino.
  for (int16_t status = -128; status <= 127; ++status) {
    TEST_ASSERT_NOT_EQUAL(o(ObdOutcome::Disconnected), o(mapElmStatus(static_cast<int8_t>(status))));
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valores_espelham_elmduino);
  RUN_TEST(test_sucesso_mapeia_para_ok);
  RUN_TEST(test_sem_dados_e_sem_conexao_mapeiam_para_nodata);
  RUN_TEST(test_timeout_e_sem_resposta_mapeiam_para_timeout);
  RUN_TEST(test_falhas_de_mensagem_e_pendentes_mapeiam_para_error);
  RUN_TEST(test_valor_desconhecido_mapeia_para_error);
  RUN_TEST(test_nunca_retorna_disconnected);
  return UNITY_END();
}
