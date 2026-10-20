# Firmware do computador de bordo

Firmware Arduino/C++ para ESP32 DOIT DevKit V1 (ESP32-WROOM-32), conforme as ADRs 0002–0005. O aplicativo Bun existente não é servidor de produção do dispositivo.

## Ferramentas

- Python 3.12 com suporte a ambientes virtuais;
- PlatformIO Core 6.1.18;
- Bun 1.2+ e Chromium Playwright para os testes do painel;
- compilador C++ nativo para os testes sem hardware.

Na raiz do repositório:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.18
export PATH="$PWD/.venv/bin:$PATH"
bun install --frozen-lockfile
bunx playwright install --with-deps chromium
```

Em Debian/Ubuntu, instale o pacote `python3-venv` se a criação do ambiente reclamar de `ensurepip`. Não instale dependências Python no ambiente global do sistema.

## Verificações separadas

```bash
bun run firmware:test
bun run firmware:build
bun run firmware:test:web
```

Esses comandos não substituem os checks Bun da raiz. O job `firmware` do CI executa os três separadamente. Uma compilação bem-sucedida não comprova conexão Bluetooth, leitura do veículo nem estabilidade do rádio.

## Configuração do scanner

A identidade padrão do ELM327 fica em `include/config.h`; pelo painel (seção "Scanner Bluetooth", ADR-0014) dá para buscar dispositivos próximos e gravar outro nome, PIN e MAC na NVS, sem recompilar. Padrões: `ONIX_BT_DEVICE_NAME` (padrão `OBDII`), `ONIX_BT_DEVICE_PIN` (padrão `1234`; muitos clones usam `0000`) e `ONIX_BT_DEVICE_MAC` (vazio conecta pelo nome). Pelo nome, o firmware faz uma busca curta (`ONIX_BT_DISCOVERY_MS`, 5,1 s), guarda o MAC encontrado e passa a conectar direto por ele; só busca de novo após `ONIX_BT_MAC_RETRIES` falhas seguidas. A busca ocupa o rádio e derruba o SoftAP enquanto dura (cerca de 1 s de indisponibilidade por tentativa); preencher `ONIX_BT_DEVICE_MAC` elimina a busca. O transporte é Bluetooth Classic (SPP): adaptadores "ELM327" que só expõem BLE não funcionam. `ONIX_OBD_PROTOCOL` usa `'0'` (busca automática, até 30 s); `'6'` (ISO 15765-4 CAN 11 bit 500 kbps) pula a busca no Onix 2019.

O firmware envia apenas Serviço 01 (leitura de PIDs) e `AT RV`; nunca apaga códigos de falha nem escreve na ECU. A task OBD roda no núcleo 1 porque a inicialização do ELMduino faz espera ativa e o watchdog do ESP32 reinicia a placa se o idle do núcleo 0 ficar sem CPU por 5 s.

A tabela de partição é `partitions_ota.csv` (ADR-0009): dois slots de app de 1,69 MB para OTA (Bluetooth Classic + Wi-Fi não cabem na partição de app padrão) e 512 KB de LittleFS. Ao trocar de tabela, grave por USB e grave também o LittleFS (`bun run web:deploy` já executa `uploadfs`).

## Gravação e diagnóstico

Com a placa conectada por USB à máquina que executa PlatformIO:

```bash
pio run -d apps/firmware -e esp32doit-devkit-v1 --target upload --upload-port /dev/ttyUSB0
pio device monitor --baud 115200 --port /dev/ttyUSB0
```

Para publicar o painel React (`apps/web`) junto com o firmware em um comando só,
prefira na raiz do repo `bun run web:deploy --port /dev/ttyUSB0` (build web + stage
p/ LittleFS + build + `uploadfs` + `upload`).

### Atualização por Wi-Fi (OTA)

Depois de uma gravação por USB com firmware que inclui OTA, conecte o computador à rede `Onix-MVP` e rode na raiz `bun run web:deploy --ota` (ou `pio run -d apps/firmware -e ota -t upload`/`-t uploadfs`). O espota envia para `192.168.4.1:3232` com a senha `ONIX_OTA_PASSWORD` de `include/config.h`, que deve bater com `--auth` do env `ota`. O ESP32 abre uma conexão TCP de volta para o computador: libere-a no firewall local. Cada gravação reinicia a placa; o script espera o HTTP voltar antes da próxima.

A porta acima é um exemplo: confirme a porta real antes de gravar. Alimente por USB durante o MVP; instalação elétrica automotiva permanente não está incluída.

Conecte o Android à rede `Onix-MVP` e abra `http://192.168.4.1`. A configuração deve seguir a SPEC-0002; o endereço efetivo é informado pelo Serial. Aceite permanecer na rede sem internet quando o Android solicitar.

