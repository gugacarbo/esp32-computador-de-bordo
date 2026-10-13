---
status: draft
date: 2026-10-13
builds-on: [ADR-0002, ADR-0003, ADR-0004]
implemented-by: []
---

# Coleta sensores OBD e recupera a conexão

## Objetivo

Validar a leitura de parâmetros do Chevrolet Onix Joy 2019 flex pelo ESP32 DOIT DevKit com
ESP32-WROOM-32 e o scanner ELM327 Bluetooth existente, mantendo o painel disponível em falhas.
RPM, velocidade e temperatura do líquido de arrefecimento são obrigatórios para concluir esta entrega.
Borboleta, MAP e tensão são consultados quando disponíveis; ausência de suporte não impede o aceite.

O comportamento foi aprovado em 2026-10-13. O estado documental permanece `draft` até existir DoD
executável específico, com testes e toolchain do firmware. Esta Spec não descreve firmware já entregue.

## Fluxo

1. Inicializar o estado sem leituras e disponibilizar rede e HTTP sem aguardar o adaptador.
2. Tentar conectar ao scanner configurado por MAC ou nome, usando o PIN quando necessário.
3. Validar comunicação com o ELM327 e tentar identificar automaticamente o protocolo do carro.
4. Consultar os mapas de suporte dos PIDs necessários, sem inferir suporte de outro veículo.
5. Coletar um sensor de cada vez e publicar apenas respostas concluídas com sucesso.
6. Tratar erro ou timeout por transação, conservar a última leitura e seguir atendendo HTTP.
7. Recuperar o acesso ao adaptador ou à ECU sem reiniciar o ESP32 e retomar a coleta.

## Contrato

### Sensores

| Campo             | Origem                                                                        | Unidade | Obrigatório para aceite |
| ----------------- | ----------------------------------------------------------------------------- | ------- | ----------------------- |
| `rpm`             | Serviço 01, PID 0C                                                            | rpm     | Sim                     |
| `speedKph`        | Serviço 01, PID 0D                                                            | km/h    | Sim                     |
| `coolantC`        | Serviço 01, PID 05                                                            | °C      | Sim                     |
| `throttlePercent` | Serviço 01, PID 11, posição absoluta da borboleta                             | %       | Não                     |
| `mapKpa`          | Serviço 01, PID 0B, pressão absoluta do coletor                               | kPa     | Não                     |
| `voltage`         | `AT RV`, tensão de entrada do adaptador; ou Serviço 01, PID 42, tensão da ECU | V       | Não                     |

A posição da borboleta não será apresentada como posição do pedal. O PID MAP fornece pressão absoluta,
sem conversão silenciosa para pressão relativa. A origem padrão de tensão é `AT RV`; usar o PID 42
é uma configuração explícita, condicionada ao suporte, e a API identifica a origem conforme SPEC-0002.

### Estado de comunicação

- `btConnected`: conexão SPP ativa com o scanner.
- `adapterReady`: inicialização concluída e adaptador disponível para consultas.
- `ecuResponding`: houve resposta OBD válida recente, dentro da janela de saúde configurada.
- `obdConnected`: conjunção dos três estados, conforme contrato da SPEC-0002.

Conectar ao Bluetooth não comprova comunicação com a ECU. Uma resposta de `AT RV` também não comprova
que o carro responde. A perda do transporte invalida imediatamente a sessão do adaptador e da ECU.
Um erro isolado de sensor não derruba a sessão inteira; a falta de respostas recentes da ECU remove
`ecuResponding`, mesmo se o scanner continuar conectado e alimentado.

### Polling e recuperação

- Manter somente uma consulta pendente e avançar após sucesso, erro ou timeout.
- Agendar sensores por intervalos centralizados, com temperatura e tensão menos frequentes.
- Medir latência e taxa efetiva; 5–10 Hz para RPM/borboleta/MAP, 5 Hz para velocidade e 1 Hz para
  temperatura/tensão são referências de desempenho, não critérios obrigatórios de aceite.
- Não acumular consultas vencidas para executar uma rajada; retomar o agendamento a partir do instante atual.
- Quando um mapa de suporte válido declarar um PID ausente, não consultá-lo repetidamente durante a sessão.
  Timeout ou `NO DATA` isolado não equivalem a prova de PID não suportado.
- Reavaliar suporte após uma nova inicialização bem-sucedida; falha na consulta de suporte mantém
  suporte desconhecido e é diagnosticada como falha, não como incompatibilidade confirmada.
- Usar intervalo de recuperação configurado e crescente, limitado por um teto; não fazer tentativas
  contínuas nem reiniciar a placa como mecanismo de recuperação.
- Carro desligado com Bluetooth ativo provoca espera controlada pela ECU, sem reconexão Bluetooth incessante.
- Logs Serial registram transições, inicialização, protocolo, falhas, PID ausente e recuperação.
  O nível padrão não imprime cada leitura nem repete um mesmo erro em cada iteração.

### Configuração

Centralizar identidade Bluetooth, MAC opcional, PIN opcional, timeout OBD, intervalos por sensor,
limites de recuperação, janela de saúde da ECU, prazo de frescor por sensor, origem da tensão e nível de log.
Os valores não ficam espalhados em números mágicos. A conexão real do scanner valida essas configurações;
o firmware não presume que `OBDII` ou `1234` funcionem em qualquer unidade.

