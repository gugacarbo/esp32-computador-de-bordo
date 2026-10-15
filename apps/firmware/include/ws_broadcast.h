// ws_broadcast.h — política pura de throttle do broadcast WebSocket, sem transporte.
//
// Decide SE um broadcast deve ocorrer neste tick; o transporte (WebSocketsServer,
// porta 81) vive em main.cpp. Cabeçalho portátil: compila no ESP32/Arduino e nos
// testes nativos com g++ — nenhum include Arduino aqui.
//
// Semântica de shouldBroadcast(nowMs, lastMs, intervalMs, hasClients, dirty):
// - sem clientes conectados -> nunca (economiza CPU/String/RAM no ESP32);
// - dirty (snapshot novo desde o último envio) -> envia de imediato, uma vez por ms;
// - caso contrário -> envia quando nowMs - lastMs >= intervalMs.
// A subtração em uint32_t tolera o wraparound de millis() (~49,7 dias).
#pragma once

#include <cstdint>

namespace onix {

inline bool shouldBroadcast(uint32_t nowMs, uint32_t lastMs, uint32_t intervalMs, bool hasClients,
                            bool dirty) {
  if (!hasClients) {
    return false;
  }
  if (dirty && nowMs != lastMs) {
    return true;
  }
  return (nowMs - lastMs) >= intervalMs;
}

}  // namespace onix
