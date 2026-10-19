---
status: accepted
date: 2026-10-13
builds-on: [ADR-0008, ADR-0010, SPEC-0002]
implemented-by:
  - apps/web/src/App.tsx
  - apps/web/src/hooks/useTelemetry.ts
  - apps/web/src/lib/envelope.ts
  - apps/web/src/lib/source.ts
  - apps/firmware/src/main.cpp
  - apps/firmware/include/static_asset_policy.h
  - apps/firmware/test/test_http/test_http.cpp
  - apps/web/vite.config.ts
  - apps/web/scripts/stage.mjs
  - apps/web/src/build-assets.test.ts
  - apps/firmware/include/ws_broadcast.h
  - scripts/web-deploy.mjs
---

# Exibe o painel offline no Android

## Objetivo

Exibir o estado do Onix no navegador Android conectado ao Wi-Fi do ESP32, com interface simples,
responsiva e totalmente offline. A página apresenta dados já coletados; não controla o veículo.

O painel é o app React de `apps/web/` (Vite com assets separados, Tailwind/shadcn mínimo), staged
em `apps/firmware/data/` e servido via LittleFS; o HTML legado em
`apps/firmware/include/dashboard.h` segue como fallback quando o filesystem está vazio. O app
recebe o envelope da SPEC-0002 por WebSocket (`ws://` + IP do ESP32 + `:81/ws`) com fallback
para polling `GET /api/data`. O painel legado continua validado por Playwright contra o HTML
real de `dashboard.h`; o ensaio integrado em navegador Android físico permanece pendente.

## Fluxo

1. O usuário alimenta o ESP32 por USB durante o teste e conecta o Android à rede definida na SPEC-0002.
2. Abre o endereço local no navegador; o HTML vem do LittleFS (ou do fallback embarcado).
3. A página mostra identidade, cartões e estado inicial enquanto abre o WebSocket.
4. O ESP32 envia o envelope por WS a cada 500 ms; sem WS, o app consulta `GET /api/data` em polling de 500 ms e atualiza textos com valores e estados.
5. Uma falha de transporte ou OBD muda a apresentação; a página permanece aberta e tenta recuperar.

## Contrato

### Conteúdo e apresentação

- Cabeçalho: `ONIX` e `Computador de bordo`.
- Seis cartões: RPM, Velocidade, Temperatura do líquido de arrefecimento, Borboleta, MAP e Tensão.
- Unidades: rpm, km/h, °C, %, kPa e V, respectivamente.
- RPM e velocidade sem casas decimais; temperatura, borboleta e MAP com arredondamento de apresentação
  simples; tensão com uma casa decimal. Arredondar na apresentação, preservando a leitura na API.
- Identificar tensão como `Tensão do adaptador` ou `Tensão da ECU` pelo campo `source`.
- Usar organização que caiba em telas de celular sem rolagem horizontal; validar larguras de 320 e
  360 pixels CSS e uma tela maior. Não exigir gráficos, instrumentos animados ou tela de configuração.
- HTML com CSS e JavaScript gerados como arquivos separados e versionados por `apps/web/`, sem fontes, imagens, scripts ou estilos de CDN. O build também gera variantes gzip para os assets; o LittleFS recebe só a variante gzip e o servidor a entrega sempre (ADR-0010).
- A resposta HTML usa `Cache-Control: no-cache`; assets com hash usam `Cache-Control: public, max-age=31536000, immutable`. O servidor preserva o Content-Type original e envia `Content-Encoding: gzip`.
- A árvore stage (`index.html` e assets gzip) não excede 448 KB no LittleFS de 512 KB (ADR-0009).
- Sem `/index.html` no LittleFS, `GET /` serve o painel legado embarcado (`dashboard.h`): degradação conhecida, mesmos estados.
- Os estados não dependem somente de cor: têm texto, contraste legível e associação com o sensor.
- A interface não contém botão de apagar erros, executar comandos ou escrever na ECU.

### Estado do painel

