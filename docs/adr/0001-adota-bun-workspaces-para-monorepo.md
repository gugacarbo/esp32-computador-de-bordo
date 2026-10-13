---
status: accepted
date: 2026-10-13
builds-on: []
superseded-by: null
deciders: ["project maintainer"]
---

# Adota Bun workspaces para separar app e pacotes

## Contexto e problema

O repositório começou como um único pacote Bun/TypeScript na raiz. A expansão para um monorepo precisa separar a aplicação executável das futuras bibliotecas compartilhadas, manter uma instalação e um lockfile únicos e preservar comandos consistentes na raiz. No momento há somente um app; firmware, painel e bibliotecas compartilhadas ainda não existem.

## Direcionadores da decisão

- Deixar cada aplicação com manifesto e dependências próprios.
- Reservar um local explícito para pacotes compartilhados sem criar pacotes vazios ou antecipar domínios.
- Manter Bun, `bun.lock`, ferramentas de qualidade e CI centralizados na raiz.
- Impedir imports diretos entre apps e deep imports para o código-fonte de pacotes.

## Opções consideradas

### Manter um único pacote na raiz

**Prós:** mantém a árvore e os comandos atuais simples.
**Contras:** mistura o app com a configuração do repositório e não estabelece limites para novos apps ou bibliotecas.

### Criar workspaces Bun para apps e packages

**Prós:** dá ownership e dependências próprios a cada app, define o local das bibliotecas compartilhadas e mantém instalação, lockfile e ferramentas comuns na raiz.
**Contras:** exige manifests por workspace e scripts agregadores na raiz.

## Decisão

O repositório usará workspaces nativos do Bun, com padrões `apps/*` e `packages/*`. O código existente será movido para `apps/computador-de-bordo`, que será o único workspace de app inicial. `packages/` ficará reservado para módulos compartilhados que venham a ter consumidores reais; nenhum pacote vazio será criado agora. A raiz será privada e manterá o lockfile, as ferramentas, a CI e os comandos que delegam aos workspaces.

## Consequências

- Cada app ou pacote terá seu próprio `package.json`; dependências internas usarão o protocolo `workspace:*` quando forem consumidas.
- Os comandos comuns de build, typecheck e teste serão executados por filtros Bun nos workspaces.
- A configuração de arquitetura percorrerá apps e packages, proibirá imports entre apps e exigirá exports públicos para dependências de packages.
- A aplicação atual continua executada por Bun; esta decisão não define placa, firmware, hardware, painel nem protocolo.
- A CI continua centralizada na raiz e instala o workspace inteiro com `bun install --frozen-lockfile`.

## Confirmação

```bash
bun install --frozen-lockfile
bun run architecture:check
bun run knip
bun run typecheck
bun run test
bun run build
```

## Notas

Este ADR registra a estrutura inicial aprovada para o monorepo. Adicionar um segundo app ou uma biblioteca compartilhada exige seu próprio contrato e validação dentro dos limites definidos acima.
