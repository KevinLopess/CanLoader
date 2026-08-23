# CanLoader v2

CanLoader é um bootloader *single-slot* para atualização de firmware do
STM32F103C8Tx por CAN clássico a 500 kbit/s. A versão atual fala somente o
perfil CanLoader v2: identificadores estendidos de 29 bits, NAME e Address
Claiming J1939, TP RTS/CTS/BAM, um subconjunto de DM14/DM15/DM16 para acesso à
memória e um PGN Proprietary A para o ciclo de vida da imagem.

Não existe fallback para o protocolo v1 de IDs de 11 bits. Host, bootloader e
aplicação de referência precisam ser v2.

> **Escopo normativo:** o projeto implementa mecanismos de J1939-21/J1939-81 e
> um perfil DM baseado em material público histórico de J1939-73. Ele não foi
> certificado pela SAE e não deve ser apresentado como implementação integral
> de J1939-73. Manifest, sessão, CRC e handoff são extensões CanLoader no PGN
> Proprietary A `0xEF00`.

## Estado atual

- target STM32F103C8Tx, 64 KiB de Flash e 20 KiB de SRAM;
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
- testes C nativos e Python, além de aplicação relocada para ensaio em placa.

A validação elétrica e funcional em hardware continua sendo uma etapa separada.
Execute [VALIDATION.md](Docs/VALIDATION.md) e registre as evidências antes de
considerar uma versão liberada.

## Mapa de memória

| Região | Intervalo | Tamanho |
|---|---:|---:|
| código do bootloader | `0x08000000..0x08004BFF` | 19 KiB |
| configuração NAME/SA | `0x08004C00..0x08004FFF` | 1 KiB |
| aplicação | `0x08005000..0x0800FBFF` | 43 KiB / 44032 bytes |
| metadata | `0x0800FC00..0x0800FFFF` | 1 KiB |

A aplicação deve ser linkada em `0x08005000`; binários antigos linkados em
`0x08004000` não são válidos no v2 atual.

O build integrado medido nesta revisão ocupa `text=16456`, `data=12` e
`bss=1648` bytes. `text+data=16468` deixa 2988 bytes na região
de código de 19456 bytes. Esses números são uma fotografia do build atual e
devem ser medidos novamente antes da entrega final.

## Hardware

| Item | Configuração |
|---|---|
| MCU | STM32F103C8Tx, Cortex-M3 |
| Clock | HSE 8 MHz, PLL ×9, SYSCLK/HCLK 72 MHz, PCLK1 36 MHz |
| CAN | bxCAN normal, 500 kbit/s, prescaler 4, BS1 13 TQ, BS2 4 TQ, SJW 1 TQ |
| Pinos | PA11/CAN_RX e PA12/CAN_TX |
| LED | PC13, ativo em nível baixo |
| Flash | página de erase 1024 bytes, programação por halfword |

O STM32 precisa de transceptor CAN externo. Conecte CAN_H, CAN_L e GND comum e
use 120 Ω em cada extremidade física do barramento. PA11/PA12 não podem ser
ligados diretamente ao par diferencial.

## Build e testes

Pré-requisitos principais:

- GNU Make e `arm-none-eabi-gcc`;
- Python 3.10 ou superior para a ferramenta host;
- STM32CubeProgrammer/ST-LINK para a instalação inicial;
- `python-can` e `pyserial` para o host.

Na raiz do repositório:

```sh
make bootloader
make test-application
make test
```

Artefatos principais:

| Alvo | Saída |
|---|---|
| `make bootloader` | `build/bootloader/CanLoader.{elf,bin,hex,map,list}` |
| `make test-application` | `TestApplication/build/canloader_test_app.{elf,bin,hex,map}` |
| `make test-c` | testes nativos de protocolo, TP, claim, FSM, config e metadata |
| `make test-python` | serialização, scanner, cliente, TP, retries e CLI |
| `make test` | testes C e Python |

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

Todos os frames v2 são estendidos. Em uma captura Lawicel/SLCAN eles aparecem
como frames `T`; frames `t` de 11 bits são v1 ou tráfego alheio.

Depois de obter o NAME com `ls`, bloqueie a identidade nas operações mutáveis:

```sh
TARGET_NAME=0x8000000000123456
PORT=/dev/cu.usbmodem2074318558421

canloader info --interface slcan --channel "$PORT" \
  --address 0x80 --name "$TARGET_NAME"

canloader update TestApplication/build/canloader_test_app.bin \
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

O bloco de escrita é 100 bytes, salvo o último. O ERASE expressa quantidade de
páginas de 1024 bytes, não bytes. A sessão proprietária não ocupa o campo
`Key/User_Level` de DM14; este perfil transmite `0xFFFF`.

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

| Estado | PC13 |
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
Portable/stm32f1/Src/    HAL/bxCAN/FPEC/Cortex-M3
Config/                  mapa, NAME default, limites e timers
Tools/canloader/         pacote Python e CLI
TestApplication/         aplicação relocada de validação
Tests/                   testes C e Python sem hardware
Docs/                    contrato, arquitetura, memória e evidências
```

## Documentação

- [Implementação técnica completa](Docs/IMPLEMENTACAO_BOOTLOADER.md)
- [Contrato on-wire v2](Docs/PROTOCOL.md)
- [Migração incompatível v1 → v2](Docs/MIGRATION_V1_TO_V2.md)
- [Arquitetura e portabilidade](Docs/ARCHITECTURE.md)
- [Mapa de memória e formatos persistentes](Docs/MEMORY_MAP.md)
- [Plano de validação](Docs/VALIDATION.md)
- [Limitações e ameaças](Docs/LIMITATIONS.md)
- [Relação com o TCC](Docs/TCC_DIVERGENCES.md)
