---
status: accepted # draft | accepted | implemented | deprecated
date: 2026-10-20
builds-on: [ADR-0018, ADR-0005, ADR-0011] # ADRs que fundamentam. A spec CONSOME decisões, não as redefine.
implemented-by: [] # paths reais (código, migrations, functions) — preenchido no fechamento
---

<!-- id é DERIVADO do filename (docs/specs/NNNN-titulo-kebab.md → SPEC-NNNN);
     title é DERIVADO do H1 abaixo. -->

# Exibe e baixa o histórico de logs persistido no LittleFS

> Convenções compartilhadas (envelope de erro, autorização, acesso a dados):
> `docs/context/CONVENTIONS.md`. Esta spec não as repete — só desvia delas
> explicitamente quando necessário.

## Objetivo

Com a placa ligada no veículo, o operador abre a seção "Diagnóstico" do painel e lê não só os logs
da sessão atual (buffer em RAM) como também as linhas `warn`+ das sessões anteriores, que
sobreviveram ao reboot. Pode baixar o histórico completo como um arquivo de texto para arquivar
fora da placa.

## Fluxo

1. O firmware acumula em RAM as linhas `warn`+ (`error`/`warn` e o resumo de boot) oferecidas pelo
   `logf()` e pelo boot.
2. A cada ~60 s, o `loop()` grava o lote no segmento mais novo do LittleFS (`/logs.0.txt`),
   rotacionando os segmentos quando o teto de 12 KB é atingido e descartando o mais antigo quando a
   cota de 48 KB estoura.
3. O painel consulta `GET /api/logs` a cada ~3 s enquanto a seção está aberta e mostra as entradas
   da sessão atual (mais recentes primeiro) seguidas do histórico persistido.
4. O operador clica em "Baixar histórico"; o painel abre `GET /api/logs/download`, que devolve o
   dump cru em `text/plain` com `Content-Disposition: attachment`.

## Contrato

### `GET /api/logs` (JSON, `Cache-Control: no-store`)

Mantém o contrato da SPEC-0002 e é estendido com os campos do histórico (ADR-0018):

```json
{
  "uptimeMs": 123456,
  "dropped": 0,
  "droppedLack": 0,
  "persisted": 512,
  "entries": [
    { "uptimeMs": 120000, "level": "warn", "tag": "bt", "message": "falha" }
  ],
  "history": [
    { "level": "error", "tag": "boot", "message": "boot #12, reset: brownout" }
  ]
}
```

- `entries`: buffer em RAM, mais antiga → mais nova, com `uptimeMs` (não muda).
- `history`: linhas do arquivo, mais antiga → mais nova; o carimbo UTC vem no próprio texto e o
  painel mostra `level`/`tag`/`message`.
- `droppedLack`: linhas `warn`+ perdidas por falta de espaço/lote cheio (0 sem persistência).
- `persisted`: bytes gravados no total (0 sem persistência).

### `GET /api/logs/download` (text/plain)

- 200 `text/plain; charset=utf-8` + `Content-Disposition: attachment; filename="onix-logs.txt"` +
  `Cache-Control: no-store`; corpo = concatenação dos segmentos, mais antigo → mais novo.
- Sem histórico injetado (build/teste sem persistência), a rota é desconhecida (404), igual às
  demais rotas opcionais.
- Método ≠ GET: 405 com `Allow: GET`.

### Painel

- Botão "Baixar histórico" (link `download`) visível quando a seção tem dados; indica os bytes
  persistidos.
- Bloco "Histórico persistido (sessões anteriores)" abaixo das entradas da sessão atual.
- Aviso quando `droppedLack > 0`.

## Casos de borda

| #   | QUANDO ⟨gatilho⟩                                                         | o sistema DEVE ⟨resposta⟩                                                                |
| --- | ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------- |
| 1   | um `logf` de nível `info` ou `debug` é emitido                           | NÃO entrar no lote do arquivo (fica só na RAM/Serial)                                    |
| 2   | um `logf` de nível `warn` ou `error` é emitido                           | entrar no lote em RAM sem fazer I/O                                                      |
| 3   | o lote em RAM enche antes do flush                                       | perder a linha mais nova e contá-la em `droppedLack`                                     |
| 4   | o LittleFS está desmontado (OTA de `U_SPIFFS`) ou com pouco espaço livre | o flush pular o lote, mantê-lo pendente e não corromper o FS                             |
| 5   | ocorre um boot                                                           | o resumo de boot (`boot #N, reset: …`) entrar no histórico mesmo em boot normal          |
| 6   | o segmento mais novo atinge 12 KB                                        | rotacionar 0→1→…→N-1 e reescrever no segmento 0                                          |
| 7   | a cota de 48 KB estoura                                                  | descartar o segmento mais antigo antes de gravar                                         |
| 8   | `GET /api/logs` sem histórico injetado                                   | responder o envelope do buffer em RAM, sem os campos `history`/`droppedLack`/`persisted` |
| 9   | `GET /api/logs/download` sem histórico injetado                          | responder 404                                                                            |
| 10  | `GET /api/logs/download` com método ≠ GET                                | responder 405 com `Allow: GET`                                                           |
| 11  | o painel abre a seção "Diagnóstico"                                      | consultar `GET /api/logs` a cada ~3 s e exibir sessão atual + histórico persistido       |
| 12  | o operador clica em "Baixar histórico"                                   | abrir `GET /api/logs/download` como download                                             |

## Questões em aberto

- Nenhuma bloqueia esta spec. A reconciliação de um segmento truncado durante a rotação (possível
  corte de energia) fica fora do escopo e é documentada na ADR-0018.

## Definition of Done

```bash
PATH=$PWD/.venv/bin:$PATH bun run firmware:test        # test_log_file e test_http verdes — casos 1-10
bun run --filter '@esp32-computador-de-bordo/web' test # logs.test.ts verde — casos 8-9, 11-12
bun run docs:check                                     # 0 erro(s)
```

## Revisão humana

- Ler o painel no celular com histórico real de um reboot para julgar legibilidade e ruído.
- Confirmar que a cota de 48 KB não aperta o bundle do painel no LittleFS após vários ciclos.

## Verificação

<!-- Preenchida no FECHAMENTO (transição para implemented, mesmo commit que
     preenche implemented-by): evidência do DoD — comandos rodados + resultado. -->

```text
(preencher no fechamento)
```

<!-- Checklist de fechamento (um commit):
     [ ] DoD verde, evidência acima
     [ ] status: implemented + implemented-by com paths reais
     [ ] gotchas novos → AGENTS.md
     [ ] estado atual novo → capítulo de contexto pertinente
     [ ] scripts/docs-check --emit-index (READMEs regenerados) -->
