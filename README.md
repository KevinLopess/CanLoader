# CanLoader

CanLoader é um bootloader *single-slot* para atualização de firmware de
microcontroladores STM32 por CAN clássico a 500 kbit/s. A lógica comum está
separada dos ports de hardware e esta revisão possui dois alvos compiláveis:
STM32F103C8Tx e NUCLEO-G0B1RE. O perfil CanLoader usa identificadores
estendidos de 29 bits, NAME e Address Claiming J1939, TP RTS/CTS/BAM, um
subconjunto de DM14/DM15/DM16 e um PGN Proprietary A para o ciclo de vida da
imagem.

Um formato preliminar com IDs de 11 bits foi descartado durante o
desenvolvimento. Host, bootloader e aplicação de referência utilizam somente o
formato atual, identificado internamente pelo valor `0x02`.

> **Escopo normativo:** o projeto implementa mecanismos de J1939-21/J1939-81 e
> um perfil DM baseado em material público histórico de J1939-73. Ele não foi
> certificado pela SAE e não deve ser apresentado como implementação integral
> de J1939-73. Manifest, sessão, CRC e handoff são extensões CanLoader no PGN
> Proprietary A `0xEF00`.

## Estado atual

- ports para STM32F103C8Tx/bxCAN/Cortex-M3 e
  STM32G0B1RE/FDCAN/Cortex-M0+;
- CAN clássico, 500 kbit/s, quadros de dados estendidos de 29 bits;
- preferred Source Address do target `0x80`; ferramenta de serviço `0xF9`;
- NAME de desenvolvimento derivado deterministicamente do UID96 do STM32;
- descoberta com Request/Address Claimed e identificação dirigida;
- seleção segura do target por NAME, mesmo quando o Source Address muda;
- atualização dirigida com DM14/DM15/DM16 e TP RTS/CTS;
- reendereçamento persistente por Commanded Address via BAM;
- CRC-32/ISO-HDLC, validação de vetores e metadata transacional;
- entrada remota no bootloader a partir da aplicação;
- host Python para SLCAN no macOS e SocketCAN no Linux;
- testes C nativos executados com as configurações dos dois targets;
- aplicações de referência relocadas para os dois mapas de memória.

Os ensaios de bancada de 29 e 30 de setembro de 2026 cobrem os dois alvos:
duas Blue Pill (STM32F103C8) e uma NUCLEO-G0B1RE no mesmo barramento, com
atualização dirigida de cada placa e recuperação após interrupções. Os
registros estão em [`Validation/evidence/`](Validation/evidence/). Consulte
[STM32G0B1RE.md](Docs/STM32G0B1RE.md) antes de gravar a NUCLEO.

## Mapas de memória

### STM32F103C8Tx

| Região | Intervalo | Tamanho |
|---|---:|---:|
| código do bootloader | `0x08000000..0x08004BFF` | 19 KiB |
| configuração NAME/SA | `0x08004C00..0x08004FFF` | 1 KiB |
| aplicação | `0x08005000..0x0800FBFF` | 43 KiB / 44032 bytes |
| metadata | `0x0800FC00..0x0800FFFF` | 1 KiB |

A aplicação deve ser linkada em `0x08005000`; binários de protótipos internos
linkados em `0x08004000` não são válidos no formato atual.

### STM32G0B1RE

| Região | Intervalo | Tamanho |
|---|---:|---:|
| código do bootloader | `0x08000000..0x080077FF` | 30 KiB |
| configuração NAME/SA | `0x08007800..0x08007FFF` | 2 KiB |
| aplicação | `0x08008000..0x0807F7FF` | 478 KiB / 489472 bytes |
| metadata | `0x0807F800..0x0807FFFF` | 2 KiB |

A aplicação G0 deve ser linkada em `0x08008000`. Os valores são definidos nos
headers de target e conferidos pelos dois linker scripts, não inferidos pela
ferramenta host.

## Hardware

| Item | STM32F103C8Tx | NUCLEO-G0B1RE |
|---|---|---|
| núcleo | Cortex-M3 | Cortex-M0+ |
| clock | 72 MHz, PCLK1 36 MHz | 64 MHz, PCLK1 64 MHz |
| controlador | bxCAN | FDCAN1 em modo CAN clássico |
| bit timing | prescaler 4, BS1 13, BS2 4, SJW 1 | prescaler 8, TSEG1 13, TSEG2 2, SJW 2 |
| pinos | PA11 RX, PA12 TX | PC4 RX, PC5 TX, AF3 |
| LED | PC13, ativo baixo | PA5/LD4, ativo alto |
| Flash | erase 1 KiB, programa 2 B | erase 2 KiB, programa 8 B |

O STM32 precisa de transceptor CAN externo. Conecte CAN_H, CAN_L e GND comum e
use 120 Ω em cada extremidade física do barramento. PA11/PA12 não podem ser
ligados diretamente ao par diferencial.

