# Port para NUCLEO-G0B1RE

Este documento descreve o segundo target do CanLoader. Ele foi criado para
a placa NUCLEO-G0B1RE, com STM32G0B1RET6, e reutiliza a FSM, o protocolo J1939,
os formatos persistentes e a ferramenta host do target STM32F103. Foram
substituídas somente as partes ligadas ao MCU: inicialização, FDCAN, Flash,
registradores de backup, LED, handoff, startup e linker scripts.

O port está compilável e coberto pelos testes automatizados do repositório. Ele
ainda não foi validado em uma NUCLEO-G0B1RE física. Essa distinção deve ser
mantida na documentação e na monografia até a execução da campanha em bancada.

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
TestApplication/build/stm32g0b1re/canloader_test_app.elf
TestApplication/build/stm32g0b1re/canloader_test_app.bin
TestApplication/build/stm32g0b1re/canloader_test_app.hex
TestApplication/build/stm32g0b1re/canloader_test_app.map
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
  TestApplication/build/stm32g0b1re/canloader_test_app.bin \
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

## 7. Validação ainda necessária

Antes de declarar suporte físico concluído, execute e arquive pelo menos:

1. descoberta, `IDENTIFY` e todos os seletores `INFO`;
2. update normal, `VERIFY`, `RUN` e retorno remoto ao bootloader;
3. update de imagens que terminem antes e depois de um limite de 2 KiB;
4. update que cruze o limite entre os bancos em `0x08040000`;
5. leitura SWD das quatro regiões antes e depois do update;
6. rejeição de MSP, Reset Handler, tamanho e CRC inválidos;
7. interrupção de alimentação durante erase, escrita e promoção da metadata;
8. Commanded Address seguido de reset e nova descoberta;
9. rede com dois alvos e seleção por NAME;
10. captura CAN temporizada para medir duração, retries e vazão.

Até esses ensaios serem executados, o estado correto é: implementação e build
concluídos; comportamento físico da NUCLEO-G0B1RE ainda não comprovado.

## 8. Fontes primárias de hardware

- STM32G0B1xB/xC/xE datasheet, DS13560;
- STM32G0x1 reference manual, RM0444;
- STM32 Cortex-M0+ programming manual, PM0223;
- NUCLEO-G0B1RE user manual, UM2324;
- STM32CubeG0 HAL Driver v1.4.7 e CMSIS Device G0 v1.4.5, preservados em
  `Drivers/`.
