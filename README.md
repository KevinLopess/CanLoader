# CanLoader

CanLoader é um bootloader para atualização de firmware por CAN no
STM32F103C8Tx. A implementação usa CAN clássico a 500 kbit/s, identificadores
padrão de 11 bits, transferência em blocos, ACK/NACK, sessão, timeout,
retransmissão no host e CRC-32/ISO-HDLC. A lógica comum é separada dos ports de
CAN, Flash e plataforma específicos do STM32F1.

O target atual é o STM32F103C8Tx com 64 KiB de Flash e 20 KiB de SRAM. A
aplicação começa em `0x08004000`, pode ocupar até 47 KiB e tem uma página de
1 KiB reservada para metadados. O projeto inclui uma ferramenta Python para
SocketCAN e uma aplicação relocada de teste.

> **Estado da validação:** testes automatizados e builds não substituem a
> validação elétrica e funcional. Hardware validation was not performed. Execute
> a sessão A–J de [validação](Docs/VALIDATION.md) antes de considerar o sistema
> validado em bancada.

## Características implementadas

- boot window de 1500 ms quando existe aplicação válida;
- permanência no bootloader quando a imagem está ausente ou inválida;
- atualização single-slot com invalidação persistente antes de apagar a
  aplicação;
- frames DATA de até 5 bytes, blocos negociados de até 100 bytes e ACK por bloco;
- detecção de sequência incorreta e tratamento idempotente de duplicatas;
- CRC da imagem, validação do MSP e do Reset Handler e handoff para a aplicação;
- limites de Flash definidos pelo firmware: o protocolo não recebe endereços
  arbitrários do host;
- CLI `canloader` com `info`, `update`, `verify` e `run`;
- descoberta não invasiva com `ls`, UID físico, node ID persistente e
  reendereçamento com `set-node`;
- entrada remota no bootloader com `reboot`, usando reset e uma janela de 10 s;
- aplicação de teste sem HAL, linkada em `0x08004000`, com padrão visual
  diferente do bootloader.

O protocolo é **J1939-inspired, not SAE J1939 compliant**. Consulte as
[limitações](Docs/LIMITATIONS.md) antes de usá-lo fora de uma bancada controlada.

## Hardware e configuração atual

| Item | Configuração |
|---|---|
| MCU | STM32F103C8Tx, Cortex-M3, 64 KiB Flash, 20 KiB SRAM |
| Clock | HSE 8 MHz, PLL x9, SYSCLK/HCLK 72 MHz, PCLK1 36 MHz |
| CAN | bxCAN em modo normal, 500 kbit/s, prescaler 4, BS1 13 TQ, BS2 4 TQ, SJW 1 TQ |
| Pinos CAN | PA11/CAN_RX e PA12/CAN_TX |
| LED | PC13, ativo em nível baixo |
| Nó | 1 por padrão; configurável e persistente (`0x600/0x700/0x580 + node`) |

O STM32 precisa de um transceptor CAN externo. Ligue CAN_H, CAN_L e GND comum,
com 120 Ω em cada extremidade física do barramento. Não conecte diretamente os
pinos PA11/PA12 à linha diferencial.

## Estrutura

```text
Application/             máquina de estados, metadados, validação e jump
Protocol/                protocolo binário e CRC portáteis
Portable/Inc/            contratos abstratos de CAN, Flash e plataforma
Portable/stm32f1/Src/    implementações STM32F1/HAL/bxCAN/FPEC
Config/                  mapa do target, IDs e timeouts
Core/ e Drivers/         projeto STM32CubeMX/HAL/CMSIS
Tools/canloader/         pacote e CLI Python/python-can
TestApplication/         firmware relocável usado na validação
Tests/                   testes C e Python sem hardware
Docs/                    arquitetura, memória, protocolo e validação
```

O fluxo de dependências e o procedimento para um futuro STM32G0 estão em
[ARCHITECTURE.md](Docs/ARCHITECTURE.md).

## Pré-requisitos

- GNU Make e o GNU Arm Embedded Toolchain (`arm-none-eabi-gcc`);
- Python 3.10 ou superior;
- `python-can` para acesso ao barramento;
- Linux/SocketCAN e `can-utils` para a validação descrita aqui;
- STM32CubeProgrammer CLI e ST-LINK para a gravação inicial;
- target STM32F103C8Tx, transceptor CAN e interface USB–CAN compatível com
  SocketCAN.