## Bare-metal, CMSIS e HAL

O CanLoader é bare-metal: não usa RTOS, scheduler, heap ou threads. CMSIS
fornece startup, definições do núcleo Cortex-M e acesso aos registradores do
processador. A HAL é usada somente na inicialização dos targets e nos ports de
CAN, Flash e plataforma; ela não faz parte da máquina de estados nem do
protocolo comum.

A portabilidade vem dos contratos em `Portable/Inc`, não da HAL. O alvo pode
implementar esses contratos com HAL, LL ou acesso direto a registradores. A
regra atual mantém a HAL nos adaptadores STM32 para reduzir o risco de erros em
clock, Flash e FDCAN, permitindo substituições pontuais quando uma medição de
tamanho ou tempo justificar. `make check-portability` falha caso uma dependência
HAL/LL ou de dispositivo STM32 seja introduzida em `Application`, `Protocol`,
`Portable/Inc` ou `Config`.

## Build e testes

Pré-requisitos principais:

- GNU Make e `arm-none-eabi-gcc`;
- Python 3.10 ou superior para a ferramenta host;
- STM32CubeProgrammer/ST-LINK para a instalação inicial;
- `python-can` e `pyserial` para o host.

Na raiz do repositório, `make all` compila o bootloader e a aplicação de teste
de todos os targets encontrados em `Targets/*/target.mk`:

```sh
make all
```

O F103 permanece o target padrão para os alvos individuais:

```sh
make bootloader
make test-application
make test
```

Para a NUCLEO-G0B1RE:

```sh
make TARGET=stm32g0b1re all
```

`all-targets` permanece como alias explícito de `all`:

```sh
make all-targets
```

Para gravar e depurar pelo STM32CubeIDE, use as configurações
`CanLoader STM32F103C8Tx.launch` e `CanLoader NUCLEO-G0B1RE.launch`. O passo a
passo está em [Gravação e depuração pelo STM32CubeIDE](Docs/CUBEIDE_LAUNCH.md).

Artefatos principais:

| Alvo | Saída |
|---|---|
| `make all` | bootloader e aplicação de teste de todos os targets em `build/<target>/` |
| `make bootloader` | `build/stm32f103c8/bootloader/CanLoader.{elf,bin,hex,map,list}` |
| `make test-application` | `build/stm32f103c8/test-application/canloader_test_app.{elf,bin,hex,map}` |
| `make TARGET=stm32g0b1re all` | `build/stm32g0b1re/bootloader/CanLoader.*` e `build/stm32g0b1re/test-application/canloader_test_app.*` |
| `make test-c` | testes nativos em `build/tests/c/` |
| `make test-python` | serialização, scanner, cliente, TP, retries e CLI |
| `make check-portability` | verifica que HAL/LL/CMSIS de dispositivo não vazaram para o núcleo comum |
| `make test` | fronteira de portabilidade, testes C e testes Python |

O Makefile procura primeiro o toolchain empacotado no STM32CubeIDE e depois
`arm-none-eabi-*` no `PATH`.

## Instalação da CLI

Use Python 3.10+; o Python 3.9 fornecido por versões antigas do Xcode não atende
ao pacote atual.

```sh
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e Tools/canloader
canloader --version
```

Se a criação do ambiente for interrompida, remova ou renomeie apenas aquela
pasta incompleta e recrie o ambiente. Não tente ativar `.venv/bin/activate`
antes de o comando `venv` terminar.

## Uso no macOS com SLCAN

No macOS não existe normalmente uma interface `can0`. O canal SLCAN é a porta
serial do adaptador. Para identificá-la:

```sh
ls /dev/cu.*
```

Compare a lista antes e depois de conectar o adaptador e prefira `/dev/cu.*` a
`/dev/tty.*`. Exemplo:

```sh
canloader ls \
  --interface slcan \
  --channel /dev/cu.usbmodem2074318558421 \
  --bitrate 500000 \
  --verbose
```

Todos os quadros do formato atual são estendidos. Em uma captura Lawicel/SLCAN
eles aparecem como quadros `T`; quadros `t` de 11 bits pertencem ao protótipo
interno anterior ou a tráfego alheio.

Depois de obter o NAME com `ls`, bloqueie a identidade nas operações mutáveis:

```sh
TARGET_NAME=0x8000000000123456
PORT=/dev/cu.usbmodem2074318558421

canloader info --interface slcan --channel "$PORT" \
  --address 0x80 --name "$TARGET_NAME"

canloader update build/stm32f103c8/test-application/canloader_test_app.bin \
  --interface slcan --channel "$PORT" \
  --address 0x80 --name "$TARGET_NAME" --run
```

`--address` é a rota observada; `--name` é a identidade. Se o target perder uma
disputa e mudar de SA, execute `ls` novamente e use o SA anunciado para o mesmo
NAME.

## Uso no Linux com SocketCAN

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0

canloader ls --interface socketcan --channel can0
canloader info --interface socketcan --channel can0 \
  --address 0x80 --name 0x8000000000123456
