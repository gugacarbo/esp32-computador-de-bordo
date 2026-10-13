---
status: draft
date: 2026-10-13
builds-on: [ADR-0005, SPEC-0002]
implemented-by: []
---

# Exibe o painel offline no Android

## Objetivo

Exibir o estado do Onix no navegador Android conectado ao Wi-Fi do ESP32, com interface simples,
responsiva e totalmente offline. A página apresenta dados já coletados; não controla o veículo.

O comportamento foi aprovado, mas a Spec permanece `draft`: a configuração Playwright do bootstrap
não contém página nem testes deste fluxo. A promoção exige comandos reais que exercitem os estados
abaixo, sem tratar o teste de startup Bun como evidência de interface.

## Fluxo

1. O usuário alimenta o ESP32 por USB durante o teste e conecta o Android à rede definida na SPEC-0002.
2. Abre o endereço local no navegador; o HTML é carregado sem depender do scanner.
3. A página mostra identidade, cartões e estado inicial enquanto busca o primeiro snapshot.
4. O navegador solicita `GET /api/data` e atualiza os textos com valores e estados.
5. Uma falha HTTP ou OBD muda a apresentação; a página permanece aberta e tenta recuperar.

## Contrato

### Conteúdo e apresentação

- Cabeçalho: `ONIX` e `Computador de bordo`.
- Seis cartões: RPM, Velocidade, Temperatura do líquido de arrefecimento, Borboleta, MAP e Tensão.
- Unidades: rpm, km/h, °C, %, kPa e V, respectivamente.
- RPM e velocidade sem casas decimais; temperatura, borboleta e MAP com arredondamento de apresentação
  simples; tensão com uma casa decimal. Arredondar na apresentação, preservando a leitura na API.
- Identificar tensão como `Tensão do adaptador` ou `Tensão da ECU` pelo campo `source`.
- Usar organização que caiba em telas de celular sem rolagem horizontal; validar larguras de 320 e
  360 pixels CSS e uma tela maior. Não exigir gráficos, instrumentos animados ou tela de configuração.
- HTML/CSS/JavaScript locais, sem fontes, imagens, scripts ou estilos de CDN.
- Os estados não dependem somente de cor: têm texto, contraste legível e associação com o sensor.
- A interface não contém botão de apagar erros, executar comandos ou escrever na ECU.

### Estado do painel

| Situação observável                                       | Apresentação                                                                  |
| --------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Ainda não chegou o primeiro snapshot                      | `Carregando dados`, cartões com `—`.                                          |
| API disponível, Bluetooth desconectado                    | `OBD desconectado`, sensores conforme seus estados.                           |
| Bluetooth ativo, adaptador ainda não inicializado         | `Inicializando adaptador`.                                                    |
| Adaptador pronto, ECU sem resposta recente                | `ECU sem resposta`.                                                           |
| `obdConnected: true`                                      | `OBD conectado`, sem presumir que todo sensor é suportado.                    |
| Consulta HTTP falhou ou resposta não tem estrutura válida | `Sem comunicação com o ESP32`; retirar a apresentação de valores como atuais. |

Uma falha web tem precedência sobre o último estado OBD recebido: não manter `OBD conectado` como se
houvesse uma confirmação nova. Quando a API volta, aplicar a resposta e recuperar o estado automaticamente.

### Estado dos cartões

| Estado na API                                      | Apresentação                                                                                      |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `valid: true`, `status: ok`                        | Valor e unidade, incluindo zero legítimo.                                                         |
| `unsupported`                                      | `Não suportado`, sem fabricar valor.                                                              |
| `unavailable` sem leitura anterior                 | `—` e `Sem leitura`.                                                                              |
| `timeout` ou `error`                               | `Falha na leitura`; último valor somente com indicação explícita de leitura anterior.             |
| `stale`                                            | `Desatualizado`; último valor identificado como leitura anterior.                                 |
| Origem offline ou falha HTTP, com leitura anterior | Mostrar `—` como valor atual; o último valor pode aparecer secundariamente como leitura anterior. |

A existência de `value` não implica validade. Se `valid` for falso, o último valor nunca terá a mesma
apresentação de uma leitura atual. Uma combinação de validade e status inconsistente ou ausência de
campos obrigatórios é resposta inválida e segue o tratamento de falha HTTP.

### Atualização

- Intervalo web inicial de aproximadamente 500 ms, independente das taxas de coleta OBD.
- No máximo uma requisição em andamento; agendar a próxima após a anterior terminar, sem rajada para
  compensar atrasos. O navegador encerra uma requisição que ultrapasse seu timeout configurado.