| Situação observável                                       | Apresentação                                                                  |
| --------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Ainda não chegou o primeiro snapshot                      | `Carregando dados`, cartões com `—`.                                          |
| API disponível, Bluetooth desconectado                    | `OBD desconectado`, sensores conforme seus estados.                           |
| Bluetooth ativo, adaptador ainda não inicializado         | `Inicializando adaptador`.                                                    |
| Adaptador pronto, ECU sem resposta recente                | `ECU sem resposta`.                                                           |
| `obdConnected: true`                                      | `OBD conectado`, sem presumir que todo sensor é suportado.                    |
| Consulta HTTP falhou ou resposta não tem estrutura válida | `Sem comunicação com o ESP32`; retirar a apresentação de valores como atuais. |

Uma falha web tem precedência sobre o último estado OBD recebido: não manter `OBD conectado` como se
houvesse uma confirmação nova. Quando a API volta, aplicar a resposta e recuperar o estado automaticamente.

### Estado dos cartões

| Estado na API                                      | Apresentação                                                                                      |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `valid: true`, `status: ok`                        | Valor e unidade, incluindo zero legítimo.                                                         |
| `unsupported`                                      | `Não suportado`, sem fabricar valor.                                                              |
| `unavailable` sem leitura anterior                 | `—` e `Sem leitura`.                                                                              |
| `timeout` ou `error`                               | `Falha na leitura`; último valor somente com indicação explícita de leitura anterior.             |
| `stale`                                            | `Desatualizado`; último valor identificado como leitura anterior.                                 |
| Origem offline ou falha HTTP, com leitura anterior | Mostrar `—` como valor atual; o último valor pode aparecer secundariamente como leitura anterior. |

A existência de `value` não implica validade. Se `valid` for falso, o último valor nunca terá a mesma
apresentação de uma leitura atual. Uma combinação de validade e status inconsistente ou ausência de
campos obrigatórios é resposta inválida e segue o tratamento de falha HTTP.

### Atualização

- Push WebSocket a cada 500 ms, somente com clientes conectados; o firmware envia o mesmo JSON de `GET /api/data`, sem canal de escrita na ECU.
- Fallback polling `GET /api/data` a cada 500 ms quando o WS está indisponível, independente das taxas de coleta OBD.
- No máximo uma requisição em andamento; agendar a próxima após a anterior terminar, sem rajada para
  compensar atrasos. O app encerra uma requisição que ultrapasse seu timeout configurado (4 s) e aplica watchdog de 4 s sem mensagem WS.
- Reconexão do WS com backoff de 1 s a 3 s; continuar tentando durante falhas e recuperar sem recarregar a página.
- Aplicar uma resposta somente após conferir a estrutura necessária; falha de parsing não apaga silenciosamente a tela.
- Continuar tentando durante falhas e recuperar sem recarregar a página.
- Não acumular intervalos, requisições ou listeners ao alternar a aba ou recuperar a conexão.
- Interrupção ou troca da rede no Android não gera valores simulados nem dependência de internet.

## Casos de borda

