---
status: accepted
date: 2026-10-13
builds-on: [ADR-0001]
superseded-by: null
deciders: ["project maintainer"]
---

# Adota firmware C++ com PlatformIO no monorepo

## Contexto e problema

O computador de bordo aprovado executa coleta OBD, rede e servidor no ESP32 DOIT DevKit com
ESP32-WROOM-32. O bootstrap Bun/TypeScript executa no host e não fornece um runtime para essa placa.
A ADR-0001 organiza os workspaces do host e deixa a escolha do firmware em aberto. Esta decisão
complementa aquela fronteira, sem substituir o monorepo.

## Direcionadores da decisão

- Aproveitar as bibliotecas Arduino do ESP32 e os exemplos da ELMduino.
- Reproduzir compilação e gravação sem depender da configuração de uma IDE.
- Manter um único repositório e poucas unidades de código.
- Separar as dependências do microcontrolador das ferramentas Bun do host.

## Opções consideradas

### Executar o produto no app Bun existente

**Prós:** aproveita o bootstrap e as ferramentas de testes atuais.
**Contras:** exige um computador externo para executar o app; não atende à execução na placa.

### Usar Arduino IDE para o firmware

**Prós:** configuração inicial simples e ampla oferta de exemplos.
**Contras:** configuração de placa e bibliotecas menos explícita no repositório.

### Usar PlatformIO com Arduino e C++

**Prós:** configuração versionada, suporte à placa e integração com as bibliotecas escolhidas.
**Contras:** acrescenta uma toolchain independente de Bun e exige validar versões compatíveis.

## Decisão

Usar PlatformIO com o framework Arduino e C++ para o firmware, fora dos padrões de workspaces Bun
`apps/*` e `packages/*`. O identificador inicial da placa é `esp32doit-devkit-v1`. O app Bun
existente não passa a ser servidor de produção nem intermediário obrigatório do dispositivo.

A implementação começou com `platformio.ini`, `src/main.cpp` e `include/config.h` em `firmware/` na
raiz; desde 2026-10-14 vive em `apps/firmware/` (ADR-0006) com os mesmos arquivos, e a interface é um
recurso local do firmware conforme ADR-0005.
Separar novos arquivos apenas quando melhorar a leitura ou viabilizar testes relevantes.
Não criar camadas de domínio, serviços e repositórios para este MVP.

## Consequências

- PlatformIO cuida de build e gravação do firmware; Bun permanece responsável pelas ferramentas do host.
- Os comandos Bun do bootstrap não comprovam compilação, gravação nem funcionamento no ESP32.
- Versões de plataforma e bibliotecas serão fixadas na configuração ao validar a primeira compilação.
- Uma integração futura de checks do firmware preserva os checks Bun existentes e explicita a cobertura.
- A localização original `firmware/` na raiz foi substituída por `apps/firmware/` em 2026-10-14; ver ADR-0006.
- O uso aprovado é USB durante testes; alimentação automotiva permanente é uma decisão futura.

## Confirmação

Na implementação, inspecionar `apps/firmware/platformio.ini` para conferir placa, framework e dependências
fixadas; compilar e gravar pela toolchain documentada e conferir o boot pelo Serial. Inspecionar que o
firmware não executa nem depende do runtime Bun. Esta ADR registra a escolha, não a existência desses
arquivos ou a execução desses testes.

## Notas

Decisão aprovada pelo mantenedor na revisão do MVP em 2026-10-13.

- [Configuração oficial da placa no PlatformIO](https://docs.platformio.org/en/latest/boards/espressif32/esp32doit-devkit-v1.html).
- [Framework Arduino no PlatformIO](https://docs.platformio.org/en/latest/frameworks/arduino.html).
