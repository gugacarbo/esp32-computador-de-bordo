// wifi_config.cpp — parse do corpo de /api/wifi, validação de credenciais e JSON de status
// (ADR-0012). Portátil: sem Arduino, testado no env native.
#include "wifi_config.h"

#include "flat_json.h"

#include <cstddef>
#include <string>

namespace onix {

namespace {

constexpr std::size_t SSID_MAX_BYTES = 32;
constexpr std::size_t PASSWORD_MIN_BYTES = 8;
constexpr std::size_t PASSWORD_MAX_BYTES = 63;

}  // namespace

std::optional<WifiRequestBody> parseWifiRequestBody(const std::string& body) {
  const auto object = parseFlatJsonObject(body);
  if (!object) return std::nullopt;
  WifiRequestBody parsed;
  for (const auto& [key, value] : *object) {
    if (key == "ssid") {
      parsed.ssid = value;
    } else if (key == "password") {
      parsed.password = value;
    }  // chaves desconhecidas (inclusive o antigo otaPassword) são ignoradas
  }
  return parsed;
}

bool isValidWifiCredentials(const WifiCredentials& credentials) {
  if (credentials.ssid.empty() || credentials.ssid.size() > SSID_MAX_BYTES) return false;
  if (credentials.password.empty()) return true;  // rede aberta
  return credentials.password.size() >= PASSWORD_MIN_BYTES &&
         credentials.password.size() <= PASSWORD_MAX_BYTES;
}

std::string serializeWifiStatusJson(const WifiStatus& status) {
  std::string out = "{\"mode\":\"";
  appendJsonEscaped(out, status.mode);
  out += "\",\"configured\":";
  out += status.configured ? "true" : "false";
  out += ",\"ssid\":\"";
  appendJsonEscaped(out, status.ssid);
  out += "\",\"ip\":\"";
  appendJsonEscaped(out, status.ip);
  out += "\"}";
  return out;
}

}  // namespace onix