### Limite de comandos

O firmware pode enviar comandos AT necessários para inicializar e configurar o próprio adaptador,
detectar e consultar o protocolo e ler sua tensão. No veículo, a lista permitida é Serviço 01 para
os sensores desta Spec e os mapas de suporte 00, 20 e 40 necessários a eles. Não existe entrada de usuário
para adicionar comandos. Reset AT do adaptador não é reset da ECU.

Mode 04, apagamento de DTCs, programação, reset de ECU, testes de atuadores, comandos proprietários GM,
monitoramento bruto e envio de mensagens CAN arbitrárias ficam fora do produto. Ler por OBD envolve
enviar requisições de diagnóstico; a restrição é não alterar o veículo, não fazer escuta passiva do CAN.

## Casos de borda

| #   | QUANDO                                                   | O sistema DEVE                                                                              |
| --- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| 1   | Não há scanner disponível no boot                        | Servir rede e HTTP; manter sensores sem leitura e tentar conectar com intervalo controlado. |
| 2   | O pareamento falha ou o MAC/nome não corresponde         | Registrar a fase e falha sem travar, mantendo as tentativas controladas.                    |
| 3   | Bluetooth está ativo, mas o carro desligado não responde | Diferenciar scanner e ECU; tratar timeout sem derrubar HTTP.                                |
| 4   | Um mapa válido indica PID ausente                        | Marcar somente esse sensor como não suportado e retirá-lo do polling da sessão.             |
| 5   | Uma consulta dá timeout, resposta inválida ou NO DATA    | Registrar falha, conservar a última leitura e concluir a transação sem atribuir zero.       |
| 6   | Bluetooth desconecta durante uma consulta                | Invalidar a sessão, abandonar a transação e recuperar sem reiniciar a placa.                |
| 7   | Scanner e ECU voltam a responder                         | Reinicializar a sessão, reavaliar suporte e publicar leituras novas.                        |
| 8   | O scanner demora mais que os intervalos pretendidos      | Reduzir a taxa efetiva sem rajada de consultas ou starvation dos sensores lentos.           |
| 9   | O sensor retorna um zero legítimo                        | Publicar zero como leitura válida, com timestamp de sucesso.                                |
| 10  | A unidade não aceita SPP ou se confirma BLE              | Registrar incompatibilidade de transporte sem iniciar migração silenciosa para BLE.         |
| 11  | A recuperação demora enquanto o último valor envelhece   | Permitir que HTTP aplique a expiração do valor, mesmo sem nova publicação do produtor.      |

## Questões em aberto

Nenhuma decisão de produto permanece aberta. Transporte SPP, pareamento, suporte de PIDs e taxa efetiva
são hipóteses de hardware a verificar, não garantias de compatibilidade.

## Definition of Done

Para promover a `accepted`, provisionar a toolchain do firmware e registrar comandos reais para a matriz
abaixo. Não usar o teste de startup Bun como prova de coleta ou recuperação OBD. Os comandos e suites
específicos ainda não existem; esta é a razão de permanecer `draft`.

| Validação necessária                                                          | Casos cobertos | Critério binário                                                                               |
| ----------------------------------------------------------------------------- | -------------- | ---------------------------------------------------------------------------------------------- |
| Testes determinísticos de sessão, respostas e relógio com transporte simulado | 1–7, 9, 11     | Todos passam; timeouts e recuperação não exigem espera real nem hardware.                      |
| Testes do escalonador e de tentativa de recuperação                           | 1–3, 6–8       | Uma consulta pendente, espera controlada e sensores lentos atendidos.                          |
| Teste de expiração durante pausa do produtor, integrado à API                 | 3, 6, 11       | O consumidor identifica leitura vencida sem aguardar o produtor.                               |
| Compilação do alvo DOIT com dependências fixadas                              | Todos          | Build do firmware termina sem erro.                                                            |
| Inspeção da lista de comandos enviados                                        | 4–5, 10        | Somente comandos AT e consultas do Serviço 01 permitidos nesta Spec.                           |
| Ensaio no Onix com evidência Serial e HTTP                                    | 1–3, 6–10      | RPM, velocidade e temperatura lidos; recuperação sem reboot; resultados opcionais registrados. |

Na conclusão, registrar comandos, versões, parâmetros de tempo usados, resultado por caso e os paths
reais em `implemented-by`; não promover a `implemented` com ensaio físico pendente.

## Revisão humana

- Testar sem scanner, com carro desligado, ignição ligada, motor ligado e retirada/recolocação do scanner.
- Confirmar velocidade zero parado, RPM acompanhando o motor e variação plausível da temperatura.
- Conferir borboleta/MAP quando suportados e identificar a origem da tensão, sem exigir precisão de bancada.
- Fazer testes parado; ajuste e observação do celular não são atividades do motorista em movimento.
- Registrar incompatibilidades do scanner e o protocolo efetivamente detectado, sem inferir dados proprietários GM.

## Verificação

Somente a documentação foi criada. Não houve compilação, gravação ou teste no carro nesta tarefa.
A evidência de implementação será registrada no fechamento desta Spec.

## Fora do escopo

Histórico, persistência, consumo de combustível calculado, autonomia, DTCs, alertas de condução,
instalação permanente, circuito CAN direto e suporte a outros veículos.
