// obd_elm.h — tradução pura dos códigos de status do ELMduino para ObdOutcome (SPEC-0001).
//
// O transporte ESP32 (obd_transport_esp32.cpp, BluetoothSerial + ELMduino) converte o `status`
// devolvido pelo ELMduino em ObdOutcome com `mapElmStatus`. Este cabeçalho não inclui Arduino
// nem ELMduino: os valores ONIX_ELM_* são cópias de ELMduino 3.4.1 (ELMduino.h, ~linhas 251-261)
// e o transporte confere a igualdade com static_assert.
//
// Política de mapeamento:
// - SUCCESS -> Ok.
// - NO_DATA e UNABLE_TO_CONNECT -> NoData: ignição desligada ou ECU calada não é falha do adaptador.
// - TIMEOUT e NO_RESPONSE -> Timeout.
// - GARBAGE, BUFFER_OVERFLOW, STOPPED, GENERAL_ERROR, GETTING_MSG (ainda pendente: o prazo do
//   chamador expirou), MSG_RXD e qualquer valor desconhecido -> Error.
// - Nunca retorna Disconnected: a perda do link Bluetooth é detectada pelo próprio transporte.
#pragma once

#include <cstdint>

#include "obd.h"

namespace onix {

// Códigos ELM_* do ELMduino 3.4.1 duplicados (o transporte ESP32 confere com static_assert).
constexpr int8_t ONIX_ELM_SUCCESS = 0;
constexpr int8_t ONIX_ELM_NO_RESPONSE = 1;
constexpr int8_t ONIX_ELM_BUFFER_OVERFLOW = 2;
constexpr int8_t ONIX_ELM_GARBAGE = 3;
constexpr int8_t ONIX_ELM_UNABLE_TO_CONNECT = 4;
constexpr int8_t ONIX_ELM_NO_DATA = 5;
constexpr int8_t ONIX_ELM_STOPPED = 6;
constexpr int8_t ONIX_ELM_TIMEOUT = 7;
constexpr int8_t ONIX_ELM_GETTING_MSG = 8;
constexpr int8_t ONIX_ELM_MSG_RXD = 9;
constexpr int8_t ONIX_ELM_GENERAL_ERROR = -1;

// Status ELMduino -> resultado da sessão. Ver a política no topo do arquivo.
constexpr ObdOutcome mapElmStatus(int8_t status) {
  switch (status) {
    case ONIX_ELM_SUCCESS:
      return ObdOutcome::Ok;
    case ONIX_ELM_NO_DATA:
    case ONIX_ELM_UNABLE_TO_CONNECT:
      return ObdOutcome::NoData;
    case ONIX_ELM_TIMEOUT:
    case ONIX_ELM_NO_RESPONSE:
      return ObdOutcome::Timeout;
    case ONIX_ELM_GARBAGE:
    case ONIX_ELM_BUFFER_OVERFLOW:
    case ONIX_ELM_STOPPED:
    case ONIX_ELM_GENERAL_ERROR:
    case ONIX_ELM_GETTING_MSG:
    case ONIX_ELM_MSG_RXD:
    default:
      return ObdOutcome::Error;
  }
}

}  // namespace onix
