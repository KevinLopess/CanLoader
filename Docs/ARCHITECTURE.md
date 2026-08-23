# Arquitetura do CanLoader

## Visão por camadas

O bootloader foi separado para que as regras de atualização não dependam da HAL
ou de registradores do STM32F103:

```text
Core/Src/main.c
        |
        v
Application/Src/bootloader.c
  |             |             |
  v             v             v
boot_image   boot_metadata   Protocol
  |             |          (frames + CRC)
  +------v------+             |
         |                    |
 Portable/Inc                 |
 boot_flash.h                 |
 boot_can.h  <----------------+
 boot_platform.h
         |
         v
 Portable/stm32f1/Src
 HAL STM32F1 + bxCAN + FPEC + Cortex-M3
```

As responsabilidades concretas são:

| Camada | Responsabilidade | Dependência de MCU |
|---|---|---|
| `Application/Src/bootloader.c` | máquina de estados, sessão, blocos, timeout e respostas | nenhuma |
| `Application/Src/boot_metadata.c` | serialização e transições persistentes de estado | apenas a API abstrata de Flash |
| `Application/Src/boot_image.c` | CRC da imagem, vetores e decisão de jump | APIs abstratas de Flash/plataforma |
| `Protocol/` | comandos, layouts little-endian e CRC-32/ISO-HDLC | nenhuma |
| `Portable/Inc/` | contratos de CAN, Flash e plataforma | nenhuma |
| `Portable/stm32f1/Src/` | adaptação HAL, bxCAN, Flash F1, LED e handoff Cortex-M3 | STM32F1 |
| `Core/` | inicialização gerada pelo CubeMX e laço principal | STM32F1 |
| `Tools/canloader/` | cliente, retries, SocketCAN e CLI | host Python |

`Application` e `Protocol` não chamam `HAL_CAN_*`, `HAL_FLASH_*` nem usam tipos
da HAL. A implementação F1 é o único ponto que conhece `CAN_HandleTypeDef`,
`CAN_TxHeaderTypeDef`, `FLASH_EraseInitTypeDef` e registradores do target.

## Inicialização

O `main.c` gerado pelo CubeMX permanece pequeno:

```text
HAL_Init
  -> SystemClock_Config
  -> MX_GPIO_Init
  -> MX_CAN_Init
  -> bootloader_init
  -> bootloader_task, continuamente
```

No target atual, o clock parte de HSE 8 MHz e PLL x9. O bxCAN usa PA11/PA12,
500 kbit/s e modo normal. Antes de configurar o filtro, o bootloader carrega o
node ID da página persistente; o filtro aceita somente o ID de comando desse nó,
seu ID DATA e o ID de broadcast.

`bootloader_init()` inicializa Flash e CAN e valida a imagem persistida. Falha
de inicialização leva a `ERROR`. Uma imagem válida abre a boot window; sem imagem
válida, o dispositivo entra diretamente em `WAIT_HOST`.

## Fluxo de boot

```text
RESET
  |
  v
INIT -- falha de Flash/CAN --> ERROR
  |
  v
validar metadata + tamanho + CRC + vetores
  |                         |
  | inválida                | válida
  v                         v
WAIT_HOST              BOOT_WINDOW (1500 ms)
  ^                         |          |
  |                         | pedido   | sem pedido aceito
  +-------------------------+          v
                                  handoff para app
```

Um pedido de controle decodificável e aceito retém o dispositivo durante a boot
window, exceto `PROBE`, que foi definido como inventário não invasivo. Uma
solicitação da aplicação usa registradores de backup e abre uma janela de 10 s.
Frames malformados, versões desconhecidas e comandos mutáveis enviados
por broadcast não impedem o autoboot. Se a aplicação é inválida, não há reset em
loop nem expiração: o bootloader continua recuperável pela CAN.

## Máquina de estados

| Estado | Função e saída normal |
|---|---|
| `INIT` | prepara o contexto e os ports; segue para `BOOT_WINDOW`, `WAIT_HOST` ou `ERROR` |
| `BOOT_WINDOW` | aguarda 1500 ms normalmente ou 10 s após solicitação da aplicação; pedido válido/aceito, exceto PROBE, retém o bootloader |
| `WAIT_HOST` | aguarda descoberta, consulta, atualização ou execução de imagem válida |
| `PREPARE_UPDATE` | sessão e tamanho aceitos; aguarda CRC e `ERASE` |
| `ERASE` | grava e confere metadata `UPDATING`, depois apaga as páginas da aplicação |
| `RECEIVE` | recebe um bloco por vez, valida sequências e grava na Flash |
| `VERIFY` | calcula CRC, valida vetores e promove metadata para `VALID` |
| `READY` | imagem completa e novamente validada; aceita nova atualização ou `RUN_APPLICATION` |
| `RUN_APPLICATION` | drena a resposta CAN de execução e faz o handoff |
| `RESET` | drena o ACK de reendereçamento e executa reset para aplicar o novo node |
| `ERROR` | erro de inicialização; somente sinaliza pelo LED |

`ERASE` e `VERIFY` são estados explícitos, mas suas operações são síncronas
dentro do tratamento do comando. O laço limita o consumo de recepção a oito
frames por chamada e nunca aloca memória dinamicamente.

