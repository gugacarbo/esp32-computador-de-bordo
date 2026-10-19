// obd.cpp — implementação da coleta OBD (SPEC-0001, ADR-0003, ADR-0004).
//
// A máquina de sessão avança UMA operação bloqueante por step(): conexão, inicialização,
// leitura de mapa de suporte ou consulta de sensor. A publicação do snapshot acontece sempre
// DEPOIS da chamada de transporte retornar, nunca durante (ADR-0004).
//
// Escalonador: sensor com menor nextDueMs vence (empate -> menor índice, RPM primeiro).
// Vencido atrasado além do próprio intervalo é reancorado no instante atual: sem rajada
// após pausa (SPEC-0001 caso 8) e sem starvation com adaptador lento (caso 8/11).

#include "obd.h"

namespace onix {

ObdSession::ObdSession(const ObdRuntime& runtime, SnapshotPublisher& publisher)
    : runtime_(runtime), publisher_(&publisher) {
  snapshot_.voltageSource = runtime_.config.voltageSource;
}

uint64_t ObdSession::nowMs() const {
  return runtime_.clock != nullptr ? runtime_.clock(runtime_.clockContext) : 0;
}

// --- logs ---

void ObdSession::logTransition(const char* line) {
  if (runtime_.log == nullptr || line == nullptr) {
    return;
  }
  const uint64_t now = nowMs();
  if (logWindowOpen_ && lastLogLine_ != nullptr &&
      now - static_cast<uint64_t>(lastLogMs_) < ONIX_OBD_LOG_REPEAT_WINDOW_MS &&
      std::strcmp(lastLogLine_, line) == 0) {
    return;  // mesma mensagem dentro da janela: sem spam (SPEC-0001 §Polling)
  }
  logWindowOpen_ = true;
  lastLogMs_ = static_cast<uint32_t>(now);
  lastLogLine_ = line;  // sempre um literal estático
  runtime_.log->log(ObdLogLevel::Transition, line);
}

void ObdSession::transition(ObdPhase phase, const char* line) {
  phase_ = phase;
  logTransition(line);
}

void ObdSession::logProtocol() {
  if (runtime_.log == nullptr) {
    return;
  }
  const char* protocol = transport().protocol();
  char line[80];
  std::snprintf(line, sizeof(line), "[obd] protocolo detectado: %s",
                protocol != nullptr ? protocol : "?");
  runtime_.log->log(ObdLogLevel::Transition, line);
}

void ObdSession::publish() {
  if (publisher_ != nullptr) {
    publisher_->publish(snapshot_);
  }
}

// --- saúde da ECU e tempos ---

void ObdSession::recordEcuOk(uint64_t now) {
  lastEcuOkMs_ = now;
  snapshot_.lastEcuResponseMs = now;  // a API deriva a saúde a partir deste carimbo (SPEC-0002)
  snapshot_.hasEcuResponse = true;
}

void ObdSession::refreshEcuHealth(uint64_t now) {
  if (lastEcuOkMs_ != 0 && now >= lastEcuOkMs_ && now - lastEcuOkMs_ <= timing().ecuHealthMs) {
    snapshot_.hasEcuResponse = true;
  } else {
    snapshot_.hasEcuResponse = false;
  }
}

void ObdSession::armAction(uint64_t now, uint32_t waitMs) {
  recoveryWaitMs_ = waitMs;
  nextAttemptMs_ = now + waitMs;
}

uint32_t ObdSession::nextRecoveryWait() const {
  const uint32_t cap = timing().recoveryMaxMs;
  if (recoveryWaitMs_ == 0) {
    return timing().recoveryInitialMs;
  }
  if (recoveryWaitMs_ >= cap) {
    return cap;
  }
  const uint64_t doubled = static_cast<uint64_t>(recoveryWaitMs_) * 2u;
  return doubled >= cap ? cap : static_cast<uint32_t>(doubled);
}

// --- sessão ---

bool ObdSession::beginSession(uint64_t now) {
  if (!transport().connect(runtime_.config.btConnectTimeoutMs)) {
    logTransition("[bt] falha ao conectar ao scanner");
    const uint8_t maxAttempts = runtime_.config.btMaxConnectAttempts;
    if (maxAttempts > 0 && ++connectFailures_ >= maxAttempts) {
      // Scanner ausente: para de ligar o rádio (cada busca degrada o Wi-Fi) até pedido explícito.
      snapshot_.btSearchStopped = true;
      char line[96];
      std::snprintf(line, sizeof(line), "[bt] busca parada apos %u tentativas; reconecte pelo painel",
                    static_cast<unsigned>(maxAttempts));
      if (runtime_.log != nullptr) {
        runtime_.log->log(ObdLogLevel::Quiet, line);  // sem dedupe: uma vez por parada
      }
      return false;
    }
    armAction(now, nextRecoveryWait());  // `now` pré-chamada: espera exata do backoff
    return false;
  }
  connectFailures_ = 0;
  snapshot_.btConnected = true;
  mapBaseIndex_ = 0;
  for (std::size_t i = 0; i < OBD_SUPPORT_MAP_COUNT; ++i) {
    mapBits_[i] = 0;
    mapOk_[i] = false;
    mapAttempts_[i] = 0;
  }
  transition(ObdPhase::Initializing, "[bt] conectado ao scanner");
  return true;
}

void ObdSession::requestReconnect() {
  connectFailures_ = 0;
  snapshot_.btSearchStopped = false;
  recoveryWaitMs_ = 0;
  nextAttemptMs_ = nowMs();  // próxima tentativa liberada já (vale só se estiver desconectada)
  logTransition("[bt] reconexao solicitada");
  publish();
}

void ObdSession::adapterReady(uint64_t now) {
  if (!transport().adapterInit(runtime_.config.adapterInitTimeoutMs)) {
    // Bluetooth de pé mas adaptador sem inicializar (ex.: carro desligado): a próxima tentativa
    // segue o backoff crescente, sem reconexão Bluetooth incessante (SPEC-0001 §Recuperação).
    invalidateSession(now, "[bt] inicializacao do adaptador falhou", nextRecoveryWait());
    return;
  }
  snapshot_.adapterReady = true;
  mapBaseIndex_ = 0;
  for (std::size_t i = 0; i < OBD_SUPPORT_MAP_COUNT; ++i) {
    mapBits_[i] = 0;
    mapOk_[i] = false;
    mapAttempts_[i] = 0;
  }
  transition(ObdPhase::ReadingSupport, "[adapter] pronto");
  logProtocol();
}

void ObdSession::invalidateSession(uint64_t now, const char* reason, uint32_t waitMs) {
  snapshot_.btConnected = false;
  snapshot_.adapterReady = false;
  snapshot_.hasEcuResponse = false;
  support_.fill(ObdSupport::Unknown);
  // Suporte volta a desconhecido também no snapshot: a nova sessão reavalia (SPEC-0001 §Suporte).
  for (SensorReading& reading : snapshot_.sensors) {
    reading.unsupported = false;
    if (reading.lastStatus == SensorStatus::Unsupported) {
      reading.lastStatus = SensorStatus::Unavailable;
    }
  }
  nextDueMs_.fill(0);
  mapBaseIndex_ = 0;
  for (std::size_t i = 0; i < OBD_SUPPORT_MAP_COUNT; ++i) {
    mapBits_[i] = 0;
    mapOk_[i] = false;
    mapAttempts_[i] = 0;
  }
  lastEcuOkMs_ = 0;
  snapshot_.lastEcuResponseMs = 0;
  // Perda de transporte (waitMs = 0) permite tentativa imediata; o backoff crescente arma-se se a
  // reconexão falhar (SPEC-0001 casos 6–7: recolocar o scanner retoma sem espera artificial).
  armAction(now, waitMs);
  transition(ObdPhase::Disconnected, reason);
}

// --- mapas de suporte ---

bool ObdSession::needsBase(uint8_t base) const {
  const bool voltageFromAdapter = runtime_.config.voltageSource == VoltageSource::Adapter;
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    if (voltageFromAdapter && i == sensorIndex(SensorId::Voltage)) {
      continue;  // tensão via AT RV não depende do mapa 0x40 (SPEC-0001 §Sensores)
    }
    if (support_[i] != ObdSupport::Unknown) {
      continue;
    }
    if (obdSupportMapBase(OBD_SENSOR_PIDS[i].pid) == base) {
      return true;
    }
  }
  return false;
}

