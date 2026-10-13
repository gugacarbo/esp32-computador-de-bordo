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
O app atual é TypeScript executado por Bun e vive em `apps/computador-de-bordo/`.
Bibliotecas compartilhadas ficam em `packages/` quando houver mais de um consumidor.
Firmware e hardware ainda não estão configurados.

## Infra & ambientes

- Workspaces nativos do Bun usam os padrões `apps/*` e `packages/*`; `bun.lock` fica na raiz.
- Apps têm manifesto próprio; mantenha ferramentas comuns e scripts agregadores na raiz.
- Desenvolvimento e checks locais usam Bun 1.2+; CI roda no GitHub Actions com Bun e Node 22.
- O app atual roda no host Bun. Ainda não há integração com placa, protocolo, serviço externo ou infraestrutura de produção.
- Não há banco de dados nem segredos obrigatórios. `NODE_ENV` é opcional e aceita `development`, `test` ou `production`.
- Nunca versione `.env`; use `.env.example` como referência sem valores secretos.

## Como rodar localmente

```bash
bun install --frozen-lockfile
bun run dev
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
```

## Como deployar

Não há destino nem processo de deploy configurado.

## Git & PRs

- A branch inicial é `main`; commits devem seguir Conventional Commits e passam por Commitlint.
- O pre-commit verifica segredos staged, executa lint-staged e valida a documentação CASA.
- Este clone não tem remote configurado. Não é possível abrir PR nem ativar proteção de branch até configurar o repositório remoto.

## Gotchas

- Use `bunx` para CLIs locais; `bun install --frozen-lockfile` confere o lockfile canônico `bun.lock`.
- A configuração Playwright está pronta, mas ainda não há aplicação web, endpoint, servidor ou specs E2E. `bun run test:e2e` não encontra testes até um fluxo real ser implementado.
- `spell:fix` não altera arquivos: CSpell reporta palavras para revisão manual.
- `bun run build`, `typecheck` e `test` delegam aos workspaces com filtros Bun; cada novo workspace deve declarar esses scripts.
- Para checar a arquitetura, rode `bun run architecture:check`; o checker percorre fontes TypeScript/TSX em `apps` e `packages`.

## Mapa do repositório

| Caminho                     | Conteúdo                                                                       |
| --------------------------- | ------------------------------------------------------------------------------ |
| `apps/computador-de-bordo/` | App Bun/TypeScript atual, testes unitários e configuração TS local             |
| `packages/`                 | Pacotes compartilhados; crie um workspace quando houver consumidores reais     |
| raiz                        | Workspaces Bun, lockfile, ferramentas, scripts globais, CI e documentação CASA |

## Mapa de contexto

Ainda não há capítulos em `docs/context/`. Crie um capítulo apenas quando contexto durável ultrapassar este router.

| Capítulo       | Quando carregar |
| -------------- | --------------- |
| (nenhum ainda) | —               |

## Mapa de docs

- Decisões: `docs/adr/` · Comportamento: `docs/specs/` (READMEs GERADOS — não editar)
- Validar: `scripts/docs-check` · Regenerar índices: `scripts/docs-check --emit-index`
