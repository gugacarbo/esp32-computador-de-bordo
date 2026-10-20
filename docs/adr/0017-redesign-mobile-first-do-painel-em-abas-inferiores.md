---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0007, ADR-0010, ADR-0015, ADR-0016] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Redesign mobile-first do painel em abas inferiores

## Contexto e problema

O painel React foi desenhado como um dashboard de desktop (`max-w-5xl`) colapsado para
mobile: coluna única com telemetria, status, configurações de Wi-Fi, Bluetooth e
diagnóstico empilhadas com scroll longo. Com o celular preso ao suporte do veículo,
o uso dominante é dirigindo — os números precisam ser legíveis de relance, e as
configurações ficam fora do caminho. Ainda: decisões de materialização tomadas antes
do primeiro ensaio físico evitam redesenhar depois de implantar o painel no carro.

## Direcionadores da decisão

- Uso dominante em movimento: velocidade e RPM legíveis de relance, sem interação.
- Estado do OBD sempre visível, em qualquer aba.
- Sem rolagem horizontal em 320px (mesma barreira dos testes do painel legado).
- Bundle único servido por LittleFS (ADR-0010): sem router, sem code splitting.
- Tema único escuro e tokens semânticos (ADR-0015) continuam valendo; só os valores
  mudam, em um `@theme` só.
- Cobertura de design em camadas (ADR-0016): stories e atualização de snapshots
  acompanham o redesign.

## Opções consideradas

### Opção 1 — scroll único refinado

Apenas reordenar para mobile: telemetria acima, config compactada abaixo.

**Prós:** menor esforço; navegação por rolagem.
**Contras:** configuração permanece a um scroll acidental de distância durante a
condução; hierarquia de relance continua competindo com painéis longos.

### Opção 2 — bloco fixo de telemetria + drawer de configuração

Telemetria fixa, fora do fluxo de rolagem; config em gaveta/sheet acionado por
botão.

**Prós:** próximo do layout atual.
**Contras:** endereça a visão de dirigir, mas mantém config acoplada à mesma tela;
gaveta escondendo painéis complexos (Wi-Fi, Bluetooth) piora a descoberta.

### Opção 3 — abas inferiores (barra fixa na base)

Barra `tablist` fixa na base com três abas: **Telemetria** (herói de velocidade e
RPM + sensores secundários), **Conexão** (scanner BT e aviso de busca pausada) e
**Ajustes** (Wi-Fi e diagnóstico). Troca por estado local; chip do estado OBD
persiste acima do conteúdo em qualquer aba.

**Prós:** config some da visão de dirigir; herói dá os números de relance; segue
padrões de apps mobile que o motorista já conhece; sem rotas, o bundle não muda de
forma.
**Contras:** reestrutura `App.tsx`; snapshots visuais precisam de atualização.

## Decisão

Adotar a Opção 3. O `apps/web/src/App.tsx` passa a ter:

- `AppView` exportado: corpo declarativo por aba (verificável por SSR em
  `App.test.tsx`), com `App` mantendo o estado da aba via `useState` e o hook
  `useTelemetry`;
- barra de abas em `components/ui/tab-bar.tsx` — `<div role="tablist">` com botões
  `role="tab"` (`tab-<id>`), `aria-selected`, `aria-controls` (`panel-<id>`) e alvo
  de toque `min-h-touch`; aba ativa em `text-accent`;
- herói de relance na Telemetria: velocidade em `Stat` de tamanho `xl` (novo maior
  tamanho, `text-6xl`) e RPM em `lg`, com traço quando não há leitura válida;
  sensores secundários (coolant, borboleta, MAP, tensão) em `SensorCard`;
- estado do OBD fixo no cabeçalho (`#panel-status` com ponto colorido e
  `PANEL_TEXT`), presente em todas as abas; transporte (WS/polling) e uptime ficam
  na Telemetria;
- Conexão: `BluetoothPanel` + `ScannerReconnect`; Ajustes: `WifiPanel` +
  `DiagnosticsPanel`.

Os valores dos tokens de ADR-0015 são reescritos em `oklch` no
`apps/web/src/index.css` (fundo azul-quase-preto, texto quase branco, accent
elétrico, status vivo) mantendo nomes, `color-scheme: dark`, raio e alvo de toque.
Componentes continuam usando só as classes dos tokens.

## Consequências

- Painel legado (`apps/firmware/include/dashboard.h`) permanece fallback intocado;
  nenhuma mudança de firmware ou de envelope.
- Sem router no bundle; navegação é estado local e os ids `tab-*`/`panel-*`
  estabilizam a a11y.
- Configuração fica a um toque, mas fora do fluxo de dirigir.
- Snapshots visuais das stories (ADR-0016) mudam e devem ser atualizados no PR com
  `bun run design:visual -- --update-snapshots`.

## Confirmação

```bash
bun run design:check && bunx biome ci apps/web/src
bun test apps/web/src/App.test.tsx apps/web/src/lib/tabs.test.ts apps/web/src/components/ui/tab-bar.test.tsx apps/web/src/components/ui/stat.test.tsx
bun run typecheck
```

Na revisão manual, o painel em 320px sem rolagem horizontal e com as três abas no
server de preview local confirma o comportamento de relance.

## Notas

O herói expõe os mesmos dados do envelope (nenhum campo novo); se o relance
exigir agregações (ex.: marcha, volta), decisão fica para issue futura.
