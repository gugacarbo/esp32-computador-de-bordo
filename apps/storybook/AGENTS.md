# AGENTS.md — apps/storybook

Storybook 10 (`@storybook/react-vite`) do painel web. As stories ficam em `apps/web/src/**/*.stories.tsx`, ao lado dos componentes; este workspace só guarda a configuração (`.storybook/`) e o CSS (`src/storybook.css`).

- Rodar: `bun run --filter '@esp32-computador-de-bordo/storybook' dev` (porta `STORYBOOK_PORT` do `.env`, padrão 6606). O servidor MCP (`@storybook/addon-mcp`) responde em `http://localhost:6606/mcp` e está registrado em `.mcp.json` na raiz.
- Antes de usar ou criar componentes de UI, consulte as ferramentas MCP do Storybook; nunca presuma props.
  - `docs-list` e `docs-show` listam componentes e suas props/exemplos; use só props documentadas ou vistas em stories, e pergunte se faltar informação.
  - `get-storybook-story-instructions` dá as convenções atuais antes de criar ou editar stories.
  - Valide com `test-run`.
- Stories não podem importar de `apps/web` por caminho em `.storybook/` ou `src/` (o `architecture:check` proíbe imports entre apps); a ligação é só o glob em `main.ts` e o `@source` do CSS.

## Convenções de stories

- Arquivo `<componente>.stories.tsx` ao lado do componente, em `apps/web/src/components/ui/`.
- `satisfies Meta<typeof X>`, `tags: ["autodocs"]` e `title: "UI/<Nome>"` (ou `Design/<Tema>` para tokens).
- Uma story por estado: padrão, desabilitado, `Falha` (nunca `Error`), vazio e carregando, quando fizerem sentido para o componente.
- Props sempre com TSDoc em português (`/** ... */`), que alimenta a documentação e o MCP.
- Estilos só por tokens de design (`bg-surface`, `text-fg`, `text-status-ok` etc., definidos em `apps/web/src/index.css`); nunca cores cruas.
