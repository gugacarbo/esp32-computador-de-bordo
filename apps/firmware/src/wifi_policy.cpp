// wifi_policy.cpp — decisão de rede do firmware (ADR-0012): máquina de estados STA/AP e escolha
// do IP fixo na faixa do gateway. Portátil: sem includes Arduino, roda no env native.
#include "wifi_policy.h"

#include "config.h"

namespace onix {

namespace {

// Endereço IPv4 como inteiro big-endian (a.b.c.d -> (a<<24)|(b<<16)|(c<<8)|d).
uint32_t toU32(const Ipv4& address) {
  return (static_cast<uint32_t>(address[0]) << 24) | (static_cast<uint32_t>(address[1]) << 16) |
         (static_cast<uint32_t>(address[2]) << 8) | static_cast<uint32_t>(address[3]);
}

Ipv4 fromU32(uint32_t value) {
  return Ipv4{static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
              static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
}

// Tempo decorrido desde `since`; relógio que retrocede conta como zero.
uint64_t elapsedSince(uint64_t nowMs, uint64_t sinceMs) {
  return nowMs > sinceMs ? nowMs - sinceMs : 0;
}

}  // namespace

const char* wifiModeName(WifiMode mode) {
  switch (mode) {
    case WifiMode::Connecting:
      return "connecting";
    case WifiMode::Station:
      return "station";
    case WifiMode::StationLost:
      return "station-lost";
    case WifiMode::AccessPoint:
      return "access-point";
  }
  return "access-point";
}

WifiTimings defaultWifiTimings() {
  WifiTimings timings;
  timings.connectTimeoutMs = ONIX_STA_CONNECT_TIMEOUT_MS;
  timings.reconnectTimeoutMs = ONIX_STA_RECONNECT_TIMEOUT_MS;
  timings.scanIntervalMs = ONIX_STA_SCAN_INTERVAL_MS;
  return timings;
}

WifiPolicy::WifiPolicy(WifiTimings timings) : timings_(timings) {}

WifiAction WifiPolicy::begin(bool hasCredentials, uint64_t nowMs) {
  hasCredentials_ = hasCredentials;
  if (!hasCredentials_) {
    mode_ = WifiMode::AccessPoint;
    lastScanMs_ = nowMs;
    return WifiAction::StartAccessPoint;
  }
  mode_ = WifiMode::Connecting;
  sinceMs_ = nowMs;
  return WifiAction::StartStation;
}

WifiAction WifiPolicy::tick(uint64_t nowMs, bool staConnected, uint8_t apClients,
                            bool scanRunning) {
  switch (mode_) {
    case WifiMode::Connecting:
      if (staConnected) {
        mode_ = WifiMode::Station;
        return WifiAction::StationUp;
      }
      if (elapsedSince(nowMs, sinceMs_) >= timings_.connectTimeoutMs) {
        mode_ = WifiMode::AccessPoint;
        lastScanMs_ = nowMs;
        return WifiAction::StartAccessPoint;
      }
      return WifiAction::None;

    case WifiMode::Station:
      if (!staConnected) {
        mode_ = WifiMode::StationLost;
        sinceMs_ = nowMs;
        return WifiAction::StationDown;
      }
      return WifiAction::None;

    case WifiMode::StationLost:
      if (staConnected) {
        mode_ = WifiMode::Station;
        return WifiAction::StationUp;
      }
      if (elapsedSince(nowMs, sinceMs_) >= timings_.reconnectTimeoutMs) {
        mode_ = WifiMode::AccessPoint;
        lastScanMs_ = nowMs;
        return WifiAction::StartAccessPoint;
      }
      return WifiAction::None;

    case WifiMode::AccessPoint:
      if (hasCredentials_ && apClients == 0 && !scanRunning &&
          elapsedSince(nowMs, lastScanMs_) >= timings_.scanIntervalMs) {
        lastScanMs_ = nowMs;
        return WifiAction::StartScan;
      }
      return WifiAction::None;
  }
  return WifiAction::None;
}

WifiAction WifiPolicy::onScanResult(uint64_t nowMs, bool ssidFound, uint8_t apClients) {
  if (mode_ != WifiMode::AccessPoint || !ssidFound || apClients != 0) {
    return WifiAction::None;
  }
  mode_ = WifiMode::Connecting;
  sinceMs_ = nowMs;
  return WifiAction::StartStation;
}

std::optional<Ipv4> selectStaticHost(const Ipv4& gateway, const Ipv4& netmask, const Ipv4& current,
                                     uint8_t first, uint8_t count, HostInUseProbe inUse,
                                     void* context) {
  const uint32_t gatewayValue = toU32(gateway);
  const uint32_t mask = toU32(netmask);
  const uint32_t network = gatewayValue & mask;
  const uint32_t hostMask = ~mask;
  const uint32_t broadcast = network | hostMask;

  // Candidato i válido: offset cabe na máscara e o endereço não é rede, broadcast nem gateway.
  auto validCandidate = [&](uint32_t offset, uint32_t& candidate) {
    if ((offset & ~hostMask) != 0) {
      return false;
    }
    candidate = network | offset;
    return candidate != network && candidate != broadcast && candidate != gatewayValue;
  };

  // Primeiro, o IP atual do DHCP: se já é candidato, fica nele sem sondar a rede.
  const uint32_t currentValue = toU32(current);
  for (uint16_t i = 0; i < count; ++i) {
    uint32_t candidate = 0;
    if (validCandidate(static_cast<uint32_t>(first) + i, candidate) && candidate == currentValue) {
      return current;
    }
  }

  for (uint16_t i = 0; i < count; ++i) {
    uint32_t candidate = 0;
    if (!validCandidate(static_cast<uint32_t>(first) + i, candidate)) {
      continue;
    }
    const Ipv4 address = fromU32(candidate);
    if (inUse == nullptr || !inUse(address, context)) {
      return address;
    }
  }
  return std::nullopt;
}

}  // namespace onix