void ObdSession::markSupport(uint8_t base, uint32_t bits) {
  const bool voltageFromAdapter = runtime_.config.voltageSource == VoltageSource::Adapter;
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    if (voltageFromAdapter && i == sensorIndex(SensorId::Voltage)) {
      continue;
    }
    if (support_[i] != ObdSupport::Unknown) {
      continue;
    }
    if (obdSupportMapBase(OBD_SENSOR_PIDS[i].pid) != base) {
      continue;
    }
    const bool declared = obdMapDeclaresPid(bits, OBD_SENSOR_PIDS[i].pid);
    support_[i] = declared ? ObdSupport::Supported : ObdSupport::Unsupported;
    if (!declared) {
      snapshot_.sensors[i].unsupported = true;  // conserva eventual leitura anterior
      snapshot_.sensors[i].lastStatus = SensorStatus::Unsupported;
    }
  }
}

void ObdSession::finishSupport(uint64_t now, bool complete) {
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    nextDueMs_[i] = now;  // todos elegíveis vencem já; empate resolve pelo menor índice
  }
  transition(ObdPhase::Polling,
             complete ? "[obd] polling iniciado" : "[obd] polling sem mapa completo");
}

// --- escalonador ---

int ObdSession::pickDueSensor(uint64_t now) const {
  int best = -1;
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    if (support_[i] == ObdSupport::Unsupported) {
      continue;  // fora do polling da sessão (SPEC-0001 caso 4)
    }
    if (nextDueMs_[i] > now) {
      continue;
    }
    if (best < 0 || nextDueMs_[i] < nextDueMs_[static_cast<std::size_t>(best)]) {
      best = static_cast<int>(i);
    }
  }
  return best;
}