## Fluxo de atualização

```text
DISCOVER / INFO(APP_MAX_SIZE)
          |
          v
START_UPDATE(size) -> SET_IMAGE_CRC(crc) -> ERASE
                                              |
                metadata UPDATING verificada -+
                                              |
                application erase por página -+
                                              v
              [BEGIN_BLOCK -> DATA x N -> ACK] x blocos
                                              |
                                              v
                  FINISH -> CRC -> vetores -> marker VALID
                                              |
                                              v
                           VERIFY -> RUN_APPLICATION opcional
```

O passo `boot_metadata_begin()` acontece e é lido de volta **antes** do erase da
aplicação. Assim que esse passo conclui, a imagem anterior deixa de ser válida.
O marker `VALID` só é programado depois de todos os bytes, do CRC e dos vetores
terem sido confirmados. Um timeout, `ABORT` ou falha depois do erase programa o
marker `INVALID` quando possível e retorna a `WAIT_HOST`.

Esta é recuperação single-slot: uma interrupção não executa imagem parcial, mas
a imagem antiga não é preservada depois do erase. Veja
[LIMITATIONS.md](LIMITATIONS.md).

## Validação e handoff

Uma imagem somente é executável quando:

1. o cabeçalho da metadata, sua versão e seu CRC são válidos;
2. o marker persistente representa `VALID`;
3. o tamanho está entre 8 bytes e `0xBC00`;
4. o CRC dos bytes `[0, image_size)` coincide com a metadata;
5. o MSP inicial está na SRAM, é alinhado em 8 bytes e não aponta para o início
   vazio da SRAM;
6. o Reset Handler possui o bit Thumb e aponta para dentro da imagem.

Antes do jump, o port F1 para e desinicializa o CAN, restaura RCC/HAL, desliga o
SysTick, desabilita e limpa IRQs do NVIC e limpa pendências de SysTick/PendSV.
A rotina final em assembly grava o VTOR em `0x08004000`, restaura `CONTROL`,
`BASEPRI`, `FAULTMASK` e `PRIMASK`, carrega o MSP e salta para o Reset Handler.

Para `RUN_APPLICATION`, o ACK é colocado na fila primeiro. No caminho normal, o
firmware espera no mínimo 50 ms, exige que a resposta tenha sido aceita para
transmissão e aguarda os três mailboxes ficarem livres. Um limite absoluto de
250 ms contado do pedido força o handoff mesmo se a resposta continuar pendente
ou o CAN não ficar ocioso; nesse caso extremo, o ACK pode não chegar ao host.

## Integração com CubeMX

O `CanLoader.ioc`, os arquivos de startup, HAL/CMSIS e os arquivos de projeto do
STM32CubeIDE continuam sendo os do target F1. A chamada ao bootloader e seu
include estão em seções `USER CODE` de `Core/Src/main.c`. As implementações
comuns ficam fora de arquivos gerados.

Depois de regenerar o projeto, confira antes de compilar:

- se `bootloader_init()` e `bootloader_task()` permaneceram nas seções de
  usuário;
- se `Application`, `Protocol` e `Portable` continuam como source folders;
- se os include paths de `Application/Inc`, `Protocol/Inc`, `Portable/Inc`,
  e `Config` permanecem nas configurações Debug e Release;
- se o linker usado continua sendo `STM32F103C8TX_FLASH.ld`.

## Portabilidade para STM32G0

A separação atual permite reutilizar a máquina de estados, metadata, validação,
CRC e protocolo, mas um port real para G0 exige um target completo. O roteiro é:

1. criar um projeto CubeMX/STM32CubeIDE específico para o STM32G0 escolhido,
   com `.ioc`, startup, CMSIS, HAL e flags de Cortex corretos;
2. definir um mapa de Flash/SRAM e linker scripts coerentes com a geometria do
   modelo G0, incluindo página de metadata e origem da aplicação;
3. fornecer uma configuração de target equivalente a `boot_config.h`, com
   tamanho de página, unidade mínima real de programação e tamanho de bloco
   divisível por essa unidade (por exemplo, 96 bytes para uma unidade de 8);
4. implementar `Portable/stm32g0/Src/boot_can_stm32g0.c` sobre FDCAN,
   preservando o contrato `boot_can_*`;
5. implementar `boot_flash_stm32g0.c` com erase, alinhamento, programação,
   readback e limites próprios do G0;
6. implementar `boot_platform_stm32g0.c` com tick, LED, reset e handoff adequados
   ao core ARM usado;
7. compilar `Application/Src` e `Protocol/Src` sem alteração e repetir vetores de
   CRC/protocolo e os testes de falha no novo port;
8. criar uma aplicação de teste e linker do G0 com VTOR e vetores na nova origem.

Trocar somente um `#define` não converte o projeto F1 em G0. FDCAN pode ser
configurado para transportar os mesmos frames clássicos de 11 bits; adotar CAN
FD, payload maior ou IDs de 29 bits mudaria o protocolo e requereria uma nova
versão de wire format. O host usa o tamanho de bloco devolvido por
`START_UPDATE`, portanto não pressupõe 100 bytes em um target futuro.
