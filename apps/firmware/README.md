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

## Gravação e diagnóstico

Com a placa conectada por USB à máquina que executa PlatformIO:

```bash
pio run -d apps/firmware -e esp32doit-devkit-v1 --target upload --upload-port /dev/ttyUSB0
pio device monitor --baud 115200 --port /dev/ttyUSB0
```

A porta acima é um exemplo: confirme a porta real antes de gravar. Alimente por USB durante o MVP; instalação elétrica automotiva permanente não está incluída.

Conecte o Android à rede `Onix-MVP` e abra `http://192.168.4.1`. A configuração deve seguir a SPEC-0002; o endereço efetivo é informado pelo Serial. Aceite permanecer na rede sem internet quando o Android solicitar.

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