uint32_t ObdSession::idleWait(uint64_t now) const {
  if (snapshot_.btSearchStopped) {
    return ONIX_OBD_STOPPED_IDLE_MS;  // busca parada: nada a agendar até requestReconnect
  }
  // No Polling, sensor vencido e ainda não servido significa trabalho pendente: o próximo
  // step deve ser imediato (uma transação por step — SPEC-0001 §Polling). Sem isso, a espera
  // de até 200 ms entre vencidos atrasaria a primeira volta completa dos sensores.
  if (phase_ == ObdPhase::Polling) {
    for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
      if (support_[i] == ObdSupport::Unsupported) {
        continue;
      }
      if (nextDueMs_[i] <= now) {
        return 1;
      }
    }
  }
  uint64_t wait = ONIX_OBD_IDLE_WAIT_MS;
  if (phase_ == ObdPhase::Disconnected && nextAttemptMs_ > now && nextAttemptMs_ - now < wait) {
    wait = nextAttemptMs_ - now;
  }
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    if (support_[i] == ObdSupport::Unsupported) {
      continue;
    }
    if (nextDueMs_[i] > now && nextDueMs_[i] - now < wait) {
      wait = nextDueMs_[i] - now;
    }
  }
  if (wait < 1) {
    wait = 1;
  }
  if (wait > ONIX_OBD_IDLE_WAIT_MS) {
    wait = ONIX_OBD_IDLE_WAIT_MS;
  }
  return static_cast<uint32_t>(wait);
}

uint32_t ObdSession::minIntervalMs() const {
  uint32_t minimum = ONIX_OBD_IDLE_WAIT_MS;
  for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
    if (support_[i] == ObdSupport::Unsupported) {
      continue;
    }
    if (timing().intervalsMs[i] < minimum) {
      minimum = timing().intervalsMs[i];
    }
  }
  return minimum;
}

// --- máquina ---

