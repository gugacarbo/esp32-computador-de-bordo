# AGENTS.md

```yaml
casa-repo-id: esp32-computador-de-bordo # usado em referências cross-repo (repo:ADR-0001)
casa-tier: T1 # T0 (leve) | T1 (padrão) — STANDARD §3
casa-version: 1.8 # versão do contrato CASA adotado (promessa do repo, ADR-0010)
casa-standard-ref: 7cdb964c54fe0fa0f2c6a4b7e175d09cf5f72749
```

> Padrão: https://github.com/atplus-digital/casa-standard (STANDARD.md)
> ROUTER (CASA §4): carga sempre, teto ~150 linhas. Só alto-ROI transversal.
> Estourou o teto → conteúdo desce para docs/context/, fica o ponteiro.
> ⚠️ NÃO usar @import para colar capítulos: @import expande tudo no launch.
> Regras de um pacote específico → <subdir>/AGENTS.md (lazy nativo, nearest-wins).

## Contexto em 5 linhas

Monorepo Bun para um computador de bordo ESP32, em desenvolvimento.
O painel web é React+Vite e vive em `apps/web/` (bundle servido pelo ESP32 via LittleFS).
Bibliotecas compartilhadas ficam em `packages/` quando houver mais de um consumidor.
O firmware PlatformIO vive em `apps/firmware/`; o ensaio físico no veículo ainda é pendente.

## Infra & ambientes

- Workspaces nativos do Bun usam os padrões `apps/*` e `packages/*`; `bun.lock` fica na raiz.
- Apps têm manifesto próprio; mantenha ferramentas comuns e scripts agregadores na raiz.
- Desenvolvimento e checks locais usam Bun 1.2+; CI roda no GitHub Actions com Bun e Node 22.
- O app web roda no host Bun (dev) e no ESP32 via LittleFS (deploy). O firmware compila para o ESP32 DOIT, mas não há ensaio físico, serviço externo ou infraestrutura de produção.
- Não há banco de dados nem segredos obrigatórios. `NODE_ENV` é opcional e aceita `development`, `test` ou `production`.
- Nunca versione `.env`; use `.env.example` como referência sem valores secretos.

## Como rodar localmente

```bash
bun install --frozen-lockfile
bun run dev
# Firmware (PlatformIO 6.1.18): no host de desenvolvimento, pio vive em .venv/bin
.venv/bin/pio test -d apps/firmware -e native
.venv/bin/pio run -d apps/firmware -e esp32doit-devkit-v1
bun run firmware:test:web
```

## Como validar (DoD global do repo)

```bash
bun run lint
bunx biome ci .
bun run format:check
bun run spell
bun run architecture:check
bun run knip
bun run docs:check
bun run typecheck
bun run test
bun run build
PATH=$PWD/.venv/bin:$PATH bun run firmware:test
PATH=$PWD/.venv/bin:$PATH bun run firmware:build
bun run firmware:test:web
```

## Como deployar

Painel web no ESP32 (placa via USB): `bun run web:deploy --port /dev/ttyUSB0`
(build web + stage p/ LittleFS + build firmware + `uploadfs` + `upload`).
Somente validação local sem placa: `bun scripts/web-deploy.mjs --build-only`.

## Git & PRs

- A branch inicial é `main`; commits devem seguir Conventional Commits e passam por Commitlint.
- O pre-commit verifica segredos staged, executa lint-staged e valida a documentação CASA.
- Remote `origin`: github.com/gugacarbo/esp32-computador-de-bordo. Proteção de branch ainda não ativada.

## Gotchas

- Use `bunx` para CLIs locais; `bun install --frozen-lockfile` confere o lockfile canônico `bun.lock`.
- Painel do firmware: specs Playwright reais em `apps/firmware/web-tests/` (`bun run firmware:test:web`, config própria `playwright.firmware.config.ts`) cobrem o HTML legado de `dashboard.h` (fallback quando o LittleFS está vazio). O `test:e2e` da raiz segue sem specs do app web (`apps/web/tests/e2e/`).
- `pio` não está no PATH do host: use `.venv/bin/pio` ou exporte `PATH=$PWD/.venv/bin:$PATH` antes dos scripts `firmware:*`. Os testes web rodam sob Node, não Bun (probe registrado `Bun.serve` indisponível no runner).
- `spell:fix` não altera arquivos: CSpell reporta palavras para revisão manual.
- `bun run build`, `typecheck` e `test` delegam aos workspaces com filtros Bun; cada novo workspace deve declarar esses scripts.
- Para checar a arquitetura, rode `bun run architecture:check`; o checker percorre fontes TypeScript/TSX em `apps` e `packages`.

## Mapa do repositório

| Caminho          | Conteúdo                                                                                                |
| ---------------- | ------------------------------------------------------------------------------------------------------- |
| `apps/web/`      | Painel React+Vite (Tailwind/shadcn mínimo, WS porta 81 + fallback HTTP), testes Bun e stage p/ LittleFS |
| `apps/firmware/` | Firmware PlatformIO do ESP32: OBD, API, painel LittleFS + WS, testes nativos e web                      |
| `packages/`      | Pacotes compartilhados; crie um workspace quando houver consumidores reais                              |
| raiz             | Workspaces Bun, lockfile, ferramentas, scripts globais, CI e documentação CASA                          |

## Mapa de contexto

Ainda não há capítulos em `docs/context/`. Crie um capítulo apenas quando contexto durável ultrapassar este router.

| Capítulo       | Quando carregar |
| -------------- | --------------- |
| (nenhum ainda) | —               |

## Mapa de docs

- Decisões: `docs/adr/` · Comportamento: `docs/specs/` (READMEs GERADOS — não editar)
- Validar: `scripts/docs-check` · Regenerar índices: `scripts/docs-check --emit-index`
