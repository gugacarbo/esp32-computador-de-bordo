---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0015] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Adota cobertura de design em camadas com hooks e CI

## Contexto e problema

O ADR-0015 define tokens semânticos e um tema escuro único para o painel, e os componentes passam
a usar apenas classes desses tokens. Essa regra, porém, depende de revisão manual: nada impede
uma cor crua (`slate-950`, `emerald-300`) em um componente novo, uma story faltando ou um
componente que reimplementa um botão base. Com o Storybook (`apps/storybook/`) passa a existir
também um lugar para testar estados visuais, acessibilidade e regressão de pixel, mas essas
verificações exigem navegador Chromium e são lentas demais para rodar a cada commit.

## Direcionadores da decisão

- Regressões de design devem ser barradas antes de chegar ao `main`, sem exigir que o autor se
  lembre da regra.
- Hooks locais precisam ser rápidos e não podem depender de navegador, para não desencorajar
  commits pequenos.
- Verificações pesadas (Vitest em Chromium, a11y, Playwright) precisam de um ambiente reprodutível,
  e o CI do GitHub Actions oferece isso.
- Snapshots visuais mudam por motivos legítimos; a mudança precisa ser revisada, não silenciada.

## Opções consideradas

### Opção 1 — somente revisão manual e checklist no PR

**Prós:** nenhum custo de execução; nenhuma ferramenta nova.
**Contras:** a regra do ADR-0015 depende de memória; regressões visuais só aparecem ao olhar o
painel no carro.

### Opção 2 — todas as verificações de design em pre-commit e CI

**Prós:** cobertura total em um único ponto.
**Contras:** o pre-commit passaria a abrir Chromium a cada commit, com lentidão que leva a
`--no-verify`; a cobertura de hook perde valor se for contornada.

### Opção 3 — cobertura em camadas: verificação básica nos hooks e completa no CI

**Prós:** o hook pega cores cruas, stories faltando e uso indevido de componentes base em
segundos, sem navegador; o CI roda a suíte completa com Chromium, a11y, cobertura e regressão
visual, com relatórios em artefato.
**Contras:** duas camadas precisam ser mantidas em sincronia; uma regressão visual só aparece no
CI, depois do push.

## Decisão

Adotar a Opção 3, com cinco camadas:

1. **Tokens:** `apps/web/src/index.css` (ADR-0015) é a fonte das cores e da tipografia.
2. **Stories:** cada componente base tem story em `apps/web/src`, exibida pelo Storybook.
3. **Uso:** `scripts/design-check.ts` (`bun run design:check`) reprova cores cruas, stories
   ausentes e componentes que reimplementam a base. É a única camada executada em
   `.husky/pre-commit` e `.husky/pre-push`.
4. **Testes, a11y e cobertura no Storybook:** `bun run design:test` roda as stories no Vitest com
   Chromium, verifica acessibilidade e mede cobertura.
5. **Regressão visual:** `bun run design:visual` compara os snapshots de
   `apps/storybook/visual/__snapshots__` com Playwright.

As camadas 4 e 5, e a execução completa (`bun run design:check:full`), rodam no job `design` do
CI. A camada 3 roda também no job `checks`. Exceções à verificação básica são declaradas e
justificadas em `scripts/design-check.ts`, e não por supressão ad hoc nos componentes.

## Consequências

- **Custo de CI:** o job `design` instala Chromium e roda a suíte de navegador a cada push e pull
  request. O tempo de execução aumenta e deve ser acompanhado.
- **Manutenção dos snapshots:** toda mudança visual intencional exige atualizar os snapshots com
  `bun run design:visual -- --update-snapshots` e revisar a diferença no PR. Snapshots não podem
  ser atualizados sem revisão.
- **Relatórios:** cobertura, relatório do Playwright e resultados de teste são enviados como
  artefato `design-reports` em toda execução do job, inclusive quando falha.
- **Hooks só barram o básico:** um commit pode passar no hook e falhar no CI por regressão visual
  ou de acessibilidade. Isso é aceito como custo da separação.
- **Proibido:** suprimir a verificação básica de design com `--no-verify` no fluxo do repo, ou
  colar cores cruas em componentes para contornar `design:check`.
- **Obrigatório:** exceções à regra de design ficam registradas em `scripts/design-check.ts`, com
  motivo.

## Confirmação

```bash
# Verificação básica roda nos hooks e no CI
grep -n "design:check" .husky/pre-commit .husky/pre-push .github/workflows/ci.yml
# Job de navegador existe e não é executado no hook
grep -n "design:test\|design:visual" .github/workflows/ci.yml && ! grep -n "design:visual\|design:test" .husky/pre-commit .husky/pre-push
# Nenhuma cor crua nova nos componentes (regra herdada do ADR-0015)
bun run design:check
```

## Notas

A escolha de Chromium apenas no CI, e não nos hooks, é um trade-off de velocidade. Se a suíte
básica ficar lenta o suficiente para desencorajar commits, a revisão deve ser feita por um novo
ADR, e não por ajuste silencioso dos hooks.
