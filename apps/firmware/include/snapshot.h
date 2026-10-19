// snapshot.h — tipos compartilhados entre a task OBD e a camada HTTP (ADR-0004).
// A estrutura é dado, não política: não calcula frescor, saúde nem validade; a API deriva
// esses campos a partir de uma cópia (SPEC-0002). Cabeçalho portátil: ESP32 usa FreeRTOS,
// testes nativos usam std::mutex.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define ONIX_HAS_FREERTOS_MUTEX 1
#else
#include <mutex>
#define ONIX_HAS_FREERTOS_MUTEX 0
#endif

namespace onix {

enum class SensorId { Rpm, Speed, Coolant, Throttle, Map, Voltage, Count };

// Seis sensores expostos pela API; SensorId::Count é sentinela e não é um sensor.
constexpr std::size_t SENSOR_COUNT = 6;

enum class SensorStatus { Unavailable, Unsupported, Timeout, Error, Stale, Ok };

enum class VoltageSource { Adapter, Ecu };

struct SensorReading {
  double value = 0;
  bool hasValue = false;
  uint64_t updatedAtMs = 0;
  SensorStatus lastStatus = SensorStatus::Unavailable;
  bool unsupported = false;
};

struct Snapshot {
  bool btConnected = false;
  bool adapterReady = false;
  bool hasEcuResponse = false;
  uint64_t lastEcuResponseMs = 0;
  VoltageSource voltageSource = VoltageSource::Adapter;
  std::array<SensorReading, SENSOR_COUNT> sensors{};
  bool btSearchStopped = false;  // busca Bluetooth parada após N falhas seguidas; só POST /api/obd/reconnect retoma
};

// Índice do array `sensors` para um SensorId. Não use SensorId::Count.
constexpr std::size_t sensorIndex(SensorId id) { return static_cast<std::size_t>(id); }

// Publicação protegida por mutex de duração curta. `publish` e `copy` não fazem I/O, log nem
// espera: o proprietário do mutex nunca é mantido durante comunicação OBD ou envio HTTP.
class SnapshotStore {
 public:
  SnapshotStore();
  ~SnapshotStore();

  SnapshotStore(const SnapshotStore&) = delete;
  SnapshotStore& operator=(const SnapshotStore&) = delete;

  void publish(const Snapshot& snapshot);
  Snapshot copy() const;

 private:
  Snapshot snapshot_{};
#if ONIX_HAS_FREERTOS_MUTEX
  SemaphoreHandle_t mutex_ = nullptr;
#else
  mutable std::mutex mutex_;
#endif
};

}  // namespace onix
