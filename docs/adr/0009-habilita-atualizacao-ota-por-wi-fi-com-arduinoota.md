---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0002, ADR-0004, ADR-0008] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Habilita atualização OTA por Wi-Fi com ArduinoOTA

## Contexto e problema

O firmware e o painel só podiam ser gravados por USB. Com a placa instalada no veículo, cada
atualização exigiria removê-la ou levar um cabo até ela. A tabela `huge_app.csv` tinha um único
slot de aplicação de 3 MB e nenhum `ota_1`, então não havia onde gravar uma imagem nova enquanto a
atual roda. O firmware também não tinha rotina para receber atualizações.

## Direcionadores da decisão

- Atualizar firmware e LittleFS pela rede do SoftAP, sem cabo e sem internet.
- Manter Bluetooth Classic + Wi-Fi, que ocupam cerca de 1,67 MB de aplicação.
- Preservar o painel do ADR-0008 (cerca de 350 KB staged) e o deploy em um comando.
- Impedir que qualquer cliente do SoftAP grave firmware sem credencial.

## Opções consideradas

### Opção 1 — manter só USB

**Prós:** nenhuma mudança de partição; slot de 3 MB com folga grande.
**Contras:** toda atualização exige acesso físico à placa.

### Opção 2 — ArduinoOTA (espota) com tabela de dois slots

**Prós:** biblioteca do framework, integrada ao PlatformIO (`upload_protocol = espota`); grava
firmware e LittleFS; protegida por senha.
**Contras:** reduz cada slot a 1,69 MB e o LittleFS a 512 KB; o host precisa estar no SoftAP e
aceitar a conexão de volta do ESP32.

### Opção 3 — rota HTTP `POST /update` com a biblioteca Update

**Prós:** atualização pelo celular, a partir do painel.
**Contras:** exige upload multipart, autenticação e tela própria; mesma mudança de partições.

## Decisão

Adotar ArduinoOTA com a tabela `apps/firmware/partitions_ota.csv`: `app0` e `app1` de 0x1B0000
(1,69 MB), LittleFS de 0x80000 (512 KB) e o `coredump` preservado. O firmware inicia o OTA no
SoftAP (porta 3232, senha `ONIX_OTA_PASSWORD` em `config.h`), suspende a task OBD durante a
gravação e desmonta o LittleFS antes de sobrescrevê-lo. O env `ota` do PlatformIO e a opção
`--ota` do `web:deploy` gravam por Wi-Fi; a primeira gravação com a nova tabela é por USB.

## Consequências

- Com OTA, o firmware usa 94,6% do slot (cerca de 95 KB livres). Crescer além disso exige
  reduzir o LittleFS ou o firmware, ou abrir mão do OTA.
- O orçamento do painel staged cai de 600 KB para 448 KB, deixando folga para os metadados do
  LittleFS de 512 KB.
- A senha do OTA fica no código-fonte, como a do SoftAP: protege contra clientes casuais do AP,
  não contra quem tem acesso ao repositório.
- O host de deploy precisa entrar na rede `Onix-MVP`; firewall local deve aceitar a conexão TCP
  que o ESP32 abre de volta para o espota.
- A Opção 3 continua possível no futuro sem nova mudança de partições.

## Confirmação

```bash
PATH=$PWD/.venv/bin:$PATH bun run firmware:build
bun run web:deploy --build-only
```

Na placa: gravar uma vez por USB com `bun run web:deploy --port /dev/ttyUSB0`; depois, com o host no
SoftAP, `bun run web:deploy --ota` deve gravar LittleFS e firmware e a placa deve voltar a servir
`http://192.168.4.1`.