| #   | QUANDO                                                          | A página DEVE                                                                                              |
| --- | --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| 1   | Abre sem scanner e antes do primeiro JSON                       | Mostrar estrutura, carregamento inicial e depois estado offline, sem tela vazia.                           |
| 2   | O sensor ainda não foi lido ou não é suportado                  | Diferenciar ausência de leitura de ausência de suporte.                                                    |
| 3   | O sensor retorna zero válido                                    | Exibir `0` com unidade, sem substituí-lo por `—`.                                                          |
| 4   | A leitura fica inválida, vence ou o scanner desconecta          | Retirar o valor como atual e identificar a última leitura, caso seja mostrada.                             |
| 5   | Bluetooth conecta, mas inicialização ou ECU ainda não respondeu | Diferenciar as fases sem anunciar OBD conectado.                                                           |
| 6   | HTTP falha, demora ou retorna JSON inválido                     | Mostrar falha de comunicação, não sobrepor consultas e continuar tentando.                                 |
| 7   | HTTP e coleta se recuperam                                      | Atualizar estados e valores sem recarregamento nem timers duplicados.                                      |
| 8   | A tela tem 320 ou 360 pixels CSS de largura                     | Manter textos e cartões legíveis, sem rolagem horizontal.                                                  |
| 9   | O celular não tem internet                                      | Carregar toda a interface pela origem local, sem requisições externas.                                     |
| 10  | A tensão é obtida por uma origem diferente                      | Usar a identificação recebida da API, sem assumir que é tensão da bateria.                                 |
| 11  | A aba volta após ficar suspensa pelo Android                    | Retomar consulta sem rajada; só apresentar dados como atuais após resposta nova.                           |
| 12  | O LittleFS está sem `index.html` (stage/uploadfs pendente)      | Servir o painel legado embarcado em `GET /`, com os mesmos estados.                                        |
| 13  | O navegador solicita um asset                                   | Receber o conteúdo comprimido com Content-Type original e Content-Encoding gzip.                           |
| 14  | O cliente não anuncia gzip                                      | Receber o mesmo conteúdo comprimido com Content-Encoding gzip; o LittleFS não guarda cópia sem compressão. |
| 15  | O navegador recarrega um asset com hash                         | Receber política de cache imutável; `GET /` continua revalidável.                                          |

## Questões em aberto

Nenhuma decisão de UX permanece aberta. O visual final é avaliado por legibilidade e estados, sem
referência Figma ou design system externo obrigatório.

## Definition of Done

A página e os testes Playwright com snapshots controlados existem; o comando canônico da matriz é:

```bash
bun run firmware:test:web
```

A suíte acima cobre as quatro primeiras linhas da matriz no navegador contra o HTML real servido localmente;
a última (ensaio integrado com API e navegador Android no Wi-Fi do ESP32) permanece pendente de hardware.

| Validação necessária                                                     | Casos cobertos | Critério binário                                                                    |
| ------------------------------------------------------------------------ | -------------- | ----------------------------------------------------------------------------------- |
| Testes de renderização com snapshots da SPEC-0002                        | 1–5, 10        | Textos, unidades, origem da tensão e tratamento de zero/nulos conforme contrato.    |
| Testes de polling com resposta lenta, falha, JSON inválido e recuperação | 6–7, 11        | No máximo uma requisição; recuperação automática e ausência de timers duplicados.   |
| Testes de layout em navegador a 320 e 360 pixels e tela maior            | 8              | Não há overflow horizontal; cartões e mensagens permanecem acessíveis.              |
| Teste de carregamento com acesso externo bloqueado                       | 9              | A interface funciona e todas as requisições têm a origem local do dispositivo.      |
| Fallback com LittleFS vazio                                              | 12             | `GET /` responde 200 com o painel legado quando `data/index.html` não existe.       |
| Entrega de assets estáticos                                              | 13–15          | URLs geradas existem no LittleFS; gzip, Content-Type e políticas de cache corretos. |
| Teste integrado com API e ensaio no navegador Android real               | 1–11           | Estados web e OBD distinguíveis; leituras e reconexão observadas no dispositivo.    |

O teste integrado pode reutilizar a evidência de API da SPEC-0002 quando os casos de interface forem
explicitamente registrados. Simulação no navegador não substitui o ensaio Wi-Fi/Bluetooth no hardware.

## Revisão humana

- Conferir legibilidade de unidades, temperaturas e mensagens no celular usado pelo mantenedor.
- Confirmar funcionamento na rede sem internet; pode ser necessário aceitar permanecer conectado a ela no Android.
- Observar inicialização, scanner ausente, carro desligado e reconexão, com o carro parado.
- Conferir que dado anterior não se confunde com leitura atual e que o painel não sugere funções de escrita.

## Verificação

Executado em 2026-10-14, em Chromium headless contra fixture local (`node:http`) que serve o HTML
real extraído de `apps/firmware/include/dashboard.h`:

