---
status: draft
date: 2026-10-13
builds-on: [ADR-0004, ADR-0005, SPEC-0001]
implemented-by: []
---

# Expõe o estado das leituras na API local

## Objetivo

Permitir que um celular conectado ao ESP32 consulte uma cópia coerente do estado OBD, sem depender da
resposta do veículo no instante da requisição. Esta Spec é a fonte do contrato HTTP, de rede e dos
metadados dos sensores. A coleta que produz as leituras pertence à SPEC-0001.

O contrato registra o escopo aprovado. O status permanece `draft` enquanto não houver testes
executáveis específicos e firmware; `implemented-by` vazio não indica uma implementação no app Bun.

## Fluxo

1. O ESP32 inicia a rede local e as rotas, com um snapshot inicial sem leituras.
2. O celular conecta à rede e acessa a origem do dispositivo.
3. Uma requisição de dados copia o snapshot atual sem aguardar comunicação OBD.
4. A resposta calcula a idade e validade da cópia no instante da publicação.
5. Novas requisições retornam o estado atualizado ou o último valor com estado de falha/expiração.

## Contrato

### Rede e recursos

- SoftAP padrão: SSID `Onix-MVP`, senha `onix1234`, endereço `192.168.4.1`.
- Centralizar SSID, senha e endereço; informar o endereço efetivo pelo Serial.
- `GET /`: HTTP 200 com `Content-Type: text/html; charset=utf-8`, contendo a interface da SPEC-0003.
- `GET /api/data`: HTTP 200 com `Content-Type: application/json` e `Cache-Control: no-store`.
- Falta de dados, scanner ou ECU é estado do JSON, não erro HTTP do recurso disponível.
- Caminho inexistente: HTTP 404. Método diferente de GET nas rotas conhecidas: HTTP 405, sem efeitos.
- Não há autenticação de aplicação; acesso exige conexão à rede local protegida pela senha do SoftAP.
- Não há CORS para outras origens nem rotas de configuração, consulta arbitrária ou escrita no veículo.

### Envelope JSON

```json
{
  "uptimeMs": 123456,
  "btConnected": true,
  "adapterReady": true,
  "ecuResponding": true,
  "obdConnected": true,
  "rpm": {
    "value": 842,
    "valid": true,
    "status": "ok",
    "updatedAtMs": 123400,
    "ageMs": 56
  },
  "speedKph": {
    "value": 0,
    "valid": true,
    "status": "ok",
    "updatedAtMs": 123350,
    "ageMs": 106
  },
  "coolantC": {
    "value": 91,
    "valid": true,
    "status": "ok",
    "updatedAtMs": 123000,
    "ageMs": 456
  },
  "throttlePercent": {
    "value": null,
    "valid": false,
    "status": "unsupported",
    "updatedAtMs": null,
    "ageMs": null
  },
  "mapKpa": {
    "value": 31,
    "valid": false,
    "status": "timeout",
    "updatedAtMs": 121000,
    "ageMs": 2456
  },
  "voltage": {
    "value": 14.1,
    "valid": true,
    "status": "ok",
    "updatedAtMs": 123000,
    "ageMs": 456,
    "source": "adapter"
  }
}
```

Os seis campos sempre existem, inclusive antes da primeira leitura e para parâmetros não suportados.
Unidades e origens OBD dos cinco primeiros são definidas na tabela da SPEC-0001.

| Campo comum   | Semântica                                                                                                         |
| ------------- | ----------------------------------------------------------------------------------------------------------------- |
| `value`       | Último número finito lido com sucesso, ou `null` se nunca houve leitura na sessão de boot. Nunca NaN ou Infinity. |
| `valid`       | Verdadeiro apenas quando o sensor tem uma leitura recente utilizável e `status` é `ok`.                           |
| `status`      | Um dos estados da tabela abaixo; não é uma mensagem de log livre.                                                 |
| `updatedAtMs` | Instante da última leitura bem-sucedida desde o boot; `null` antes dela. Falha não atualiza esse instante.        |
| `ageMs`       | Idade não negativa da última leitura no instante da resposta; `null` sem leitura.                                 |

`uptimeMs` e `updatedAtMs` usam a mesma base monotônica em milissegundos desde o boot, sem depender de
internet ou relógio de calendário. Representá-los sem rollover de 32 bits exposto ao cliente.
`ageMs` é calculado a partir dessa base; reiniciar a placa zera o estado e não preserva timestamps antigos.

`voltage.source` é sempre `adapter` para `AT RV` ou `ecu` para PID 42, conforme configuração, inclusive
sem leitura. A API nunca mistura essas origens sob uma denominação ambígua de bateria.
`obdConnected` é verdadeiro somente quando Bluetooth, adaptador e ECU estiverem disponíveis.
A saúde da ECU expira pela janela centralizada da SPEC-0001, mesmo se o produtor estiver esperando.

### Estados do sensor

| `status`      | Significado                                                                   | `valid` |
| ------------- | ----------------------------------------------------------------------------- | ------- |
| `unavailable` | Ainda não houve leitura ou o transporte/origem necessário está indisponível.  | false   |
| `unsupported` | Ausência de suporte confirmada para o PID na sessão.                          | false   |
| `timeout`     | A última consulta não concluiu dentro do prazo.                               | false   |
| `error`       | A última resposta não pôde ser interpretada como leitura válida.              | false   |
| `stale`       | A última leitura teve sucesso, mas passou do prazo de frescor.                | false   |
| `ok`          | Última leitura bem-sucedida, origem disponível e prazo de frescor respeitado. | true    |

