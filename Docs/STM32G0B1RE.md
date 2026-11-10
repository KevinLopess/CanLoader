# Port para NUCLEO-G0B1RE

Este documento descreve o segundo target do CanLoader. Ele foi criado para
a placa NUCLEO-G0B1RE, com STM32G0B1RET6, e reutiliza a FSM, o protocolo J1939,
os formatos persistentes e a ferramenta host do target STM32F103. Foram
substituídas somente as partes ligadas ao MCU: inicialização, FDCAN, Flash,
registradores de backup, LED, handoff, startup e linker scripts.

O port está compilável e coberto pelos testes automatizados do repositório. Ele
foi ensaiado em uma NUCLEO-G0B1RE física nas sessões de 29 e 30 de setembro e de
4 de novembro de 2026, registradas em `Validation/evidence/`. A Seção 7 separa
o que esses ensaios cobriram do que ainda não foi verificado na placa.

## 1. Configuração implementada

| Item | Valor |
|---|---|
| MCU/placa | STM32G0B1RET6 / NUCLEO-G0B1RE |
| núcleo | Arm Cortex-M0+ |
| Flash/SRAM | 512 KiB / 144 KiB |
| clock | HSI16 + PLL, SYSCLK/HCLK/PCLK1 de 64 MHz |
| CAN | FDCAN1 em modo CAN clássico, IDs de 29 bits |
| bitrate | 500 kbit/s, 16 TQ, sample point de 87,5% |
| FDCAN RX | PC4, AF3, conector CN9 pino 2 (D1) |
| FDCAN TX | PC5, AF3, conector CN9 pino 1 (D0) |
| LED | PA5/LD4, ativo em nível alto |
| erase/programação | página de 2048 B / doubleword de 8 B |
| código do target | `0xB1` no campo alto de `IDENTIFY` |
| bloco de update | 96 B, informado por `START_UPDATE` |

Os rótulos D0/D1 da placa correspondem ao uso Arduino/USART. Para este port,
vale a função alternativa configurada no STM32: PC4 é `FDCAN1_RX` e PC5 é
`FDCAN1_TX`.

## 2. Ligação do barramento

A NUCLEO expõe os sinais digitais do controlador FDCAN, mas o port pressupõe um
transceptor CAN externo. A ligação mínima é:

```text
NUCLEO PC5 / FDCAN1_TX  ---> TXD do transceptor
NUCLEO PC4 / FDCAN1_RX  <--- RXD do transceptor
NUCLEO GND              ----- GND do transceptor e do barramento
transceptor CAN_H       ----- CAN_H
transceptor CAN_L       ----- CAN_L
```

Use um transceptor cujo lado lógico aceite 3,3 V. CAN_H e CAN_L devem receber
120 ohms em cada extremidade física da rede. Não conecte PC4 ou PC5 diretamente
ao par diferencial. O ST-LINK integrado à NUCLEO pode ser usado para a gravação
inicial e depuração.

## 3. Mapa de memória

| Região | Início | Fim exclusivo | Tamanho |
|---|---:|---:|---:|
| código do bootloader | `0x08000000` | `0x08007800` | 30 KiB |
| configuração NAME/SA | `0x08007800` | `0x08008000` | 2 KiB |
| aplicação | `0x08008000` | `0x0807F800` | 478 KiB / 489472 B |
| metadata | `0x0807F800` | `0x08080000` | 2 KiB |
| SRAM | `0x20000000` | `0x20024000` | 144 KiB |

O port exige o mapeamento padrão dos bancos da Flash (`nSWAP_BANK=1`). A
inicialização falha se detectar Flash menor que 512 KiB ou bancos trocados. Um
erase que cruza `0x08040000` é dividido entre os bancos 1 e 2 pelo port.

A aplicação enviada pelo CAN precisa ter a tabela de vetores em `0x08008000`.
Os oito primeiros bytes continuam sendo o MSP inicial e o Reset Handler Thumb.
O `.bin` não transporta endereço-base; por isso um binário produzido com o
linker do F103 não é compatível com o G0, mesmo que caiba na partição.

## 4. Build

Na raiz do repositório:

```sh
make TARGET=stm32g0b1re all
make test
```

Artefatos gerados:

```text
build/stm32g0b1re/bootloader/CanLoader.elf
build/stm32g0b1re/bootloader/CanLoader.bin
build/stm32g0b1re/bootloader/CanLoader.hex
build/stm32g0b1re/bootloader/CanLoader.map
build/stm32g0b1re/test-application/canloader_test_app.elf
build/stm32g0b1re/test-application/canloader_test_app.bin
build/stm32g0b1re/test-application/canloader_test_app.hex
build/stm32g0b1re/test-application/canloader_test_app.map
```

