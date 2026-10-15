---
status: superseded
date: 2026-10-13
builds-on: [ADR-0002, ADR-0004]
superseded-by: ADR-0007
deciders: ["project maintainer"]
---

# Serve painel offline por HTTP com polling

> VERDADE ATUAL: superado pela ADR-0007. O painel é React+Vite servido do LittleFS com push
> WebSocket na porta 81 (fallback para este polling HTTP); o HTML em PROGMEM segue apenas como
> fallback quando o filesystem está vazio.

## Contexto e problema

O consumidor do MVP é o navegador de um celular Android próximo ao ESP32, durante testes no carro.
O produto precisa funcionar sem internet e mostrar o último estado conhecido dos sensores.
Não existe necessidade aprovada de histórico, aplicativo nativo ou atualização por evento.

## Direcionadores da decisão

- Servir conteúdo e dados no próprio ESP32, sem computador externo.
- Usar uma interface leve e simples, sem dependências remotas.
- Desacoplar frequência de consultas do carro e frequência de atualização da página.
- Manter dois recursos HTTP e configuração de rede centralizada.

## Opções consideradas

### Usar aplicativo Android ou serviço remoto

**Prós:** oferece recursos de plataforma e ferramentas de interface mais extensas.
**Contras:** amplia distribuição, dependências e infraestrutura além da validação local aprovada.

### Usar painel web com WebSocket

**Prós:** entrega mudanças por evento sem consultas periódicas.
**Contras:** exige gerenciar conexões persistentes e reconexão sem uma necessidade demonstrada.

### Usar painel web local com polling HTTP

**Prós:** entrega conteúdo offline e consulta snapshots com protocolo e depuração simples.
**Contras:** acrescenta requisições periódicas e não entrega cada amostra OBD à interface.

## Decisão

Usar WiFi em modo SoftAP e WebServer do Arduino-ESP32, com HTML, CSS e JavaScript locais.
O navegador consulta periodicamente o snapshot HTTP; o servidor não consulta o veículo para responder
à página. Os contratos das rotas e da rede pertencem à SPEC-0002 e o fluxo da interface à SPEC-0003.

Incorporar o HTML e seus recursos ao firmware, preferindo conteúdo constante na flash para evitar uma
etapa separada de gravação de filesystem. Não usar framework frontend, CDN, servidor Bun de produção
ou conexão WebSocket no MVP. A interface usa uma apresentação simples em cartões, com estados de
carregamento, ausência de leitura, dado desatualizado e erro definidos na SPEC-0003, sem introduzir
uma biblioteca de design system.

## Consequências

- Página e API compartilham a mesma origem e funcionam sem acesso à internet.
- Mudar o conteúdo da interface exige recompilar e gravar o firmware nesta etapa.
- O ritmo de polling web é independente do escalonamento OBD.
- O navegador não recebe um canal para enviar comandos arbitrários ao veículo.
- A coexistência SoftAP/Bluetooth precisa de validação física; o compartilhamento do rádio pode afetar a latência.
- Uma necessidade futura demonstrada de WebSocket ou filesystem requer reavaliar esta escolha.

## Confirmação

Inspecionar que o HTML, CSS e JavaScript são locais, que as respostas vêm da RAM e que não há
endpoints de execução de comandos. Na validação da SPEC-0003, abrir o painel sem internet e
conferir no navegador a ausência de requisições externas e de polling sobreposto.

## Notas

- [Wi-Fi no Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html).
- [WebServer do Arduino-ESP32](https://github.com/espressif/arduino-esp32/tree/master/libraries/WebServer).
- [Coexistência de rádio no ESP32](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/coexist.html).

A documentação da Espressif classifica SoftAP conectado com Bluetooth Classic como suportado com
possível instabilidade de desempenho. A validação física faz parte da entrega, não é uma garantia inferida.
