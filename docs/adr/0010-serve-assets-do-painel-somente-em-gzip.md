---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0008, ADR-0009] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Serve assets do painel somente em gzip

## Contexto e problema

A ADR-0008 publica cada asset do painel duas vezes no LittleFS, sem compressão e em gzip, e o
servidor escolhe pela negociação de `Accept-Encoding`. Com a tabela OTA da ADR-0009, o LittleFS
caiu para 512 KB e o painel staged ocupa 349,8 KB, dos quais cerca de 266 KB são as cópias sem
compressão. Essas cópias só servem a clientes que não anunciam gzip; o painel é aberto por
navegadores, que anunciam gzip, e a rede do SoftAP não tem proxy no caminho.

## Direcionadores da decisão

- Liberar flash do LittleFS para o painel crescer ou para devolver espaço aos slots de firmware.
- Continuar entregando o painel a qualquer navegador moderno.
- Não quebrar `GET /api/data`, o WebSocket nem o fallback embarcado de `dashboard.h`.

## Opções consideradas

### Opção 1 — manter as duas variantes (ADR-0008)

**Prós:** cliente sem gzip recebe o asset sem compressão.
**Contras:** gasta cerca de 266 KB de flash para um cliente que não abre o painel.

### Opção 2 — publicar só `.gz` e servi-lo sempre

**Prós:** painel staged cai para cerca de 85 KB; servidor fica mais simples, sem negociação nem
`Vary`.
**Contras:** cliente sem gzip (por exemplo `curl` sem `--compressed`) recebe o corpo comprimido.

### Opção 3 — publicar só `.gz` e descomprimir no ESP32 para quem não aceita gzip

**Prós:** atende qualquer cliente.
**Contras:** exige inflate em streaming no firmware, que já usa 94,6% do slot.

## Decisão

Adotar a Opção 2. O stage copia só o `.gz` de todo arquivo que tem sidecar comprimido; arquivos
sem sidecar, como o `index.html` de menos de 1 KB, vão sem compressão. O firmware serve o `.gz`
sempre que ele existir, com `Content-Encoding: gzip` e o Content-Type do arquivo original, sem ler
`Accept-Encoding`.

## Consequências

- O painel staged cai de 349,8 KB para cerca de 85 KB no LittleFS de 512 KB.
- Clientes HTTP que não descomprimem gzip recebem bytes comprimidos; o painel não os atende.
- `Vary: Accept-Encoding` deixa de ser enviado, pois a resposta não varia mais por cliente.
- As políticas de cache da ADR-0008 (HTML revalidável, assets com hash imutáveis) permanecem.
- O LittleFS de 512 KB passa a ter folga grande; reduzi-lo para ampliar os slots de firmware
  exige nova tabela de partições e gravação por USB (ADR-0009).

## Confirmação

```bash
bun test apps/web/src/build-assets.test.ts
PATH=$PWD/.venv/bin:$PATH bun run firmware:test
bun run web:deploy --build-only
```

O teste web confirma que o stage contém o `.gz` de cada asset e não contém o original. Na placa,
um asset requisitado por navegador deve chegar com `Content-Encoding: gzip`.
