# Computador de bordo ESP32

Monorepo Bun com o painel web em `apps/web` (React+Vite, bundle servido pelo ESP32 via LittleFS). Bibliotecas que forem compartilhadas por mais de um workspace ficam em `packages/`; crie um pacote quando houver um consumidor real.

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

Publicar o painel no ESP32 (placa via USB, build web + LittleFS + firmware):

```bash
bun run web:deploy --port /dev/ttyUSB0
```