```

A CLI usa CAN raw e implementa claim/TP em user space. Não compartilhe o mesmo
Source Address entre essa pilha e um socket `CAN_J1939` do kernel.

## Comandos

| Comando | Efeito |
|---|---|
| `canloader ls` | solicita Address Claimed e identifica individualmente nós CanLoader |
| `canloader info` | lê capabilities, memória, UID, NAME e SA |
| `canloader update IMAGE.bin` | abre manifest, apaga, escreve, finaliza e verifica |
| `canloader verify` | recalcula e valida a imagem armazenada |
| `canloader run` | executa uma aplicação válida |
| `canloader reboot` | pede à aplicação que reinicie no bootloader |
| `canloader set-address NEW_SA` | envia Commanded Address ao NAME bloqueado |

Os parâmetros comuns incluem `--address`, `--name`, `--source-address`,
`--source-name`, `--timeout`, `--transport-timeout`, `--erase-timeout`,
`--scan-timeout`, `--retries` e `--verbose`. O timeout TP default do host é
1,5 s, maior que o deadline de progresso de 1,25 s do target.

## Fluxo resumido do update

```text
Request/Address Claimed + IDENTIFY
        -> START_UPDATE(size)
        -> SET_IMAGE_CRC(crc32)
        -> DM14 ERASE / DM15 / close
        -> [DM14 WRITE / DM15 PROCEED / DM16 direto (1..7)
            ou via TP (8..100) /
            DM15 COMPLETED / close] × blocos
        -> FINALIZE
        -> VERIFY
        -> RUN opcional
```

O target informa o tamanho do bloco na resposta a `START_UPDATE`: 100 bytes no
F103 e 96 bytes no G0. O seletor `INFO FLASH_ERASE_UNIT_SIZE` informa a unidade
usada para calcular o `length` de ERASE: 1024 ou 2048 bytes, respectivamente.
A sessão proprietária não ocupa o campo `Key/User_Level` de DM14; este perfil
transmite `0xFFFF`.

## Aplicação e retorno ao bootloader

A aplicação deve compartilhar NAME e preferred SA com o bootloader, executar
Address Claim, responder a `IDENTIFY`/`INFO` e aceitar
`ENTER_BOOTLOADER=0x33` no PGN Proprietary A dirigido. Ela grava a assinatura
transitória `0xB007/0x4C44` em backup registers, tenta entregar o ACK e reinicia.
O bootloader consome a assinatura uma única vez e permanece disponível por 10 s.

```sh
canloader reboot --interface slcan --channel "$PORT" \
  --address 0x80 --name "$TARGET_NAME"
```

## LED

| Estado | LED do target |
|---|---|
| boot window com imagem válida | aceso por até 1500 ms; até 10000 ms após pedido da aplicação |
| aguardando host | alterna a cada 500 ms |
| preparação, erase, recepção ou verificação | alterna a cada 125 ms |
| imagem pronta / handoff | apagado |
| erro de inicialização | alterna a cada 100 ms |
| aplicação de referência | duas piscadas curtas e pausa |
| aplicação detectou handoff com IRQ mascarada | continuamente aceso |

## Estrutura

```text
Application/             FSM, configuração, metadata, imagem e handoff
Protocol/                J1939, Address Claim, TP, CanLoader e CRC
Portable/Inc/            contratos de CAN, Flash e plataforma
Portable/stm32f103c8/    port HAL/bxCAN/FPEC/Cortex-M3
Portable/stm32g0b1re/    port HAL/FDCAN/Flash/Cortex-M0+
Config/                  mapa, NAME default, limites e timers
Config/targets/          parâmetros físicos de cada target
Targets/<target>/         configuração, startup, inicialização e linker
Tools/canloader/         pacote Python e CLI
TestApplication/         build comum e fontes relocadas por target
Tests/                   testes C e Python sem hardware
Core/Src/                syscalls e sysmem comuns aos dois targets
Validation/              scripts de sessão registrada e evidências de bancada
Docs/                    contrato, arquitetura, memória e histórico
```

## Documentação

- [Implementação técnica completa](Docs/IMPLEMENTACAO_BOOTLOADER.md)
- [Contrato on-wire](Docs/PROTOCOL.md)
- [Histórico da mudança do protótipo interno](Docs/MIGRATION_V1_TO_V2.md)
- [Arquitetura e portabilidade](Docs/ARCHITECTURE.md)
- [Port da NUCLEO-G0B1RE](Docs/STM32G0B1RE.md)
- [Mapa de memória e formatos persistentes](Docs/MEMORY_MAP.md)
- [Plano de validação](Docs/VALIDATION.md)
- [Limitações e ameaças](Docs/LIMITATIONS.md)
- [Relação com o TCC](Docs/TCC_DIVERGENCES.md)
- [Histórico de desenvolvimento e dos commits](Docs/HISTORY.md)
