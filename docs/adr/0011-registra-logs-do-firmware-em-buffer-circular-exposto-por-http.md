---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0004, ADR-0009] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Registra logs do firmware em buffer circular exposto por HTTP

## Contexto e problema

O firmware só escrevia `Serial.printf` sem nível. No veículo não há cabo USB nem alguém olhando a
serial, e quedas de tensão ou watchdog reiniciam a placa sem deixar rastro. Faltava ler o que
aconteceu pelo celular conectado ao SoftAP e saber por que a placa reiniciou.

## Direcionadores da decisão

- Ler logs recentes pela rede local, sem cabo e sem internet.
- Memória fixa e previsível; nenhuma alocação nem I/O no caminho de log da task OBD (ADR-0004).
- Pouca flash: o firmware usa 94,5% do slot OTA (ADR-0009).
- Registrar o motivo de cada reset e métricas de heap/pilha para achar vazamentos.

## Opções consideradas

### Opção 1 — só Serial com níveis (`log_x`/`ESP_LOGx`)

**Prós:** sem código novo; níveis removidos em compilação.
**Contras:** continua exigindo cabo USB.

### Opção 2 — buffer circular em RAM exposto por `GET /api/logs`

**Prós:** leitura pelo painel no celular; memória fixa; testável nativamente como o `ApiRouter`.
**Contras:** o conteúdo se perde em reset; consome cerca de 9 KB de RAM.

### Opção 3 — arquivo no LittleFS com rotação

**Prós:** sobrevive a reset.
**Contras:** desgasta a flash e corrompe linhas em corte de energia; exige rotação e lote.

## Decisão

Adotar a Opção 2. `log.h` define níveis (`error`, `warn`, `info`, `debug`), o filtro
`ONIX_LOG_MIN_LEVEL` e um `LogBuffer` de `ONIX_LOG_CAPACITY` entradas com texto truncado, protegido
por mutex curto. Cada linha vai ao Serial e ao buffer; `GET /api/logs` devolve o buffer em JSON e o
painel web mostra uma seção "Diagnóstico". No boot, o firmware registra `esp_reset_reason()` e um
contador de boots na NVS; a cada `ONIX_METRICS_INTERVAL_MS` registra heap livre, heap mínimo, maior
bloco, pilha livre da task OBD e uptime.

## Consequências

- O contrato da SPEC-0002 ganha a rota `GET /api/logs`, com as mesmas regras de 404/405.
- O buffer ocupa cerca de 9 KB de RAM fixos; ao encher, sobrescreve a entrada mais antiga e conta
  as perdas em `dropped`.
- Os logs anteriores a um reset se perdem; o motivo do reset e o contador de boots continuam
  visíveis no boot seguinte.
- Persistir avisos e erros no LittleFS (Opção 3) fica para depois, se o buffer e o motivo do
  reset não bastarem.

## Confirmação

```bash
PATH=$PWD/.venv/bin:$PATH bun run firmware:test
PATH=$PWD/.venv/bin:$PATH bun run firmware:build
bun run test
```

Na placa: `GET http://192.168.4.1/api/logs` deve listar o boot com o motivo do reset e, a cada 30 s,
as métricas de memória; a seção "Diagnóstico" do painel deve mostrar as mesmas entradas.