O build requer GNU Arm Embedded Toolchain. O Makefile procura primeiro o
toolchain incluído no STM32CubeIDE e depois `arm-none-eabi-*` no `PATH`.

## 5. Gravação inicial

Confirme o serial do ST-LINK antes de qualquer erase. O exemplo abaixo apaga
toda a Flash da placa selecionada e grava somente o bootloader:

```sh
STM32_CLI=/caminho/para/STM32_Programmer_CLI
STLINK_SN='<serial confirmado>'

"$STM32_CLI" -l
"$STM32_CLI" -c port=SWD sn="$STLINK_SN" -e all
"$STM32_CLI" -c port=SWD sn="$STLINK_SN" \
  -w build/stm32g0b1re/bootloader/CanLoader.bin 0x08000000 -v -rst
```

Depois de conectar o transceptor e o adaptador USB--CAN, descubra o NAME e
transmita a aplicação relocada:

```sh
canloader ls --interface socketcan --channel can0

canloader update \
  build/stm32g0b1re/test-application/canloader_test_app.bin \
  --interface socketcan --channel can0 \
  --address 0x80 --name '<NAME descoberto>' --run --verbose
```

No macOS, substitua SocketCAN por SLCAN e informe a porta `/dev/cu.*`.

## 6. Diferenças internas em relação ao STM32F103

- FDCAN substitui bxCAN, mas opera em formato clássico sem BRS nem quadros CAN
  FD;
- o filtro de recepção possui entradas para o SA local, destino global e PGN
  Commanded Address;
- a programação da Flash usa doublewords e completa o último bloco com `0xFF`
  fora do tamanho e do CRC da imagem;
- os marcadores de metadata ocupam unidades independentes de 8 bytes;
- o commit do journal é promovido junto com a unidade final de 8 bytes;
- o pedido one-shot de boot usa `TAMP->BKP0R/BKP1R`;
- o handoff do Cortex-M0+ não tenta escrever BASEPRI ou FAULTMASK, registradores
  que não existem nesse núcleo;
- a ferramenta consulta `FLASH_ERASE_UNIT_SIZE` por `INFO`, em vez de presumir
  páginas de 1 KiB.

## 7. Validação física

### 7.1 Coberto em bancada

Com duas Blue Pill e a NUCLEO-G0B1RE no mesmo barramento, CANable2 por SLCAN
a 500 kbit/s:

1. descoberta, `IDENTIFY` e consultas `INFO` da NUCLEO
   (`2026-09-29-222857-bancada-guiada`);
2. update dirigido a partir da aplicação em execução: retorno remoto ao
   bootloader, transferência de 14204 bytes, `VERIFY` com CRC `0x1835F363` e
   `RUN`, com as outras duas placas transmitindo mensagens periódicas (mesma
   sessão);
3. rede com três alvos e seleção por NAME; a NUCLEO perdeu a disputa por `0x80`
   para a Blue Pill de NAME menor e operou em `0x81`;
4. tempo de apagamento e vazão útil com instantes registrados no computador:
   cerca de 22 ms por página de 2 KiB e 7,3 a 7,7 kB/s com a imagem de
   referência (`2026-11-04-tempo-apagamento`);
5. update de 489472 bytes, que ocupa toda a região de aplicação e cruza o
   limite entre os bancos em `0x08040000`: apagamento de 239 páginas em 5,3 s,
   `FINISH` e `RUN` aceitos (mesma sessão; foi preciso `--erase-timeout 15`).

### 7.2 Ainda não verificado na NUCLEO

1. leitura SWD das quatro regiões antes e depois do update;
2. rejeição de MSP, Reset Handler, tamanho e CRC inválidos;
3. interrupção de alimentação durante erase, escrita e promoção da metadata;
4. Commanded Address seguido de reset e nova descoberta;
5. captura CAN independente, com marcas temporais no barramento.

Os ensaios de interrupção e recuperação foram feitos somente na Blue Pill A e
não devem ser atribuídos a este port.

## 8. Fontes primárias de hardware

- STM32G0B1xB/xC/xE datasheet, DS13560;
- STM32G0x1 reference manual, RM0444;
- STM32 Cortex-M0+ programming manual, PM0223;
- NUCLEO-G0B1RE user manual, UM2324;
- STM32CubeG0 HAL Driver v1.4.7 e CMSIS Device G0 v1.4.5, preservados em
  `Drivers/`.