uint32_t ObdSession::step() {
  const uint64_t now = nowMs();
  switch (phase_) {
    case ObdPhase::Disconnected: {
      if (snapshot_.btSearchStopped) {
        break;  // busca parada: não chama connect até requestReconnect
      }
      if (now >= nextAttemptMs_) {
        beginSession(now);
      }
      break;
    }

    case ObdPhase::Initializing: {
      adapterReady(now);
      break;
    }

    case ObdPhase::ReadingSupport: {
      bool acted = false;
      for (std::size_t b = 0; b < OBD_SUPPORT_MAP_COUNT && !acted; ++b) {
        const uint8_t base = OBD_SUPPORT_MAP_BASES[b];
        if (mapOk_[b] || mapAttempts_[b] >= ONIX_OBD_SUPPORT_MAP_MAX_ATTEMPTS || !needsBase(base)) {
          continue;
        }
        acted = true;  // uma transação por step
        const ObdReply reply = transport().readSupportMap(base, timing().obdTimeoutMs);
        ++mapAttempts_[b];
        if (reply.outcome == ObdOutcome::Disconnected) {
          invalidateSession(now, "[bt] transporte perdido na leitura do mapa");
          break;
        }
        if (reply.outcome == ObdOutcome::Ok && reply.hasBits) {
          mapBits_[b] = reply.bits;
          mapOk_[b] = true;
          markSupport(base, reply.bits);
          recordEcuOk(now);  // mapa é consulta Serviço 01: prova ECU viva
        }
        // Timeout/NO DATA/erro: tentativa consumida, suporte segue Unknown (caso 5)
      }
      if (phase_ == ObdPhase::ReadingSupport) {
        bool remaining = false;
        for (std::size_t b = 0; b < OBD_SUPPORT_MAP_COUNT; ++b) {
          const uint8_t base = OBD_SUPPORT_MAP_BASES[b];
          if (!mapOk_[b] && mapAttempts_[b] < ONIX_OBD_SUPPORT_MAP_MAX_ATTEMPTS && needsBase(base)) {
            remaining = true;
            break;
          }
        }
        if (!remaining) {
          bool complete = true;
          const bool voltageFromAdapter =
              runtime_.config.voltageSource == VoltageSource::Adapter;
          for (std::size_t i = 0; i < SENSOR_COUNT; ++i) {
            if (voltageFromAdapter && i == sensorIndex(SensorId::Voltage)) {
              continue;
            }
            if (support_[i] == ObdSupport::Unknown) {
              complete = false;
              break;
            }
          }
          finishSupport(now, complete);
        }
      }
      break;
    }

    case ObdPhase::Polling: {
      // Sem reancoramento: pickDueSensor escolhe o menor nextDueMs ABSOLUTO, ou seja, o
      // sensor lido há mais tempo — rotação justa com adaptador lento (SPEC-0001 caso 8) e
      // prioridade RPM apenas no empate exato (retomada). A anti-rajada vem do contrato
      // "uma transação por step": vencidos atrasados são servidos um por passo, nunca em
      // disparada dentro de um único step.
      const int due = pickDueSensor(now);
      if (due < 0) {
        break;  // nada vencido: espera cooperativa
      }
      const std::size_t idx = static_cast<std::size_t>(due);
      const SensorPidSpec& spec = OBD_SENSOR_PIDS[idx];
      const bool adapterVoltage = idx == sensorIndex(SensorId::Voltage) &&
                                  runtime_.config.voltageSource == VoltageSource::Adapter;
      lastTransactionSensorIndex_ = static_cast<int>(idx);
      ++transactionsStarted_;
      ++transactionsTotal_;
      const ObdReply reply = adapterVoltage
                                 ? transport().readAdapterVoltage(timing().obdTimeoutMs)
                                 : transport().readPid(spec.pid, timing().obdTimeoutMs);
      SensorReading& reading = snapshot_.sensors[idx];
      if (reply.outcome == ObdOutcome::Ok) {
        reading.value = reply.value;  // transporte já decodifica; zero é leitura válida (caso 9)
        reading.hasValue = true;
        reading.updatedAtMs = now;
        reading.lastStatus = SensorStatus::Ok;
        if (!adapterVoltage) {
          recordEcuOk(now);  // AT RV não comprova a ECU (SPEC-0001 §Estado)
        }
        nextDueMs_[idx] = now + timing().intervalsMs[idx];
      } else if (reply.outcome == ObdOutcome::NoData || reply.outcome == ObdOutcome::Timeout) {
        reading.lastStatus = SensorStatus::Timeout;  // conserva valor e timestamp (caso 5)
        nextDueMs_[idx] = now + timing().intervalsMs[idx];
      } else if (reply.outcome == ObdOutcome::Error) {
        reading.lastStatus = SensorStatus::Error;
        nextDueMs_[idx] = now + timing().intervalsMs[idx];
      } else {
        invalidateSession(now, "[bt] transporte perdido durante consulta");  // caso 6
      }
      break;
    }
  }

  const uint64_t after = nowMs();  // pós-chamada: latência simulada pode ter avançado o relógio
  refreshEcuHealth(after);
  publish();  // fora de qualquer chamada bloqueante (ADR-0004)
  return idleWait(after);
}

}  // namespace onix
