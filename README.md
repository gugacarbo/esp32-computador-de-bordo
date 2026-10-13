# Computador de bordo ESP32

Monorepo Bun com a aplicação atual em `apps/computador-de-bordo`. Bibliotecas que forem compartilhadas por mais de um workspace ficam em `packages/`; crie um pacote quando houver um consumidor real.

## Requisitos

- Bun 1.2 ou superior

## Comandos

```bash
bun install --frozen-lockfile
bun run dev
bun run lint
bun run typecheck
bun run test
bun run build
```

Os comandos de build, tipos e testes da raiz são delegados aos workspaces Bun. Cada workspace tem seu próprio `package.json`; `bun.lock` permanece único na raiz.
