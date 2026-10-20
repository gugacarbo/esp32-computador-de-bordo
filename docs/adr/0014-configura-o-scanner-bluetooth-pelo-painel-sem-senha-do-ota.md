---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0003, ADR-0012, ADR-0013] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Configura o scanner Bluetooth pelo painel sem senha do OTA

## Contexto e problema

Nome, PIN e MAC do scanner ELM327 eram constantes de `config.h`: trocar de adaptador exigia
recompilar e gravar o firmware. Muitos clones usam nome ou PIN diferentes, e informar o MAC evita a
busca Bluetooth que degrada o Wi-Fi (ADR-0013). Além disso, a ADR-0012 exigia a senha do OTA para
trocar o hotspot pelo painel, o que obrigava o motorista a conhecer um segredo de gravação de
firmware só para configurar a rede.

## Direcionadores da decisão

- Trocar e escolher o scanner pelo celular, sem recompilar.
- Facilitar o uso do MAC, que dispensa a busca por nome.
- Configuração simples: quem está na rede da placa já é o dono do veículo.

## Opções consideradas

### Opção 1 — manter constantes e senha do OTA

**Prós:** nenhuma mudança; rede protegida por um segundo segredo.
**Contras:** trocar de scanner exige recompilar; a senha do OTA vira senha de configuração.

### Opção 2 — configuração na NVS, busca pelo painel e escrita sem senha

**Prós:** escolha do scanner numa lista; aplica sem reiniciar; uma senha a menos para o motorista.
**Contras:** qualquer cliente conectado ao SoftAP ou ao hotspot pode trocar o scanner e a rede.

### Opção 3 — configuração na NVS protegida pela senha do OTA

**Prós:** mantém o segundo segredo.
**Contras:** mesma fricção da ADR-0012, que o mantenedor decidiu remover.

## Decisão

Adotar a Opção 2. Nome, PIN e MAC do scanner ficam na NVS (namespace `obd`) e são editados por
`GET/POST/DELETE /api/obd/config`; os padrões de `config.h` valem enquanto não houver configuração
gravada. `POST /api/obd/scan` pede à task OBD uma busca de ~5 s, cujo resultado (nome, MAC, RSSI)
sai em `GET /api/obd/config`. Salvar aplica sem reiniciar: o transporte esquece o MAC achado,
reaplica o PIN e a sessão reconecta. `/api/wifi` deixa de exigir a senha do OTA; a senha continua só
no espota (gravação de firmware por Wi-Fi).

## Consequências

- A proteção das escritas de configuração passa a ser só a senha da rede (SoftAP ou hotspot).
- A busca só roda com o scanner desconectado (estado `busy` caso contrário) e ocupa o rádio por
  ~5 s, derrubando o Wi-Fi nesse intervalo.
- Dispositivos que não anunciam nome aparecem só com o MAC.
- O firmware passa a usar 96,3% do slot OTA.

## Confirmação

```bash
PATH=$PWD/.venv/bin:$PATH bun run firmware:test
.venv/bin/pio run -d apps/firmware -e esp32doit-devkit-v1
bun run test
```

Na placa: "Buscar" na seção "Scanner Bluetooth" lista dispositivos próximos; escolher um e salvar
grava na NVS, `/api/logs` registra a nova configuração e a placa tenta conectar sem reiniciar.
