---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-20
builds-on: [ADR-0004, ADR-0009, ADR-0011] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Persiste logs do firmware no LittleFS com lote e rotacao

## Contexto e problema

A ADR-0011 decidiu registrar os logs num buffer circular em RAM exposto por `GET /api/logs`. Ela
funciona para "o que aconteceu agora", mas o buffer vive só na RAM: qualquer reboot — queda de
tensão no veículo, watchdog, OTA — apaga o histórico. No veículo não há cabo USB nem alguém
olhando a serial, então uma falha que reinicia a placa some sem deixar rastro. Pior: o histórico
não atravessa o ciclo de uso (ligar, dirigir, desligar), que é justamente quando os problemas
aparecem.

O LittleFS de 512 KB (`partitions_ota.csv`, ADR-0009) já existe e hospeda o bundle do painel. Ele
sobrevive a reboot, mas tem custo de escrita (desgaste da flash) e risco de corte de energia no
meio da gravação — os dois motivos pelos quais a ADR-0011 recusou persistir. Persistir exige
revisitar aquela decisão com um desenho que mitigue esses riscos.

## Direcionadores da decisão

- Ler, pelo celular no SoftAP, o que aconteceu ANTES do reboot atual (não só depois).
- Não competir com o bundle do painel: o LittleFS tem 512 KB e o bundle já é validado com teto de
  448 KB (`apps/web/src/build-assets.test.ts`). A cota do log precisa ser pequena e fixa.
- Desgaste de flash aceitável: só sinais de baixo volume vão para o arquivo.
- Nunca fazer I/O no caminho quente do log: a task OBD (núcleo 0) e o `loop()` não podem bloquear
  em escrita de flash (ADR-0004).
- Sobreviver a corte de energia: uma linha entra completa ou não entra; nunca pela metade.

## Opções consideradas

### Opção 1 — Gravar tudo (info+) direto no arquivo a cada linha

**Prós:** histórico completo; implementação direta.

**Contras:** as métricas de 30 s (`maybeLogMetrics`) e os `info` de rotina escreveriam dezenas de
vezes por hora — muito desgaste e volume para pouca informação; I/O no caminho quente do log.
É exatamente o que a ADR-0011 recusou.

### Opção 2 — Persistir só warn+ num lote gravado com flush periódico (escolhida)

**Prós:** volume mínimo (só falhas e o resumo de boot); I/O fora do caminho quente, no `loop()`;
rotação simples com cota fixa; uma linha é gravada inteira ou descartada.

**Contras:** perde os `info` no arquivo (ficam só na RAM/Serial); o último lote (≤ 60 s) pode se
perder num corte de energia — mas continua no buffer em RAM.

### Opção 3 — Usar NVS (Preferences) em vez do LittleFS

**Prós:** sem gestão de filesystem.

**Contras:** NVS é para chave/valor pequeno, não para log append-only; o desgaste é o mesmo e o
tratamento de arquivo/rotação vira código ad hoc. Já usada para credenciais e contador de boots.

## Decisão

Adotamos a **opção 2**: persistir no LittleFS **somente `warn`+ (`error`/`warn`) e o resumo de
boot**, acumulados num lote em RAM e gravados por um `flush` com throttle no `loop()` (a cada
`ONIX_LOG_FILE_FLUSH_MS` = 60 s). O arquivo é dividido em `ONIX_LOG_FILE_SEGMENTS` = 4 segmentos
de até `ONIX_LOG_FILE_SEGMENT_BYTES` = 12 KB (cota total de 48 KB, ~9% do LittleFS); o segmento 0 é
o mais novo e a rotação empurra 0→1→…→N-1, descartando o mais antigo. `GET /api/logs` continua com
o buffer em RAM e ganha o histórico do arquivo; `GET /api/logs/download` entrega o dump cru em
`text/plain` para tirar o histórico do veículo.

A escrita roda na fila `LogFileStore` (em `log_file.h`/`log_file.cpp`, portátil e testada no env
`native`), alimentada pelo `logf()`; o `File`/`LittleFS` fica isolado em `main.cpp` atrás da porta
`LogFileSinkPort` (mesmo padrão de `WifiConfigPort`/`BtConfigPort`). Cada `flush` abre, escreve e
fecha o `File` — nunca fica aberto entre flushes — o que faz cada linha já estar na flash quando a
chamada retorna e torna o histórico resistente a corte de energia. O `flush` nunca roda dentro de
`logf()`, então nenhum dos dois núcleos bloqueia em flash no caminho quente.

## Consequências

- **Positivas:** o histórico atravessa reboots e o ciclo de uso; `GET /api/logs` responde o que
  aconteceu antes da sessão atual; o download permite arquivar a evidência; o caminho quente do log
  segue sem I/O.
- **Negativas:** a cota de 48 KB precisa ser respeitada (a persistência para com pouco espaço livre
  em vez de encher o FS, contando em `droppedLack`); os `info` não vão para o arquivo; um segmento
  do meio pode faltar se um corte ocorrer durante a rotação (perde o mais antigo — documentado).
- **Obrigatório:** todo I/O do histórico passa pela porta `LogFileSinkPort` e só é chamado do
  `flush` do `loop()`; a formatação/rotação/parse ficam em `log_file.*` (sem Arduino), testáveis no
  env `native`.
- **Proibido:** escrever no LittleFS a partir de `logf()` ou da task OBD; gravar níveis abaixo de
  `warn` no arquivo; manter um `File` aberto entre flushes.

## Confirmação

```bash
# 1. O caminho quente não faz I/O: logf() só alimenta o lote (nenhuma chamada LittleFS./File em
#    log.cpp — comentários que citam o nome do FS não contam).
grep -nE "LittleFS\.|\bFile\b[ (]" apps/firmware/src/log.cpp && exit 1
# 2. Formatação/rotação/parse testáveis no env native.
PATH=$PWD/.venv/bin:$PATH bun run firmware:test # test_log_file n/N verdes
# 3. O contrato HTTP ganha o download e mantém /api/logs.
grep -n "API_ROUTE_LOGS_DOWNLOAD" apps/firmware/include/api.h
```

## Notas

- A ADR-0011 continua válida para o buffer em RAM e para o contrato de `GET /api/logs`; esta ADR
  revoga apenas a parte que recusava persistir (`superseded-by` preenchido na 0011).
- Reconciliação de um segmento truncado no meio da rotação fica fora do escopo: o segmento perdido
  é sempre o mais antigo e o rastro recente permanece no segmento 0 e no buffer em RAM.