A resolução aplica nesta ordem: suporte ausente confirmado; origem indisponível; falha da última
consulta; expiração da leitura; leitura disponível e recente. Não substituir `timeout` ou `error`
por `stale`, pois o erro já explica a invalidade. Todo estado inválido preserva um último valor existente;
`null` significa exclusivamente ausência de leitura bem-sucedida no boot.

A origem dos PIDs exige transporte, adaptador e saúde da ECU; a tensão do adaptador exige somente
transporte e adaptador. Assim, `voltage` pode estar válida enquanto `ecuResponding` é falso.
Uma nova leitura bem-sucedida recupera o sensor e atualiza seu timestamp. O timeout de um sensor não
invalida automaticamente os outros sensores que continuam recentes e com origem disponível.

O prazo de frescor é configurado por sensor, considerando timeout e intervalo efetivo de polling.
A expiração independe de a task OBD conseguir publicar uma mudança. O consumidor da API pode confiar
em `valid`, em vez de reconstruir a política a partir de timestamps.

## Casos de borda

| #   | QUANDO                                                     | O sistema DEVE                                                                             |
| --- | ---------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| 1   | A API é consultada antes da primeira leitura               | Responder 200 com todos os sensores, valores e tempos nulos, `valid: false`.               |
| 2   | O sensor leu zero                                          | Retornar número zero com validade, não `null` nem ausência de dado.                        |
| 3   | Um sensor falha após uma leitura bem-sucedida              | Conservar valor e timestamp; marcar timeout/erro e atualizar a idade.                      |
| 4   | A leitura expira com o produtor bloqueado                  | Responder com validade falsa e estado calculado a partir da cópia e do relógio atual.      |
| 5   | Bluetooth cai ou a ECU deixa de responder                  | Refletir as fases corretas; invalidar apenas sensores cuja origem está indisponível.       |
| 6   | Um PID não é suportado                                     | Manter o campo presente e o estado `unsupported`.                                          |
| 7   | HTTP ocorre simultaneamente a uma atualização OBD          | Publicar uma cópia coerente, sem campos parcialmente atualizados ou mutex durante envio.   |
| 8   | A placa reinicia ou o contador de 32 bits cruza seu limite | Não retornar valores de outro boot, idades negativas nem expiração incorreta por rollover. |
| 9   | A tensão vem do adaptador com ECU indisponível             | Identificar `source: adapter`; não declarar a ECU saudável por essa leitura.               |
| 10  | O cliente usa rota desconhecida ou método não permitido    | Responder 404 ou 405, respectivamente, sem enviar comandos OBD.                            |
| 11  | O adaptador está ausente ou em inicialização demorada      | Continuar respondendo com estado offline sem aguardar conexão ou consulta ao carro.        |

## Questões em aberto

Nenhuma decisão de contrato permanece aberta. Os parâmetros de frescor e de recuperação serão
configurados e registrados com base na medição prevista na SPEC-0001.

## Definition of Done

Antes de promover a `accepted`, completar a toolchain e vincular cada linha abaixo a comandos reais do
repositório. Uma execução verde de `docs-check` valida documentos, não JSON, concorrência ou latência.
Não existem testes da API no bootstrap; esta matriz é o contrato de validação a implementar.

| Validação necessária                                                | Casos cobertos | Critério binário                                                                    |
| ------------------------------------------------------------------- | -------------- | ----------------------------------------------------------------------------------- |
| Testes de serialização do snapshot e estados com relógio controlado | 1–6, 8–9       | Tipos, presença de campos, nulidade, fontes e precedência correspondem ao contrato. |
| Testes de saúde da ECU e frescor durante pausa do produtor          | 4–5, 9, 11     | Expiração ocorre sem publicação OBD nova e não depende de AT RV.                    |
| Testes de cópia concorrente e handlers sem transporte real          | 7, 10–11       | Cópia coerente; rotas não chamam ELM nem mantêm o mutex ao enviar a resposta.       |
| Testes HTTP de cabeçalhos, status e métodos                         | 1, 6, 10–11    | 200/404/405 e cabeçalhos conforme contrato, inclusive offline.                      |
| Ensaio HTTP durante busca automática, carro desligado e reconexão   | 5, 7, 11       | Nenhuma tentativa de inicialização ou recuperação impede resposta HTTP.             |

Para o ensaio físico, registrar duração e latência de cada resposta durante as falhas provocadas,
com a conexão Wi-Fi do Android ativa. O critério é ausência de timeout HTTP com prazo de 2 segundos
por requisição no ensaio; se falhar, tratar a limitação antes de fechar a Spec, sem escondê-la mudando o prazo.
A execução precisa cobrir a espera completa de inicialização do ELM, não somente o polling normal.

## Revisão humana

- Confirmar conexão à rede local pelo Android sem depender de acesso à internet.
- Conferir uma captura JSON em cada fase Bluetooth/adaptador/ECU e comparar com o Serial.
- Inspecionar a tensão e sua origem e os PIDs opcionais sem suporte.
- Registrar comportamento do rádio quando o celular acessa HTTP durante reconexão Bluetooth.

## Verificação

Documentação criada; nenhum servidor HTTP do firmware foi iniciado nem testado nesta tarefa.
Resultados de API e hardware serão registrados na transição para `implemented`.
