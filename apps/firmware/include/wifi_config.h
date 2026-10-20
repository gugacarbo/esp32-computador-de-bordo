// wifi_config.h — credenciais do hotspot e porta de configuração usada por /api/wifi (ADR-0012).
// Cabeçalho portátil: a validação e o parse do corpo JSON são testados no env native; a
// persistência na NVS (Preferences) fica em main.cpp, que implementa WifiConfigPort.
#pragma once

#include <optional>
#include <string>

namespace onix {

struct WifiCredentials {
  std::string ssid;      // 1–32 bytes
  std::string password;  // vazio (rede aberta) ou 8–63 bytes (WPA2)
};

// Estado exposto por GET /api/wifi. Nunca inclui a senha.
struct WifiStatus {
  std::string mode;  // wifiModeName(): "connecting" | "station" | "station-lost" | "access-point"
  bool configured = false;
  std::string ssid;  // SSID cadastrado ("" sem credencial)
  std::string ip;    // IP atual da interface ativa ("" se nenhum)
};

// Implementada em main.cpp (NVS + reinício agendado); fakes nos testes.
class WifiConfigPort {
 public:
  virtual ~WifiConfigPort() = default;
  virtual WifiStatus status() const = 0;
  // Grava e agenda o reinício (a resposta HTTP sai antes). false = falha na NVS.
  virtual bool save(const WifiCredentials& credentials) = 0;
  // Apaga e agenda o reinício. false = falha na NVS.
  virtual bool clear() = 0;
};

// Corpo de POST /api/wifi: {"ssid":"...","password":"..."} (ADR-0014: sem senha do OTA).
// Objeto JSON plano só com strings (flat_json.h); chaves desconhecidas são ignoradas; chave
// ausente vira "". nullopt = JSON inválido.
struct WifiRequestBody {
  std::string ssid;
  std::string password;
};
std::optional<WifiRequestBody> parseWifiRequestBody(const std::string& body);

// Regras de WifiCredentials acima.
bool isValidWifiCredentials(const WifiCredentials& credentials);

// {"mode":"station","configured":true,"ssid":"...","ip":"192.168.43.50"} com strings escapadas.
std::string serializeWifiStatusJson(const WifiStatus& status);

}  // namespace onix
