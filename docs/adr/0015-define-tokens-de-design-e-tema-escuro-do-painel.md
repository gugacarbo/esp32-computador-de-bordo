---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0007, ADR-0010] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Define tokens de design e tema escuro do painel

## Contexto e problema

O painel React era estilizado com classes utilitárias do Tailwind espalhadas pelos componentes:
cores de fundo, texto e status repetidas com valores literais (`slate-950`, `emerald-300`,
`rose-300` etc.). Componentes novos (badges, cartões, botões) precisavam decidir cores do zero, e
uma troca de paleta exigiria editar cada arquivo. Ao mesmo tempo, o painel é usado dentro do
carro, muitas vezes à noite, e precisa de contraste estável nesse cenário.

## Direcionadores da decisão

- Tema único escuro: o uso principal é no carro, com pouca luz ambiente, e o painel não precisa
  alternar temas.
- Mobile-first com alvo principal no celular Android, que fica preso ao suporte do veículo e é
  tocado com o dedo: alvo mínimo de toque de 44px.
- Bundle pequeno servido pelo ESP32 via LittleFS, comprimido em gzip (ADR-0010), com espaço
  limitado na partição.
- Sem webfont externa: a placa não tem internet e o painel precisa funcionar offline.
- Nomes semânticos (fundo, texto, status) em vez de nomes de paleta, para que os componentes
  não dependam de valores específicos.

## Opções consideradas

### Opção 1 — manter classes de paleta em cada componente

**Prós:** nenhuma camada extra; as classes do Tailwind ficam explícitas no JSX.
**Contras:** cada componente repete decisões de cor; trocar um tom exige busca e edição em vários
arquivos; nada impede que um status use uma cor que não é a dos outros.

### Opção 2 — tokens semânticos no `@theme` do Tailwind v4 com tema escuro único

**Prós:** um lugar define as cores; as classes derivadas (`bg-surface`, `text-fg-muted`,
`border-line`, `text-status-ok`) são geradas pelo próprio Tailwind; `--font-sans`, raio e
espaçamento de toque entram no mesmo mecanismo.
**Contras:** o tema claro fica fora do escopo; quem quiser um segundo tema vai precisar de um
novo ADR.

### Opção 3 — tema claro e escuro com alternância por `prefers-color-scheme`

**Prós:** respeita a preferência do sistema.
**Contras:** dobra o trabalho de contraste e de teste; o celular do motorista pode estar em modo
claro durante o dia e escuro à noite, o que torna a aparência imprevisível dentro do carro;
aumenta o bundle.

## Decisão

Adotar a Opção 2. O `apps/web/src/index.css` declara em `@theme` os tokens de cor semânticos
`surface`, `surface-raised`, `line`, `fg`, `fg-muted`, `accent` e `status-ok|warn|error|muted`,
além de `--font-sans` com pilha de fontes do sistema, `--radius-card` (0,75rem) e
`--spacing-touch` (2,75rem, alvo mínimo de toque). Os valores preservam a aparência atual: fundo
slate-950/900, texto slate-100/400, status ok em emerald-300, warn em amber-200, error em
rose-300 e accent em sky-400. O `body` aplica `bg-surface` e `text-fg` e o documento declara
`color-scheme: dark`. Componentes devem usar apenas as classes dos tokens, não as da paleta.

## Consequências

- Componentes em `apps/web/src/components/ui/` usam classes semânticas; trocar a paleta passa a
  exigir alteração em um único arquivo.
- Não há tema claro. Qualquer tema alternativo exige novo ADR que supersede este.
- A pilha de fontes depende das fontes instaladas no sistema do celular; o painel não força uma
  fonte específica.
- Proibido: webfont ou CSS externo carregado de CDN pelo painel, porque quebra o uso offline na
  placa.
- Obrigatório: alvos interativos com altura mínima de `touch` (2,75rem).

## Confirmação

```bash
# Nenhuma classe de paleta crua nos componentes do painel (somente tokens semânticos)
grep -rEn "(bg|text|border)-(slate|emerald|amber|rose|sky)-[0-9]" apps/web/src/components && exit 1
# Nenhuma webfont externa no CSS do painel
grep -rEn "fonts\.googleapis|@font-face" apps/web/src && exit 1
bunx biome ci apps/web/src
```

Na revisão visual, a story "Design/Tokens" no Storybook mostra todos os tokens de cor e de
tipografia.

## Notas

Decisões de negócio ainda não tomadas (por exemplo, um tema claro para uso diurno) ficam no
issue tracker, não neste ADR.
