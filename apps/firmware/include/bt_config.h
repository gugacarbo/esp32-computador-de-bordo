// bt_config.h — configuração do scanner Bluetooth (nome, PIN, MAC) e busca de dispositivos usadas
// por /api/obd/config e /api/obd/scan (ADR-0014). Cabeçalho portátil: validação, parse e JSON são
// testados no env native; a NVS e a busca real ficam em main.cpp/obd_transport_esp32.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace onix {

struct BtScannerConfig {
  std::string name;  // nome anunciado pelo scanner (0–31 bytes); usado na busca quando não há MAC
  std::string pin;   // PIN de pareamento (0–16 bytes); vazio mantém o PIN atual da pilha
  std::string mac;   // "" ou "AA:BB:CC:DD:EE:FF"; com MAC a conexão dispensa a busca
};

// Padrões de config.h (ONIX_BT_DEVICE_NAME, _PIN, _MAC).
BtScannerConfig defaultBtScannerConfig();

// Regras acima; exige nome ou MAC.
bool isValidBtScannerConfig(const BtScannerConfig& config);

// "aa:bb:cc:dd:ee:ff" ou "AA-BB-..." -> bytes. Só aceita 6 pares hex separados por ':' ou '-'.
bool parseBtMac(const std::string& text, std::array<uint8_t, 6>& out);
std::string formatBtMac(const std::array<uint8_t, 6>& mac);  // "AA:BB:CC:DD:EE:FF"

// Corpo de POST /api/obd/config: {"name":"...","pin":"...","mac":"..."}; chaves ausentes viram "",
// desconhecidas são ignoradas. O MAC volta normalizado em maiúsculas com ':'. nullopt = JSON
// inválido.
std::optional<BtScannerConfig> parseBtConfigBody(const std::string& body);

struct BtDevice {
  std::string name;  // "" quando o dispositivo não anunciou nome na busca
  std::string mac;   // "AA:BB:CC:DD:EE:FF"
  int rssi = 0;
  bool hasRssi = false;
};

// idle: nunca buscou; pending: pedido aceito, aguardando a task OBD; scanning: buscando;
// done: resultado em `devices`; busy: scanner conectado (a busca exige desconectar); failed: a pilha
// recusou a busca.
enum class BtScanState : uint8_t { Idle, Pending, Scanning, Done, Busy, Failed };
const char* btScanStateName(BtScanState state);

struct BtScanResult {
  BtScanState state = BtScanState::Idle;
  std::vector<BtDevice> devices;
};

// Estado de GET /api/obd/config.
struct BtConfigStatus {
  bool custom = false;     // true = configuração gravada na NVS; false = padrões de config.h
  BtScannerConfig config;  // configuração em uso
  BtScanResult scan;
};

// {"custom":false,"name":"OBDII","pin":"1234","mac":"","scan":{"state":"done","devices":[
//  {"name":"OBDII","mac":"AA:BB:CC:DD:EE:FF","rssi":-60}]}} — "rssi" é null sem medida.
std::string serializeBtConfigStatusJson(const BtConfigStatus& status);

// Implementada em main.cpp (NVS + task OBD); fakes nos testes.
class BtConfigPort {
 public:
  virtual ~BtConfigPort() = default;
  virtual BtConfigStatus status() const = 0;
  // Grava na NVS e aplica sem reiniciar (a task OBD reconecta). false = falha na NVS.
  virtual bool save(const BtScannerConfig& config) = 0;
  // Apaga da NVS e volta aos padrões. false = falha na NVS.
  virtual bool clear() = 0;
  // Pede uma busca à task OBD. false = já há busca pendente ou em andamento.
  virtual bool startScan() = 0;
};

}  // namespace onix
