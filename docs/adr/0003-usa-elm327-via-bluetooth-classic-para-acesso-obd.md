---
status: accepted
date: 2026-10-13
builds-on: [ADR-0002]
superseded-by: null
deciders: ["project maintainer"]
---

# Usa ELM327 via Bluetooth Classic para acesso OBD

## Contexto e problema

O mantenedor possui um scanner anunciado como ELM327 Bluetooth e informou conexão Bluetooth comum.
A placa ESP32-WROOM-32 dispõe de Bluetooth Classic. O transporte escolhido para o MVP é SPP;
a compatibilidade efetiva da unidade e dos PIDs do Onix Joy 2019 flex ainda exige teste físico.
O anúncio não forneceu evidência técnica suficiente para garantir o transporte ou a taxa de consultas.

## Direcionadores da decisão

- Usar os equipamentos disponíveis e a alimentação USB do ESP32.
- Evitar transceiver CAN, circuitos adicionais e decodificação proprietária GM.
- Reutilizar inicialização e interpretação de respostas ELM327.
- Manter o firmware com um único proprietário da comunicação com o adaptador.

## Opções consideradas

### Acessar CAN diretamente

**Prós:** elimina a ponte serial e permite controle sobre o transporte.
**Contras:** exige hardware adicional e amplia substancialmente a responsabilidade do projeto.

### Implementar comandos e parser ELM327 próprios

**Prós:** controle direto sobre inicialização, erros e tempos de espera.
**Contras:** duplica tratamento já disponível e aumenta o custo de validação de respostas.

### Usar BluetoothSerial e ELMduino

**Prós:** reaproveita SPP do Arduino-ESP32 e as consultas OBD da ELMduino.
**Contras:** depende da compatibilidade do scanner e possui chamadas bloqueantes na conexão e inicialização.

## Decisão

Conectar o ESP32 como cliente Bluetooth Classic/SPP, usando BluetoothSerial como transporte e ELMduino
para comunicação ELM327 e interpretação dos parâmetros. Preferir MAC configurado quando conhecido;
nome e PIN ficam disponíveis na configuração centralizada. Não presumir nome ou PIN universal.

Uma única instância da ELMduino atende uma consulta por vez. Usar detecção automática do protocolo
inicialmente e registrar o protocolo encontrado para diagnóstico, sem codificar um protocolo GM presumido.
As esperas das bibliotecas ficam isoladas conforme ADR-0004.

## Consequências

- A confirmação de hardware inclui conexão SPP, pareamento e resposta válida a consultas do carro.
- BLE não é uma troca de configuração de BluetoothSerial: exige transporte GATT e validação do serviço
  e das características do scanner. Uma incompatibilidade será explicitada, sem mudar a arquitetura silenciosamente.
- Trocar a ELMduino por comunicação serial própria exige uma limitação demonstrada e uma nova decisão.
- As capacidades de escrita existentes na biblioteca não ficam expostas pelo produto. A lista permitida
  de consultas e comandos do adaptador pertence à SPEC-0001.
- O número de versão anunciado pelo scanner não substitui teste de compatibilidade.

## Confirmação

Inspecionar o uso de BluetoothSerial e ELMduino na implementação e verificar que somente a task OBD
acessa essas instâncias. No equipamento, testar nome/MAC/PIN configurados e conferir pelo Serial as
fases Bluetooth, adaptador e ECU. Cruzar leituras com um aplicativo Android usado separadamente.

## Notas

- [Biblioteca ELMduino e sua execução incremental](https://github.com/PowerBroker2/ELMduino).
- [Bluetooth no Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/bluetooth.html).
- [Exemplo ESP32 da ELMduino](https://github.com/PowerBroker2/ELMduino/blob/master/examples/ESP32_Bluetooth_Serial/ESP32_Bluetooth_Serial.ino).

Os loops infinitos de falha dos exemplos não constituem comportamento aceito para o produto.
