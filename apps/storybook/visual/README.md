# Regressão visual do Storybook

Cada story do build estático (`apps/storybook/storybook-static`) vira um screenshot de página inteira, nos projetos `mobile` (390x844) e `desktop` (1280x800). Os snapshots ficam em `__snapshots__/{projeto}/{id-da-story}.png`.

## Como rodar

```bash
bun run design:visual
```

O config (`playwright.storybook.config.ts`) roda o build do Storybook antes de carregar os testes e sobe um servidor estático em `http://127.0.0.1:6616` (`STORYBOOK_VISUAL_PORT` muda a porta; não use `STORYBOOK_PORT`, que é do dev server).

## Atualizar snapshots

Depois de uma mudança intencional no visual:

```bash
bun run design:visual -- --update-snapshots
```

## Revisão no PR

Todo PR que altera snapshots precisa ter o diff dos PNGs revisado (abra os arquivos alterados no GitHub). Não aceite atualização em massa sem olhar.

Gere os snapshots no mesmo SO do CI (ubuntu). Renderização de fonte e antialiasing mudam entre sistemas operacionais; a tolerância (`maxDiffPixelRatio: 0.01`) absorve pequenas diferenças, não diferenças de SO.
