// wifi_policy.h — decisão de rede do firmware (ADR-0012), sem transporte: quando tentar o
// hotspot (STA), quando subir o SoftAP e quando procurar o hotspot de novo. main.cpp executa as
// ações com a API WiFi do Arduino; aqui só há estado e relógio injetado, testável no env native.
//
// Transições:
//   begin(sem credencial)            -> AccessPoint, StartAccessPoint (nunca procura)
//   begin(com credencial)            -> Connecting,  StartStation
//   Connecting + conectou            -> Station,     StationUp
//   Connecting + connectTimeoutMs    -> AccessPoint, StartAccessPoint
//   Station + desconectou            -> StationLost, StationDown
//   StationLost + reconectou         -> Station,     StationUp
//   StationLost + reconnectTimeoutMs -> AccessPoint, StartAccessPoint
//   AccessPoint + credencial + 0 clientes + sem scan em curso + scanIntervalMs desde o último
//                                    -> StartScan
//   onScanResult(achou, 0 clientes)  -> Connecting,  StartStation
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace onix {

enum class WifiMode : uint8_t { Connecting, Station, StationLost, AccessPoint };

enum class WifiAction : uint8_t {
  None,
  StartStation,      // WiFi.mode(STA) + begin(ssid, pass), DHCP
  StationUp,         // conectou: escolher IP fixo (selectStaticHost) e logar
  StationDown,       // caiu: voltar ao DHCP antes da reconexão automática
  StartAccessPoint,  // derrubar a STA e subir o SoftAP (config.h)
  StartScan,         // scan assíncrono pelo SSID cadastrado
};

// "connecting" | "station" | "station-lost" | "access-point"
const char* wifiModeName(WifiMode mode);

struct WifiTimings {
  uint32_t connectTimeoutMs = 0;
  uint32_t reconnectTimeoutMs = 0;
  uint32_t scanIntervalMs = 0;
};

WifiTimings defaultWifiTimings();  // valores ONIX_STA_* de config.h

class WifiPolicy {
 public:
  explicit WifiPolicy(WifiTimings timings);

  WifiAction begin(bool hasCredentials, uint64_t nowMs);

  // Chamado a cada loop(). staConnected = WiFi.status() == WL_CONNECTED; apClients =
  // WiFi.softAPgetStationNum(); scanRunning = há scan assíncrono em andamento.
  WifiAction tick(uint64_t nowMs, bool staConnected, uint8_t apClients, bool scanRunning);

  // Resultado do scan disparado por StartScan. Só troca para a STA se ainda não houver clientes.
  WifiAction onScanResult(uint64_t nowMs, bool ssidFound, uint8_t apClients);

  WifiMode mode() const { return mode_; }

 private:
  WifiTimings timings_;
  WifiMode mode_ = WifiMode::AccessPoint;
  bool hasCredentials_ = false;
  uint64_t sinceMs_ = 0;     // início de Connecting/StationLost
  uint64_t lastScanMs_ = 0;  // último StartScan (ou entrada no AP)
};

using Ipv4 = std::array<uint8_t, 4>;

// Testa se o endereço já responde na rede (ARP no ESP32). Contexto opaco do chamador.
using HostInUseProbe = bool (*)(const Ipv4& address, void* context);

// IP fixo na faixa do gateway (ADR-0012): candidatos (gateway & netmask) | (first + i), para
// i em [0, count). Descarta candidatos fora da máscara, o endereço de rede, o broadcast e o
// próprio gateway. Se `current` (IP do DHCP) já é candidato, devolve-o sem sondar. Senão, devolve
// o primeiro candidato que `inUse` diz livre; nullopt se nenhum (fica no DHCP).
std::optional<Ipv4> selectStaticHost(const Ipv4& gateway, const Ipv4& netmask, const Ipv4& current,
                                     uint8_t first, uint8_t count, HostInUseProbe inUse,
                                     void* context);

}  // namespace onix