## Hotspot do celular

Com o hotspot do celular cadastrado, a placa entra nele como cliente para o aparelho manter a
internet móvel enquanto mostra o painel (ADR-0012). Sem ele, o SoftAP `Onix-MVP` continua
disponível como antes.

- **Cadastrar:** na seção "Rede Wi-Fi" do painel, conectado ao `Onix-MVP`, informe o SSID, a senha
  do hotspot e a senha do OTA (`ONIX_OTA_PASSWORD` em `include/config.h`). A placa grava a
  credencial na NVS e reinicia. Equivalente: `POST /api/wifi` com `{"ssid","password","otaPassword"}`.
- **Remover:** a mesma seção, ou `DELETE /api/wifi` com `{"otaPassword":"..."}`.
- **Requisitos do hotspot:** 2,4 GHz e WPA2. O ESP32 não conecta em 5 GHz nem em WPA3. SSID de 1 a
  32 bytes; senha de 8 a 63 caracteres, ou vazia para rede aberta.
- **Encontrar o IP:** a placa tenta o endereço fixo `.50` a `.59` na faixa do hotspot, e usa o IP do
  DHCP se todos estiverem ocupados. O endereço aparece no Serial, em `GET /api/logs` e em
  `GET /api/wifi`. Na lista de clientes do celular, a placa aparece com o nome `onix`. Não há
  `onix.local` nem `--ota-host`; o deploy por Wi-Fi continua pelo `Onix-MVP`.
- **Fallback:** com credencial, o boot tenta o hotspot por 15 s e depois sobe o `Onix-MVP`. Com o
  `Onix-MVP` ativo e sem clientes, a placa procura o hotspot a cada 20 s. Se o hotspot cai, a placa
  espera 30 s e sobe o `Onix-MVP`. Com clientes conectados ao `Onix-MVP`, a busca fica suspensa, pois
  o scan troca de canal e derruba a conexão.

## Aceite físico pendente

Execute com o veículo parado; o motorista não deve ajustar nem observar o celular em movimento.

| Ensaio                                         | Evidência a registrar                                                            |
| ---------------------------------------------- | -------------------------------------------------------------------------------- |
| Boot sem scanner                               | Serial de boot e resposta HTML/JSON offline                                      |
| Scanner real                                   | Identidade configurada, transporte SPP, pareamento e protocolo detectado         |
| Carro desligado com scanner conectado          | Estados separados de Bluetooth, adaptador e ECU; HTTP disponível                 |
| Ignição e motor ligados                        | RPM, velocidade e temperatura; suporte dos sensores opcionais e origem da tensão |
| Retirada e recolocação do scanner              | Recuperação sem reboot, valores antigos inválidos e novas leituras               |
| HTTP durante inicialização e reconexão         | Duração de cada resposta, sem timeout com prazo de 2 segundos                    |
| Android sem internet e retorno de aba suspensa | Página local, estados corretos e recuperação sem rajada                          |

Registre versões, configuração, latência, taxa efetiva e resultados por caso. Consulte as matrizes completas em `docs/specs/0001-*`, `0002-*` e `0003-*`. Simulação e teste em Chromium não substituem o ensaio no ESP32/ELM327/Onix/Android. As Specs não podem ser encerradas como implementadas enquanto esse aceite estiver pendente.
