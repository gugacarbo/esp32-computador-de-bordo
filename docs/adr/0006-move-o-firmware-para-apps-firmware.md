---
status: accepted
date: 2026-10-14
builds-on: [ADR-0002]
superseded-by: null
deciders: ["project maintainer"]
---

# Move o firmware para apps/firmware

## Contexto

A ADR-0002 colocou o firmware em `firmware/` na raiz, fora dos workspaces Bun. Com a chegada dos
testes nativos Unity, dos testes Playwright do painel e dos scripts agregadores da raiz
(`firmware:test`, `firmware:build`, `firmware:test:web`), o firmware passou a ser uma frente
principal do monorepo, e a raiz acumulava um diretório fora do padrão `apps/*`/`packages/*`
mapeado pelo AGENTS.md e pelo checker de arquitetura.

## Decisão

Mover o firmware para `apps/firmware/`, dentro do padrão `apps/*` dos workspaces Bun, preservando
a configuração PlatformIO, os ambientes `esp32doit-devkit-v1` e `native`, as versões fixadas e os
testes existentes. O diretório não ganha manifesto de workspace: continua sendo um projeto
PlatformIO, não um pacote Bun; os scripts da raiz o invocam por caminho.

## Consequências

- Caminhos canônicos do firmware passam a ser `apps/firmware/{src,include,test,web-tests}`; a raiz
  volta a conter apenas workspaces, ferramentas e documentação.
- Scripts agregadores e a CI referenciam `apps/firmware` explicitamente, como já registrado desde a
  mudança física (commit `a9c7efd`).
- Este documento registra a decisão depois da mudança física; a ADR-0002 recebeu emendas de
  localização apontando para cá.
- Nada muda na toolchain: PlatformIO continua responsável por build, testes nativos e gravação.

## Confirmação

`.venv/bin/pio test -d apps/firmware -e native` (27 casos + `test_http` GREEN) e
`.venv/bin/pio run -d apps/firmware -e esp32doit-devkit-v1` (build do alvo DOIT sem erro),
executados em 2026-10-14 no host de desenvolvimento.

## Notas

Decisão registrada em 2026-10-14 junto ao fechamento do MVP verificável em documentos.
