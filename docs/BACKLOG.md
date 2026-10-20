# Backlog — esp32-computador-de-bordo

Pendências e reservas de numeração (STANDARD §5, ADR-0002, ADR-0013).
Reserve com `scripts/docs-reserve adr|spec "título"` — linha fora do
formato da tabela é apontada pelo comando, nunca lida em silêncio.

## Reservas ADR

| NNNN | título                                                        | data       | situação |
| ---- | ------------------------------------------------------------- | ---------- | -------- |
| 0001 | adota-bun-workspaces-para-monorepo                            | 2026-10-13 | em uso   |
| 0002 | Adota firmware C++ com PlatformIO no monorepo                 | 2026-10-13 | em uso   |
| 0003 | Usa ELM327 via Bluetooth Classic para acesso OBD              | 2026-10-13 | em uso   |
| 0004 | Isola OBD em uma task e compartilha leituras em memória       | 2026-10-13 | em uso   |
| 0005 | Serve painel offline por HTTP com polling                     | 2026-10-13 | em uso   |
| 0008 | Serve painel com assets estáticos compactados                 | 2026-10-19 | em uso   |
| 0009 | Habilita atualização OTA por Wi-Fi com ArduinoOTA             | 2026-10-19 | em uso   |
| 0010 | Serve assets do painel somente em gzip                        | 2026-10-19 | em uso   |
| 0011 | Registra logs do firmware em buffer circular exposto por HTTP | 2026-10-19 | em uso   |
| 0012 | Conecta ao hotspot do celular com fallback para o SoftAP      | 2026-10-19 | em uso   |
| 0013 | Isola OBD no núcleo 0 e para a busca Bluetooth após falhas    | 2026-10-19 | em uso   |
| 0014 | Configura o scanner Bluetooth pelo painel sem senha do OTA    | 2026-10-19 | em uso   |

## Reservas Spec

| NNNN | título                                   | data       | situação |
| ---- | ---------------------------------------- | ---------- | -------- |
| 0001 | Coleta sensores OBD e recupera a conexão | 2026-10-13 | em uso   |
| 0002 | Expõe o estado das leituras na API local | 2026-10-13 | em uso   |
| 0003 | Exibe o painel offline no Android        | 2026-10-13 | em uso   |
