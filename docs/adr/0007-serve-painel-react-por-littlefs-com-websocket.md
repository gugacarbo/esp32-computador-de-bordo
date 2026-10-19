---
status: superseded
date: 2026-10-15
builds-on: [ADR-0004, ADR-0005, ADR-0006]
superseded-by: ADR-0008
deciders: ["project maintainer"]
---

> ⚠️ VERDADE ATUAL: LittleFS, WebSocket e deploy local permanecem; o bundle singlefile foi substituído por assets separados e gzip na ADR-0008.

# Serve painel React por LittleFS com WebSocket

## Contexto e problema

O painel legado (`apps/firmware/include/dashboard.h`: HTML/JS em literal PROGMEM com polling
HTTP de 500 ms) cumpre a SPEC-0003, mas trava a evolução da interface: qualquer ajuste visual
exige editar C++ e recompilar o firmware, e o polling periódico desperdiça requisições sem dado
novo. A própria ADR-0005 previu a reavaliação ("uma necessidade futura demonstrada de WebSocket
ou filesystem requer reavaliar esta escolha"). O mantenedor decidiu por um painel React+Vite com
entrega por evento (WebSocket) e filesystem, mantendo operação offline na mesma origem e sem
canal de escrita no veículo.

## Direcionadores da decisão

- Servir conteúdo e dados no próprio ESP32, sem computador externo e sem internet.
- Interface em React com build enxuto que caiba no flash do ESP32 DOIT junto ao firmware.
- Entregar snapshots por evento (push), com fallback para o polling HTTP já validado.
- Manter o contrato do envelope JSON da SPEC-0002 e os dois recursos HTTP existentes.
- Publicar painel + firmware na placa em um comando único a partir da raiz do repo.

## Opções consideradas

### Manter painel PROGMEM com polling HTTP

**Prós:** sem etapa de filesystem, sem conexão persistente, tudo já validado em hardware de teste.
**Contras:** UI presa ao C++, sem push por evento, custo de manutenção da interface em literal bruto.

### Usar painel React servido do LittleFS com WebSocket na porta 81, mantendo GET /api/data

**Prós:** UI moderna e testável em Bun, push a 500 ms sem polling sobreposto, HTTP preservado como
fallback e depuração, fallback embarcado quando o filesystem está vazio.
**Contras:** exige etapa separada de gravação do filesystem (`uploadfs`) e gerencia broadcast
periódico com alocação de String a 2 Hz.

### Migrar o HTTP para ESPAsyncWebServer com WebSocket na mesma porta

**Prós:** WS e HTTP na porta 80, servidor assíncrono.
**Contras:** troca toda a stack HTTP validada (WebServer síncrono + ApiRouter testável), refactor
amplo sem necessidade demonstrada.

## Decisão

Adotar `apps/web/` (React 19 + Vite singlefile + Tailwind/shadcn mínimo, sem CDN nem fontes
externas) com stage do `dist/index.html` para `apps/firmware/data/` e entrega via LittleFS.
O firmware monta o LittleFS, serve `GET /` do filesystem (fallback para `dashboard.h` quando
ausente) e publica o mesmo JSON de `GET /api/data` via `WebSocketsServer` na porta 81 a cada
500 ms, somente com clientes conectados. `ApiRouter::buildDataJson()` é a origem única da
serialização (sem duplicar o envelope). Orçamento do bundle: 600 KB. Deploy em comando único
(`bun run web:deploy`: build web + stage + build firmware + `uploadfs` + `upload`).

## Consequências

- Mudar a interface não exige recompilar o firmware, mas exige `uploadfs`: são dois uploads
  (filesystem + firmware), orquestrados pelo script único.
- `GET /api/data` e o contrato da SPEC-0002 não mudam; o app usa WS com fallback polling.
- Sem `/index.html` no LittleFS, o ESP32 serve o painel legado embarcado (degradação conhecida).
- Cada broadcast aloca o JSON em RAM a 2 Hz; a cadência exata depende da coexistência
  SoftAP/Bluetooth Classic, como já registrado na ADR-0005.
- O navegador continua sem canal de escrita na ECU: WS é somente push do snapshot.
- O ensaio físico (placa + ELM327 + Android) permanece pendente, como o restante do hardware.

## Confirmação

Evidência de 2026-10-15 no host de desenvolvimento: `apps/web/dist/index.html` com 267 KB
(83 KB gz); `bun test` no workspace web 21/21; `pio test -e native` 32/32 (inclui `test_ws`);
`pio run -e esp32doit-devkit-v1` SUCCESS (RAM 14,3%, Flash 65,1%); suíte Playwright do painel
legado 17/17 (fallback preservado); `bun scripts/web-deploy.mjs --build-only` exit 0.

## Notas

- [WebSockets para Arduino (Links2004)](https://github.com/Links2004/arduinoWebSockets).
- [LittleFS no Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/littlefs.html).
- [vite-plugin-singlefile](https://github.com/richard-hajek/vite-plugin-singlefile).
