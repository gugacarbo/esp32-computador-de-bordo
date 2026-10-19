---
status: accepted # proposed | accepted | deprecated | superseded
date: 2026-10-19
builds-on: [ADR-0009, ADR-0011] # ADRs relacionados: ADR-NNNN ou repo:ADR-NNNN
superseded-by: null # escalar; preenchido no ADR antigo no commit que aceita o novo
deciders: ["project maintainer"]
---

# Conecta ao hotspot do celular com fallback para o SoftAP

## Contexto e problema

No SoftAP `Onix-MVP` o Android perde a internet móvel enquanto mostra o painel, porque a placa é a
única rede disponível. Para manter a internet do celular e o painel ao mesmo tempo, a placa precisa
entrar no hotspot do próprio celular como cliente (STA) e usar o SoftAP só quando o hotspot não
estiver disponível.

## Direcionadores da decisão

- Manter o celular com internet móvel enquanto o painel é consultado.
- Continuar acessível pelo `Onix-MVP` quando o hotspot estiver desligado ou fora de alcance.
- Memória e flash escassas: a partição de aplicação já está em 94,9% e restam cerca de 90 KB livres,
  o que descarta bibliotecas de portal cativo como WiFiManager.
- Não alterar o painel nem o contrato de dados da SPEC-0002; a configuração entra por uma rota nova.
- Proteger a troca de rede com a mesma senha do OTA (ADR-0009), sem criar um segundo segredo.

## Opções consideradas

### Opção 1 — manter só o SoftAP

**Prós:** nenhuma mudança de rede; comportamento já validado; sem configuração.
**Contras:** o celular perde a internet móvel enquanto consulta o painel.

### Opção 2 — STA com fallback para o SoftAP

**Prós:** o celular mantém a internet pelo próprio hotspot; o SoftAP continua como caminho de
acesso quando o hotspot cai ou não existe; a troca é feita pelo painel com a senha do OTA.
**Contras:** o endereço do hotspot muda entre ativações no Android, por isso a placa escolhe um IP
fixo; há uma janela de busca e reconexão que interrompe o acesso enquanto dura.

### Opção 3 — AP e STA simultâneos

**Prós:** o painel fica disponível pelo SoftAP mesmo com a placa conectada ao hotspot.
**Contras:** o SoftAP precisa seguir o canal do roteador, o que muda a rede do painel a cada troca;
usa mais RAM e energia, sem folga na placa.

## Decisão

Adotar a Opção 2. As credenciais do hotspot ficam na NVS (namespace `wifi`, chaves `ssid` e `pass`)
e são gravadas por `POST /api/wifi` com `{ssid, password, otaPassword}`; `DELETE /api/wifi` com
`{otaPassword}` as remove. Ambas exigem a senha do OTA (`ONIX_OTA_PASSWORD`) e reiniciam a placa
depois da resposta. `GET /api/wifi` informa modo, se há credencial, SSID e IP, nunca a senha.

Transições: com credencial, o boot tenta a STA por `ONIX_STA_CONNECT_TIMEOUT_MS` (15 s) e sobe o
SoftAP se não conectar. Em AP sem clientes, a placa faz um scan assíncrono pelo SSID a cada
`ONIX_STA_SCAN_INTERVAL_MS` (20 s); não escaneia com clientes conectados, porque o scan troca de
canal e derruba o cliente. Se a STA cai, espera `ONIX_STA_RECONNECT_TIMEOUT_MS` (30 s) antes de subir
o SoftAP.

Endereço: no Android o intervalo do hotspot costuma ser sorteado a cada ativação. Após o DHCP, a
placa escolhe um IP fixo entre `.50` e `.59` na faixa do gateway, sondando cada candidato por ARP
(`ONIX_STA_ARP_PROBE_MS`). Se todos estiverem ocupados, permanece no IP do DHCP. O hostname é
`onix`, para aparecer na lista de clientes do hotspot, e o IP escolhido vai para `/api/logs`.

Regras: SSID de 1 a 32 bytes; senha vazia (rede aberta) ou de 8 a 63 bytes. O ESP32 só conecta a
hotspots de 2,4 GHz com WPA2.

## Consequências

- Positivas: o celular mantém a internet móvel durante o uso do painel; o painel continua no
  `Onix-MVP` quando o hotspot não está disponível; a troca de rede não exige cabo nem recompilação.
- Negativas: o Android pode entregar o mesmo IP fixo a outro dispositivo em uma ativação futura,
  risco aceito; enquanto a placa procura o hotspot ou reconecta, o acesso HTTP pode ficar indisponível por alguns
  segundos.
- O deploy por Wi-Fi (`--ota`, ADR-0009) continua passando pela rede `Onix-MVP`, pois o host só
  alcança a placa com ela em modo AP.
- Fora de escopo: `--ota-host`, mDNS `onix.local`, pedido de IP por DHCP (opção 50), múltiplas redes
  salvas e AP+STA simultâneos.
- Quem conhece a senha do OTA pode trocar a rede da placa; é o mesmo nível de proteção do ADR-0009.
- A lógica fica testável no ambiente nativo (`WifiPolicy`, `selectStaticHost` e
  `parseWifiRequestBody`), sem depender do rádio.

## Confirmação

```bash
PATH=$PWD/.venv/bin:$PATH bun run firmware:test
PATH=$PWD/.venv/bin:$PATH bun run firmware:build
bun run test
```

Na placa: cadastrar o hotspot pela seção "Rede Wi-Fi" do painel e confirmar que a placa entra no
hotspot e que o painel responde em `x.x.x.50` (ou no IP do DHCP, se os candidatos estiverem
ocupados). Desligar o hotspot e confirmar que o `Onix-MVP` sobe em até cerca de 30 s. Religar o
hotspot sem clientes no AP e confirmar que a placa volta a entrar nele em até cerca de 20 s.

## Notas

O ensaio físico desta decisão segue pendente até o registro das três etapas acima no veículo.