- Aplicar uma resposta somente após conferir a estrutura necessária; falha de parsing não apaga silenciosamente a tela.
- Continuar tentando durante falhas e recuperar sem recarregar a página.
- Não acumular intervalos, requisições ou listeners ao alternar a aba ou recuperar a conexão.
- Interrupção ou troca da rede no Android não gera valores simulados nem dependência de internet.

## Casos de borda

| #   | QUANDO                                                          | A página DEVE                                                                    |
| --- | --------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| 1   | Abre sem scanner e antes do primeiro JSON                       | Mostrar estrutura, carregamento inicial e depois estado offline, sem tela vazia. |
| 2   | O sensor ainda não foi lido ou não é suportado                  | Diferenciar ausência de leitura de ausência de suporte.                          |
| 3   | O sensor retorna zero válido                                    | Exibir `0` com unidade, sem substituí-lo por `—`.                                |
| 4   | A leitura fica inválida, vence ou o scanner desconecta          | Retirar o valor como atual e identificar a última leitura, caso seja mostrada.   |
| 5   | Bluetooth conecta, mas inicialização ou ECU ainda não respondeu | Diferenciar as fases sem anunciar OBD conectado.                                 |
| 6   | HTTP falha, demora ou retorna JSON inválido                     | Mostrar falha de comunicação, não sobrepor consultas e continuar tentando.       |
| 7   | HTTP e coleta se recuperam                                      | Atualizar estados e valores sem recarregamento nem timers duplicados.            |
| 8   | A tela tem 320 ou 360 pixels CSS de largura                     | Manter textos e cartões legíveis, sem rolagem horizontal.                        |
| 9   | O celular não tem internet                                      | Carregar toda a interface pela origem local, sem requisições externas.           |
| 10  | A tensão é obtida por uma origem diferente                      | Usar a identificação recebida da API, sem assumir que é tensão da bateria.       |
| 11  | A aba volta após ficar suspensa pelo Android                    | Retomar consulta sem rajada; só apresentar dados como atuais após resposta nova. |

## Questões em aberto

Nenhuma decisão de UX permanece aberta. O visual final é avaliado por legibilidade e estados, sem
referência Figma ou design system externo obrigatório.

## Definition of Done

Antes de promover a `accepted`, disponibilizar a página e uma forma de exercitar snapshots controlados
nos testes do repositório. Registrar comandos reais para cada linha abaixo. O Playwright configurado
sem testes não é DoD executável desta Spec; não criar comando fictício para fazê-la parecer aceita.

| Validação necessária                                                     | Casos cobertos | Critério binário                                                                  |
| ------------------------------------------------------------------------ | -------------- | --------------------------------------------------------------------------------- |
| Testes de renderização com snapshots da SPEC-0002                        | 1–5, 10        | Textos, unidades, origem da tensão e tratamento de zero/nulos conforme contrato.  |
| Testes de polling com resposta lenta, falha, JSON inválido e recuperação | 6–7, 11        | No máximo uma requisição; recuperação automática e ausência de timers duplicados. |
| Testes de layout em navegador a 320 e 360 pixels e tela maior            | 8              | Não há overflow horizontal; cartões e mensagens permanecem acessíveis.            |
| Teste de carregamento com acesso externo bloqueado                       | 9              | A interface funciona e todas as requisições têm a origem local do dispositivo.    |
| Teste integrado com API e ensaio no navegador Android real               | 1–11           | Estados web e OBD distinguíveis; leituras e reconexão observadas no dispositivo.  |

O teste integrado pode reutilizar a evidência de API da SPEC-0002 quando os casos de interface forem
explicitamente registrados. Simulação no navegador não substitui o ensaio Wi-Fi/Bluetooth no hardware.

## Revisão humana

- Conferir legibilidade de unidades, temperaturas e mensagens no celular usado pelo mantenedor.
- Confirmar funcionamento na rede sem internet; pode ser necessário aceitar permanecer conectado a ela no Android.
- Observar inicialização, scanner ausente, carro desligado e reconexão, com o carro parado.
- Conferir que dado anterior não se confunde com leitura atual e que o painel não sugere funções de escrita.

## Verificação

A interface ainda não foi implementada nem testada. Registrar evidência de testes de navegador e de
ensaio Android no fechamento; manter `implemented-by` vazio até haver os arquivos reais.

## Fora do escopo

Aplicativo Android nativo, instalação PWA, captive portal, gráficos, histórico, alertas, autenticação
de aplicação, serviço remoto e WebSocket.
