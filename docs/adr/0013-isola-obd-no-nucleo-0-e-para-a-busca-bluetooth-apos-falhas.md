---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0003, ADR-0004, ADR-0012] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Isola OBD no núcleo 0 e para a busca Bluetooth após falhas

## Contexto e problema

O painel ficava lento tanto no SoftAP quanto no hotspot do celular. Duas causas foram medidas ou
identificadas no código:

- **CPU:** a task OBD rodava no núcleo 1, junto do `loop()` (WebServer, WebSocket, OTA). Ela foi
  colocada lá porque o ELMduino faz espera ativa de até 30 s na busca de protocolo e, no núcleo 0,
  deixaria o `IDLE0` sem CPU, disparando o watchdog.
- **Rádio:** Wi-Fi e Bluetooth Classic dividem o mesmo rádio. Sem o scanner por perto, cada
  tentativa de conexão (busca de 5,1 s e conexão pelo MAC) derrubava o Wi-Fi por cerca de 1 s, e as
  tentativas se repetiam para sempre com backoff de até 30 s.

## Direcionadores da decisão

- Servidor web com tempo de resposta previsível, sem disputar CPU com a coleta OBD.
- Não ocupar o rádio indefinidamente quando o scanner não está presente.
- Permitir que o usuário retome a busca sem reiniciar a placa.

## Opções consideradas

### Opção 1 — manter a task OBD no núcleo 1 e o backoff infinito

**Prós:** nenhuma mudança; watchdog do `IDLE0` preservado.
**Contras:** HTTP divide CPU com a OBD; o rádio nunca para de buscar.

### Opção 2 — OBD no núcleo 0 sem watchdog do `IDLE0` e busca limitada com retomada pelo painel

**Prós:** núcleo 1 dedicado ao `loop()`; as pilhas Wi-Fi, TCP/IP e Bluetooth, de prioridade maior,
continuam interrompendo a task OBD no núcleo 0; o rádio fica livre quando o scanner não aparece.
**Contras:** sem vigilância do `IDLE0`, um travamento no núcleo 0 não reinicia a placa pelo watchdog;
se o scanner ligar depois que a busca parou, o usuário precisa pedir a reconexão.

### Opção 3 — reescrever a inicialização do ELMduino sem espera ativa

**Prós:** permitiria manter o watchdog do `IDLE0`.
**Contras:** exige substituir `begin()` da biblioteca; mais código num firmware que usa 95% do slot.

## Decisão

Adotar a Opção 2. A task OBD é criada no núcleo 0 (`ONIX_OBD_TASK_CORE = 0`) com prioridade 1, e o
watchdog do `IDLE0` é desligado (`disableCore0WDT()`). Depois de `ONIX_BT_MAX_CONNECT_ATTEMPTS`
falhas seguidas de conexão, a sessão OBD para de tentar e publica `btSearchStopped: true` no
envelope; `POST /api/obd/reconnect` (botão "Procurar scanner" no painel) ou um reboot retomam a
busca. O env `esp32-no-obd` (macro `ONIX_DISABLE_OBD`) compila o firmware sem a task OBD, só para medir
Wi-Fi e HTTP sem o rádio dividido.

## Consequências

- O contrato da SPEC-0002 ganha o campo `btSearchStopped` e a rota `POST /api/obd/reconnect`.
- No veículo, se o scanner só ligar com a ignição depois de cinco falhas, é preciso tocar em
  "Procurar scanner" ou reiniciar a placa.
- A divisão de núcleos não resolve a disputa de rádio com o scanner conectado nem a economia de
  energia do Wi-Fi exigida pela coexistência com o Bluetooth.

## Confirmação

```bash
PATH=$PWD/.venv/bin:$PATH bun run firmware:test
.venv/bin/pio run -d apps/firmware -e esp32doit-devkit-v1
.venv/bin/pio run -d apps/firmware -e esp32-no-obd
bun run test
```

Na placa, sem scanner: após cinco falhas, `/api/logs` registra a parada da busca, `/api/data`
mostra `btSearchStopped: true` e o painel exibe o botão; tocá-lo retoma as tentativas.
