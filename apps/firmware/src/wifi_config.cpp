// wifi_config.cpp — parse do corpo de /api/wifi, validação de credenciais e JSON de status
// (ADR-0012). Portátil: sem Arduino, testado no env native.
#include "wifi_config.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace onix {

namespace {

constexpr std::size_t SSID_MAX_BYTES = 32;
constexpr std::size_t PASSWORD_MIN_BYTES = 8;
constexpr std::size_t PASSWORD_MAX_BYTES = 63;

void skipWhitespace(const std::string& s, std::size_t& i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
    ++i;
  }
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Codifica um ponto de código (BMP) em UTF-8. Surrogates isolados são codificados como estão.
void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Lê uma string JSON que começa em s[i] == '"'. Ao sucesso, i fica logo após a aspa final.
std::optional<std::string> parseJsonString(const std::string& s, std::size_t& i) {
  if (i >= s.size() || s[i] != '"') return std::nullopt;
  ++i;
  std::string out;
  while (i < s.size()) {
    const char c = s[i++];
    if (c == '"') return out;
    // JSON não aceita controles crus dentro de string; precisam vir escapados.
    if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
    if (c != '\\') {
      out += c;
      continue;
    }
    if (i >= s.size()) return std::nullopt;
    const char escape = s[i++];
    switch (escape) {
      case '"':
      case '\\':
      case '/':
        out += escape;
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        if (i + 4 > s.size()) return std::nullopt;
        uint32_t cp = 0;
        for (std::size_t k = 0; k < 4; ++k) {
          const int digit = hexValue(s[i + k]);
          if (digit < 0) return std::nullopt;
          cp = cp * 16 + static_cast<uint32_t>(digit);
        }
        i += 4;
        appendUtf8(out, cp);
        break;
      }
      default:
        return std::nullopt;
    }
  }
  return std::nullopt;  // aspa final ausente
}

// Acrescenta `text` escapando aspas, barra invertida e controles < 0x20 como \u00XX.
void appendJsonEscaped(std::string& out, const std::string& text) {
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c < 0x20) {
      char escaped[7];
      std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
      out += escaped;
    } else {
      out += raw;
    }
  }
}

}  // namespace

std::optional<WifiRequestBody> parseWifiRequestBody(const std::string& body) {
  std::size_t i = 0;
  skipWhitespace(body, i);
  if (i >= body.size() || body[i] != '{') return std::nullopt;
  ++i;

  WifiRequestBody parsed;
  skipWhitespace(body, i);
  if (i < body.size() && body[i] == '}') {
    ++i;  // objeto vazio: todos os campos ficam ""
  } else {
    while (true) {
      skipWhitespace(body, i);
      const auto key = parseJsonString(body, i);
      if (!key) return std::nullopt;
      skipWhitespace(body, i);
      if (i >= body.size() || body[i] != ':') return std::nullopt;
      ++i;
      skipWhitespace(body, i);
      const auto value = parseJsonString(body, i);
      if (!value) return std::nullopt;

      if (*key == "ssid") {
        parsed.ssid = *value;
      } else if (*key == "password") {
        parsed.password = *value;
      } else if (*key == "otaPassword") {
        parsed.otaPassword = *value;
      }  // chaves desconhecidas são ignoradas

      skipWhitespace(body, i);
      if (i < body.size() && body[i] == ',') {
        ++i;
        continue;
      }
      if (i < body.size() && body[i] == '}') {
        ++i;
        break;
      }
      return std::nullopt;
    }
  }

  skipWhitespace(body, i);
  if (i != body.size()) return std::nullopt;  // lixo depois do objeto
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