- `bun run firmware:test:web` (`playwright test -c playwright.firmware.config.ts`): 17 testes GREEN —
  `dashboard.render.spec.ts` (casos 1–5, 9–10: estados do painel e dos cartões, zero legítimo,
  origem da tensão, sem requisição externa), `dashboard.polling.spec.ts` (casos 6–7, 11: no máximo
  uma requisição em andamento, timeout do cliente, HTTP/JSON/estrutura inválidos, recuperação
  automática, retomada de aba suspensa sem rajada) e `dashboard.layout.spec.ts` (caso 8: 320 e
  360 px sem rolagem horizontal e tela larga com três colunas).
- O fixture `apps/firmware/web-tests/dashboard-server.ts` roda sobre `node:http` porque o runner
  Playwright deste repositório executa em Node (probe registrou `Bun.serve` indisponível).

O ensaio integrado com API real e navegador Android via Wi-Fi do ESP32 (última linha da matriz)
permanece pendente; o `implemented-by` acima cobre host + compilação, não o ensaio em hardware.

Evidência de 2026-10-19 para a entrega de assets estáticos (painel React + WS + LittleFS):

- Build de produção gera HTML separado de CSS/JS com hash e sidecars gzip; o stage copia a árvore
  completa para `apps/firmware/data/` e mantém o total abaixo de 600 KB.
- `bun test src/build-assets.test.ts` em `apps/web/`: 1 teste GREEN, 11 verificações; saída staged
  com 358.233 bytes (349,8 KB), abaixo do limite de 600 KB. CSS e JavaScript têm variantes gzip.
- `bun run test`: 22 testes GREEN e 62 verificações; `bun run typecheck`: SUCCESS.
- `apps/firmware/include/static_asset_policy.h` tem cobertura nativa para negociação gzip, fallback
  sem gzip e política de cache. O teste HTTP nativo também confirma nomes com hash e `q=0`.
- `PATH=$PWD/.venv/bin:$PATH bun run firmware:test`: 48 testes nativos GREEN; `bun run
firmware:test:web`: 17 testes Playwright GREEN.
- `bun run web:deploy --build-only` na árvore combinada com OTA: SUCCESS; firmware usa 19,8% da
  RAM e 94,6% do slot OTA (1.673.801 de 1.769.472 bytes). O stage ocupa 349,8 KB do limite de
  448 KB.
- O upload USB de LittleFS e firmware da versão com assets terminou com sucesso e hash verificado
  pela placa ESP32-D0WDQ6 antes da adoção da tabela OTA. A migração física para essa tabela e o
  primeiro upload OTA ainda estão pendentes.
- A verificação HTTP dos cabeçalhos gzip ainda não foi possível pelo host: ele está em
  `192.168.18.6` e encaminha `192.168.4.1` pelo roteador `192.168.18.1`, sem acesso à rede SoftAP
  do ESP32. O ensaio integrado com navegador Android físico continua pendente.

O build singlefile de 2026-10-15 abaixo foi substituído e não é evidência da entrega atual.

- `bun test` em `apps/web/`: 21 testes GREEN (`envelope.test.ts`: parsing/validação do envelope,
  estados do painel e formatação; `source.test.ts`: URL do WS, host e backoff).
- `bun run firmware:test` (`pio test -e native`): 32 casos GREEN, incluindo `test_ws`
  (throttle do broadcast, intervalo, dirty imediato, wraparound de `millis`).
- `bun run firmware:build` (`pio run -e esp32doit-devkit-v1`): SUCCESS (RAM 14,3%, Flash 65,1%).
- `bun run firmware:test:web`: 17 testes GREEN — o painel legado de `dashboard.h` segue válido
  como fallback do caso 12.
- `bun scripts/web-deploy.mjs --build-only`: exit 0 — build web + stage p/ `firmware/data` +
  build do firmware sem placa.

## Fora do escopo

Aplicativo Android nativo, instalação PWA, captive portal, gráficos, histórico, alertas, autenticação
de aplicação e serviço remoto. O WebSocket é somente push do snapshot: sem botões ou comandos
de escrita na ECU.
