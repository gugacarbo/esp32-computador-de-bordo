---
status: accepted
date: 2026-10-13
builds-on: [ADR-0002, ADR-0003]
superseded-by: null
deciders: ["project maintainer"]
---

# Isola OBD em uma task e compartilha leituras em memória

## Contexto e problema

O mantenedor confirmou a necessidade de atendimento HTTP durante falhas e reconexões. A leitura
incremental da ELMduino não torna toda a biblioteca assíncrona: sua inicialização executa comandos
bloqueantes e a busca automática do protocolo usa uma espera de até 30 segundos. As chamadas de
conexão do BluetoothSerial também aguardam eventos internamente.
Uma máquina de estados ao redor dessas chamadas no mesmo loop não elimina essas esperas.

## Direcionadores da decisão

- Separar as esperas do adaptador do atendimento web.
- Conservar uma única consulta OBD pendente e reconexões controladas.
- Compartilhar leituras sem acesso concorrente à biblioteca ELM.
- Limitar memória e concorrência, sem banco, histórico ou arquitetura genérica.

## Opções consideradas

### Executar tudo no mesmo loop

**Prós:** menor complexidade de concorrência e bom encaixe com o polling incremental.
**Contras:** as chamadas bloqueantes suspendem o atendimento do WebServer na conexão e inicialização.

### Reimplementar conexão e inicialização com APIs assíncronas

**Prós:** permite uma máquina de estados única sem task adicional.
**Contras:** aumenta o código próprio e o acoplamento a detalhes de Bluetooth e ELM327.

### Criar uma task OBD e compartilhar um snapshot em memória

**Prós:** isola esperas preservando as bibliotecas e mantém os dois responsáveis explícitos.
**Contras:** exige sincronização curta, ownership definido e atenção ao watchdog e à memória.

## Decisão

Criar uma única task de aplicação para conexão, inicialização, polling e recuperação OBD.
O loop principal cuida de Wi-Fi e WebServer. As tasks internas do framework não entram nessa contagem.
A task OBD é a única proprietária de BluetoothSerial e ELMduino e publica o estado em uma estrutura
fixa na RAM, contendo a última leitura bem-sucedida e os metadados de cada sensor.

Proteger somente a cópia ou atualização dessa estrutura com um mutex de duração curta. A camada HTTP
copia o snapshot, libera a proteção e então calcula idades e serializa o resultado. Nunca manter o
mutex durante conexão, consulta OBD, espera de resposta, log ou envio HTTP. O leitor aplica o critério
de expiração mesmo quando o produtor estiver aguardando uma chamada bloqueante.

Dentro da task OBD, usar polling incremental com uma transação pendente, intervalos por sensor e
espera cooperativa quando não houver trabalho. Não criar uma task por sensor, por rota ou por cliente.

## Consequências

- A biblioteca ELM não precisa oferecer concorrência interna.
- O snapshot preserva valores válidos legítimos, incluindo zero, e não acumula histórico.
- Uma resposta HTTP contém uma cópia coerente do estado, mas as leituras têm instantes distintos.
- Reiniciar o ESP32 apaga as leituras; armazenamento persistente não entra nesta decisão.
- Reconexão e frescor continuam sendo contratos observáveis definidos nas Specs, não efeitos presumidos do uso de tasks.
- Tasks separam esperas de software, mas não eliminam disputa do rádio entre Wi-Fi e Bluetooth.

## Confirmação

Inspecionar a criação de uma única task OBD, a propriedade exclusiva das bibliotecas e os limites do
mutex. Exercitar HTTP durante conexão, busca automática, ausência da ECU e desconexão do adaptador,
com os casos da SPEC-0001 e SPEC-0002. Conferir que a expiração funciona durante uma pausa do produtor.

## Notas

- [Inicialização da ELMduino](https://github.com/PowerBroker2/ELMduino/blob/master/src/ELMduino.cpp).
- [Conexão no BluetoothSerial](https://github.com/espressif/arduino-esp32/blob/master/libraries/BluetoothSerial/src/BluetoothSerial.cpp).