O `Makefile` procura primeiro o toolchain instalado como plugin do
STM32CubeIDE e, se não o encontrar, usa `arm-none-eabi-*` do `PATH`.

## Build e testes

Na raiz do repositório:

```sh
make bootloader
make test-application
make test
```

Os principais alvos são:

| Alvo | Resultado |
|---|---|
| `make bootloader` | `build/bootloader/CanLoader.elf`, `.bin`, `.hex`, `.map` e `.list` |
| `make test-application` | `TestApplication/build/canloader_test_app.elf`, `.bin`, `.hex` e `.map` |
| `make all` | bootloader e aplicação de teste |
| `make test-c` | testes nativos do protocolo, máquina de estados, metadata e imagem |
| `make test-python` | testes da serialização, cliente, retries, barramento e CLI |
| `make test` | testes C e Python |
| `make clean` | remove apenas artefatos gerados pelos Makefiles |

O bootloader também pode ser importado no STM32CubeIDE. As configurações Debug
e Release usam `STM32F103C8TX_FLASH.ld` e incluem `Application`, `Protocol` e
`Portable`. O firmware de teste tem Makefile e linker próprios.

## Instalação da ferramenta host

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e Tools/canloader
```

A CLI não configura `can0` e nunca executa `sudo`. A configuração da interface
é responsabilidade do operador.

## Quick start

Este exemplo pressupõe que o hardware está montado, o bootloader e a aplicação
de teste foram compilados e a virtual environment acima está ativa.

1. Para uma primeira sessão controlada, apague a Flash e grave o bootloader. O
   apagamento total também remove qualquer aplicação e metadata anteriores:

   ```sh
   STM32_Programmer_CLI -c port=SWD -e all
   STM32_Programmer_CLI -c port=SWD -w build/bootloader/CanLoader.bin 0x08000000 -v -rst
   ```

2. Configure o SocketCAN a 500 kbit/s:

   ```sh
   sudo ip link set can0 down
   sudo ip link set can0 type can bitrate 500000 restart-ms 100
   sudo ip link set can0 up
   ip -details -statistics link show can0
   ```

3. Descubra os nós, consulte o target e envie a aplicação:

   ```sh
   python -m canloader ls --interface socketcan --channel can0
   python -m canloader info --interface socketcan --channel can0 --node 1
   python -m canloader update TestApplication/build/canloader_test_app.bin --interface socketcan --channel can0 --node 1 --run
   ```

Depois que a aplicação de teste estiver executando, ela continua respondendo a
`ls` e pode retornar ao bootloader sem RESET físico:

```sh
python -m canloader reboot --interface socketcan --channel can0 --node 1
python -m canloader set-node 7 --interface socketcan --channel can0 --node 1
```

`set-node` deve ser enviado ao bootloader no endereço atual. Ele grava a página
de configuração, responde no endereço antigo, reinicia e confirma o endereço
novo. Updates da aplicação não apagam essa configuração; erase total por SWD
apaga e restaura o default 1.

Os defaults do host são timeout normal de 0,5 s, timeout de erase de 4 s e três
retransmissões depois da tentativa inicial. Use `--verbose` para registrar frames.

## Indicações do LED

| Situação | PC13 |
|---|---|
| boot window com imagem válida | aceso por até 1500 ms |
| aguardando host | alterna a cada 500 ms |
| preparação, erase, recepção ou verificação | alterna a cada 125 ms |
| imagem pronta / pedido de execução | apagado até o jump |
| erro de inicialização do bootloader | alterna a cada 100 ms |
| aplicação de teste | duas piscadas de 80 ms, separadas por 100 ms, e pausa de 740 ms |
| erro de IRQ no handoff detectado pela aplicação | continuamente aceso |

## Documentação

- [Implementação técnica completa e requisitos](Docs/IMPLEMENTACAO_BOOTLOADER.md)
- [Arquitetura e portabilidade](Docs/ARCHITECTURE.md)
- [Mapa de memória e metadata](Docs/MEMORY_MAP.md)
- [Protocolo CAN v1](Docs/PROTOCOL.md)
- [Sessão de validação A–J](Docs/VALIDATION.md)
- [Limitações conhecidas](Docs/LIMITATIONS.md)
- [Divergências em relação ao TCC](Docs/TCC_DIVERGENCES.md)
