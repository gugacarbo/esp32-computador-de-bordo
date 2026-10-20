// bt_config.cpp — validação, parse e JSON da configuração do scanner Bluetooth (ADR-0014).
// Portátil: sem Arduino, testado no env native.
#include "bt_config.h"

#include <cstdio>

#include "config.h"
#include "flat_json.h"

namespace onix {

namespace {

constexpr std::size_t NAME_MAX_BYTES = 31;
constexpr std::size_t PIN_MAX_BYTES = 16;

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

BtScannerConfig defaultBtScannerConfig() {
  return {ONIX_BT_DEVICE_NAME, ONIX_BT_DEVICE_PIN, ONIX_BT_DEVICE_MAC};
}

bool parseBtMac(const std::string& text, std::array<uint8_t, 6>& out) {
  if (text.size() != 17) return false;
  for (std::size_t i = 0; i < 6; ++i) {
    const std::size_t at = i * 3;
    const int hi = hexDigit(text[at]);
    const int lo = hexDigit(text[at + 1]);
    if (hi < 0 || lo < 0) return false;
    if (i < 5 && text[at + 2] != ':' && text[at + 2] != '-') return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

std::string formatBtMac(const std::array<uint8_t, 6>& mac) {
  char text[18];
  std::snprintf(text, sizeof text, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
                mac[4], mac[5]);
  return text;
}

bool isValidBtScannerConfig(const BtScannerConfig& config) {
  if (config.name.size() > NAME_MAX_BYTES || config.pin.size() > PIN_MAX_BYTES) return false;
  std::array<uint8_t, 6> mac{};
  if (!config.mac.empty() && !parseBtMac(config.mac, mac)) return false;
  return !config.name.empty() || !config.mac.empty();
}

std::optional<BtScannerConfig> parseBtConfigBody(const std::string& body) {
  const auto object = parseFlatJsonObject(body);
  if (!object) return std::nullopt;
  BtScannerConfig parsed;
  for (const auto& [key, value] : *object) {
    if (key == "name") {
      parsed.name = value;
    } else if (key == "pin") {
      parsed.pin = value;
    } else if (key == "mac") {
      parsed.mac = value;
    }  // chaves desconhecidas são ignoradas
  }
  std::array<uint8_t, 6> mac{};
  if (parseBtMac(parsed.mac, mac)) parsed.mac = formatBtMac(mac);  // normaliza; inválido segue cru
  return parsed;
}

const char* btScanStateName(BtScanState state) {
  switch (state) {
    case BtScanState::Idle:
      return "idle";
    case BtScanState::Pending:
      return "pending";
    case BtScanState::Scanning:
      return "scanning";
    case BtScanState::Done:
      return "done";
    case BtScanState::Busy:
      return "busy";
    case BtScanState::Failed:
      return "failed";
  }
  return "failed";
}

std::string serializeBtConfigStatusJson(const BtConfigStatus& status) {
  std::string out = "{\"custom\":";
  out += status.custom ? "true" : "false";
  out += ",\"name\":\"";
  appendJsonEscaped(out, status.config.name);
  out += "\",\"pin\":\"";
  appendJsonEscaped(out, status.config.pin);
  out += "\",\"mac\":\"";
  appendJsonEscaped(out, status.config.mac);
  out += "\",\"scan\":{\"state\":\"";
  out += btScanStateName(status.scan.state);
  out += "\",\"devices\":[";
  for (std::size_t i = 0; i < status.scan.devices.size(); ++i) {
    const BtDevice& device = status.scan.devices[i];
    if (i > 0) out += ',';
    out += "{\"name\":\"";
    appendJsonEscaped(out, device.name);
    out += "\",\"mac\":\"";
    appendJsonEscaped(out, device.mac);
    out += "\",\"rssi\":";
    out += device.hasRssi ? std::to_string(device.rssi) : "null";
    out += '}';
  }
  out += "]}}";
  return out;
}

}  // namespace onix
