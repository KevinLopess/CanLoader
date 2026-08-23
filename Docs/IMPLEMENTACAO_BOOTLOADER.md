# Implementação técnica completa do CanLoader

| Campo | Valor |
|---|---|
| documento | especificação *as built* revisada em 19 de agosto de 2026 |
| target | STM32F103C8Tx / Cortex-M3 |
| wire protocol | CanLoader v1 |
| bitrate do target | 500 kbit/s |
| estado | software compilado/testado; validação física pendente |

## Sumário

- [1. Objetivo e escopo deste documento](#1-objetivo-e-escopo-deste-documento)
- [2. Visão geral do sistema](#2-visão-geral-do-sistema)
- [3. Requisitos utilizados na construção](#3-requisitos-utilizados-na-construção)
- [4. Arquitetura de software](#4-arquitetura-de-software)
- [5. Configuração concreta do STM32F103C8Tx](#5-configuração-concreta-do-stm32f103c8tx)
- [6. Mapa de memória](#6-mapa-de-memória)
- [7. Configuração persistente do dispositivo](#7-configuração-persistente-do-dispositivo)
- [8. Metadata da imagem e transação persistente](#8-metadata-da-imagem-e-transação-persistente)
- [9. Inicialização e decisão de boot](#9-inicialização-e-decisão-de-boot)
- [10. Máquina de estados](#10-máquina-de-estados)
- [11. Protocolo CAN v1](#11-protocolo-can-v1)
- [12. CRC-32/ISO-HDLC](#12-crc-32iso-hdlc)
- [13. Fluxo completo de atualização](#13-fluxo-completo-de-atualização)
- [14. Validação da imagem e handoff](#14-validação-da-imagem-e-handoff)
- [15. Contrato exigido da aplicação](#15-contrato-exigido-da-aplicação)
- [16. Operação com vários dispositivos](#16-operação-com-vários-dispositivos)
- [17. Ferramenta host Python](#17-ferramenta-host-python)
- [18. Requisitos e processo de build](#18-requisitos-e-processo-de-build)
- [19. Gravação inicial e primeiro update](#19-gravação-inicial-e-primeiro-update)
- [20. Tratamento de falhas](#20-tratamento-de-falhas)
- [21. Testes e critérios de aceitação](#21-testes-e-critérios-de-aceitação)
- [22. Limitações e requisitos não atendidos](#22-limitações-e-requisitos-não-atendidos)
- [23. Portabilidade para outro MCU](#23-portabilidade-para-outro-mcu)
- [24. Checklist de implementação e liberação](#24-checklist-de-implementação-e-liberação)
- [25. Glossário e convenções](#25-glossário-e-convenções)
- [26. Referências internas](#26-referências-internas)

## 1. Objetivo e escopo deste documento

Este documento especifica, de forma consolidada, a implementação do bootloader
CanLoader existente neste repositório. Ele descreve os requisitos usados no
projeto, as decisões arquiteturais, o mapa de memória, os formatos persistentes,
a máquina de estados, o protocolo CAN, a ferramenta de host, a integração que a
aplicação deve realizar, o processo de construção, a gravação inicial, os testes
e as limitações conhecidas.

O documento trata como fonte de verdade o código-fonte atual, principalmente:

- [`Config/boot_config.h`](../Config/boot_config.h), para constantes do target;
- [`Application/Src/bootloader.c`](../Application/Src/bootloader.c), para a
  máquina de estados e as regras da atualização;
- [`Application/Src/boot_metadata.c`](../Application/Src/boot_metadata.c) e
  [`Application/Src/boot_image.c`](../Application/Src/boot_image.c), para
  persistência e validação;
- [`Protocol/`](../Protocol), para o wire protocol e o CRC;
- [`Portable/stm32f1/Src/`](../Portable/stm32f1/Src), para o port do
  STM32F103/bxCAN/Flash/Cortex-M3;
- [`Tools/canloader/`](../Tools/canloader), para o cliente e a CLI Python;
- [`TestApplication/`](../TestApplication), como implementação de referência do
  contrato exigido da aplicação.

O target entregue é o **STM32F103C8Tx**, normalmente usado na placa Blue Pill.
O protocolo é proprietário e usa conceitos de diagnóstico, fragmentação e
confirmação comuns em redes veiculares, porém é **J1939-inspired, not SAE J1939
compliant**.

> **Estado de validação:** o firmware e as ferramentas possuem testes
> automatizados e são compiláveis, mas os ensaios elétricos e funcionais em
> hardware descritos em [`VALIDATION.md`](VALIDATION.md) ainda precisam ser
> registrados. Portanto, até essa campanha ser executada, vale a afirmação:
> **Hardware validation was not performed.**

---

## 2. Visão geral do sistema

O sistema contém quatro participantes principais:

```text
                         computador do operador
                   +------------------------------+
arquivo .bin -----> | canloader CLI / python-can  |
                   +---------------+--------------+
                                   |
                        USB-CAN / SLCAN / SocketCAN
                                   |
               CAN_H/CAN_L, 500 kbit/s, CAN clássico
                                   |
         +-------------------------+-------------------------+
         |                         |                         |
  +------+-------+          +------+-------+          +------+-------+
  | transceptor  |          | transceptor  |          | transceptor  |
  | + STM32 nó 1 |          | + STM32 nó 7 |   ...    | + STM32 nó N |
  +------+-------+          +------+-------+          +------+-------+
         |                         |                         |
  bootloader/app             bootloader/app             bootloader/app
```

O host nunca recebe permissão para escrever em um endereço absoluto. Ele
seleciona um node ID, anuncia tamanho e CRC, e envia dados sequenciais. O
firmware transforma esses dados em offsets relativos exclusivamente dentro da
partição da aplicação.

Cada dispositivo tem duas formas de identificação:

1. **node ID lógico**, configurável entre 1 e 127 e persistido em Flash;
2. **UID físico de 96 bits**, fornecido pelo próprio STM32 e somente consultado.

O node ID determina os identificadores CAN usados em operações unicast. O UID é
usado para inventário e conferência de identidade, mas não autentica o
dispositivo e não participa de nenhuma decisão criptográfica.

---

## 3. Requisitos utilizados na construção

As tabelas seguintes transformam as restrições do código e as decisões do
projeto em requisitos rastreáveis. “Atendido” significa implementado no estado
atual; não significa necessariamente validado em bancada.

### 3.1 Requisitos de hardware e plataforma

| ID | Requisito | Atendimento e verificação |
|---|---|---|
| `HW-01` | O primeiro target deve ser um STM32F103C8Tx/Cortex-M3 com pelo menos 64 KiB de Flash e 20 KiB de SRAM. | `boot_config.h` fixa esses limites e `boot_flash_init()` lê `FLASHSIZE_BASE`, recusando capacidade inferior a 64 KiB. |
| `HW-02` | O bootloader deve partir da Flash principal em `0x08000000`. | Startup, linker e vetor do bootloader usam essa origem. Em operação normal, BOOT0 deve selecionar a Flash principal. |
| `HW-03` | O clock do bootloader deve usar HSE de 8 MHz, PLL x9, SYSCLK/HCLK de 72 MHz e PCLK1 de 36 MHz. | Configurado em `Core/Src/main.c` e no arquivo `CanLoader.ioc`. |
| `HW-04` | A comunicação deve usar CAN1/bxCAN nos pinos PA11 (`CAN_RX`) e PA12 (`CAN_TX`). | Configuração MSP/HAL do port STM32F1. |
| `HW-05` | A camada física deve usar transceptor CAN externo; PA11/PA12 não podem ser ligados diretamente a CAN_H/CAN_L. | Requisito elétrico de integração. O firmware controla apenas bxCAN em nível lógico. |
| `HW-06` | O barramento deve possuir CAN_H, CAN_L e referência de GND comum, com 120 Ω em cada extremidade física. | Requisito de montagem e validação de bancada. Duas terminações resultam em aproximadamente 60 Ω com a rede desligada. |
| `HW-07` | O LED de estado deve estar em PC13 e ser ativo em nível baixo. | `main.h` e `BOOT_LED_ACTIVE_LOW`. |
| `HW-08` | Erase de Flash deve ocorrer em páginas de 1024 bytes e programação em halfwords de 2 bytes. | `BOOT_FLASH_PAGE_SIZE`, `BOOT_FLASH_PROGRAM_UNIT` e `_Static_assert` contra a HAL. |
| `HW-09` | A gravação inicial do bootloader deve ser feita por SWD/ST-LINK ou mecanismo equivalente. | O protocolo atual atualiza apenas a partição da aplicação; não existe self-update do bootloader. |

### 3.2 Requisitos funcionais do bootloader

| ID | Requisito | Atendimento e verificação |
|---|---|---|
| `RF-01` | Ao ligar, o bootloader deve validar a imagem antes de decidir executá-la. | Metadata, estado persistente, tamanho, CRC, MSP e Reset Handler são relidos. |
| `RF-02` | Com imagem válida, deve existir uma janela normal de 1500 ms para o host assumir o controle. | Estado `BOOT_WINDOW` e `BOOT_WINDOW_MS`. |
| `RF-03` | Sem imagem válida, o dispositivo deve permanecer recuperável no bootloader indefinidamente. | Inicialização segue diretamente para `WAIT_HOST`; não há expiração nesse estado. |
| `RF-04` | A aplicação deve poder solicitar reboot para o bootloader por CAN. | Comando `ENTER_BOOTLOADER`, contrato de registradores BKP e reset de sistema. |
| `RF-05` | Um reboot solicitado pela aplicação deve manter o bootloader disponível por 10 s. | Magias em `BKP->DR1/DR2`, consumo one-shot e `BOOT_REQUEST_WINDOW_MS`. |
| `RF-06` | O host deve consultar versão, target, mapa da aplicação, tamanho/CRC armazenados e UID. | `DISCOVER`, `INFO` e `PROBE`. |
| `RF-07` | O host deve transferir uma imagem binária sequencialmente, sem fornecer endereços absolutos. | `START_UPDATE`, blocos, frames `DATA` e regiões abstratas de Flash. |
| `RF-08` | A imagem somente deve tornar-se executável depois de transferência, CRC e vetores válidos. | Marker `VALID` é o último compromisso persistente da transação. |
| `RF-09` | O host deve poder verificar e executar uma imagem já persistida. | `VERIFY` e `RUN_APPLICATION`. |
| `RF-10` | O host deve poder abortar uma sessão. | `ABORT`, limpeza do contexto e invalidação quando a aplicação já foi apagada. |
| `RF-11` | O dispositivo deve sinalizar visualmente espera, atualização, prontidão e erro. | Máquina de LED baseada no estado. |

### 3.3 Requisitos de múltiplos dispositivos

| ID | Requisito | Atendimento e verificação |
|---|---|---|
| `MULTI-01` | Cada dispositivo deve possuir node ID lógico no intervalo 1..127. | IDs CAN são calculados como base + node; validação existe em C e Python. |
| `MULTI-02` | O node ID deve sobreviver a reset e atualização da aplicação. | Registro próprio na página `0x08003C00`, fora da área da aplicação. |
| `MULTI-03` | Configuração ausente/corrompida deve ter fallback determinístico. | O default é node 1. |
| `MULTI-04` | Deve ser possível alterar o node ID pela rede, somente por unicast. | `SET_NODE_ID`; gravação, readback, ACK no ID antigo e reset. |
| `MULTI-05` | Deve existir inventário que não capture o autoboot nem altere estado persistente. | Quatro broadcasts `PROBE`; comando `canloader ls`. A consulta ainda conta como atividade para o timeout de transferência. |
| `MULTI-06` | O inventário deve expor o UID STM32 de 96 bits. | Leitura de três words em `UID_BASE`; impressão como 24 dígitos hexadecimais. |
| `MULTI-07` | Atualizações mutáveis devem atingir somente o node explicitamente selecionado. | Comandos de update são unicast; broadcasts mutáveis são ignorados. |
| `MULTI-08` | A aplicação deve continuar identificável enquanto estiver executando. | A aplicação de referência responde `PROBE` e marca `APPLICATION_RUNNING`. |

### 3.4 Requisitos de integridade, recuperação e robustez

| ID | Requisito | Atendimento e verificação |
|---|---|---|
| `ROB-01` | Uma imagem parcial ou com CRC incorreto nunca deve ser executada. | Estados persistentes `UPDATING`/`INVALID`, CRC e validação antes de todo jump. |
| `ROB-02` | A invalidação persistente deve preceder o erase da aplicação. | `boot_metadata_begin()` é gravado e relido antes de apagar a primeira página da aplicação. |
| `ROB-03` | Erase e programação devem respeitar região, limite e alinhamento. | API `boot_flash_*`, regiões fechadas e validação antes do endereço físico. |
| `ROB-04` | Erase e programação devem possuir readback. | O port F1 confere `0xFFFF` após erase e cada halfword após programar. |
| `ROB-05` | Transferências devem usar sessão não zero e rejeitar sessão divergente. | Sessão de 8 bits, `session_matches()` e NACK `SESSION`. |
| `ROB-06` | Blocos e frames devem ser estritamente sequenciais. | Índice, sequência, tamanho e offset esperado são conferidos. |
| `ROB-07` | Repetições causadas por perda de ACK não devem reprogramar dados divergentes. | BEGIN/DATA duplicados possuem tratamento idempotente e conferência de payload/bytes comprometidos. |
| `ROB-08` | Uma sessão sem atividade reconhecida deve expirar. | Timeout de 5000 ms em `PREPARE_UPDATE` e `RECEIVE`; consultas bem formadas também renovam o timer, conforme detalhado adiante. |
| `ROB-09` | O host deve retransmitir controles e blocos após timeout recuperável. | Default de três retransmissões além da tentativa inicial. |
| `ROB-10` | O ACK de execução/reconfiguração deve ter oportunidade de sair antes de jump/reset, sem bloquear para sempre. | Atraso mínimo de 50 ms, mailbox ocioso e limite absoluto de 250 ms. |
| `ROB-11` | O handoff deve remover estado residual do bootloader. | Deinit de CAN/RCC/HAL, SysTick desligado, IRQs limpas, VTOR/MSP e máscaras restaurados. |
| `ROB-12` | O firmware não deve depender de alocação dinâmica. | Contexto e buffer são estáticos; heap do linker do bootloader é zero. |

### 3.5 Requisitos de protocolo e host

| ID | Requisito | Atendimento e verificação |
|---|---|---|
| `PROTO-01` | Usar CAN clássico, data frames, IDs padrão de 11 bits e no máximo 8 bytes. | Implementado no port e validado nos decoders. |
| `PROTO-02` | Operar a 500 kbit/s no target STM32F1. | PCLK1 36 MHz, prescaler 4, 18 TQ por bit. |
| `PROTO-03` | Campos multibyte devem ser little-endian. | Helpers equivalentes em C e Python. |
| `PROTO-04` | A versão inicial do wire protocol deve ser 1. | Byte `version=0x01`; versão diferente recebe NACK. |
| `PROTO-05` | O firmware deve responder com ACK/NACK correlacionado por comando e sessão. | Resposta de 8 bytes no ID `0x580 + node`. |
| `HOST-01` | A ferramenta deve executar em Python 3.10 ou superior. | Declarado em `pyproject.toml`; Python 3.9 não é suportado. |
| `HOST-02` | Acesso CAN deve ser abstraído por `python-can`. | Dependência `python-can>=4.3` e adaptador `PythonCanBus`. |
| `HOST-03` | SLCAN serial deve possuir suporte Python instalado. | Dependência `pyserial>=3.5`. |
| `HOST-04` | A CLI não deve elevar privilégios nem executar configuração SocketCAN do sistema. | SocketCAN é configurado externamente; no SLCAN, o backend `python-can` usa `CAN_BITRATE` ao abrir/configurar o adaptador. |
| `HOST-05` | A CLI deve fornecer `ls`, `info`, `update`, `verify`, `run`, `reboot` e `set-node`. | Subcomandos implementados em `canloader/cli.py`. |

### 3.6 Requisitos de construção e qualidade

| ID | Requisito | Atendimento e verificação |
|---|---|---|
| `BUILD-01` | Compilar o firmware para Cortex-M3/Thumb, ABI soft-float. | Flags `-mcpu=cortex-m3 -mthumb -mfloat-abi=soft`. |
| `BUILD-02` | Usar C11/GNU11 e otimização para tamanho. | Bootloader: `-std=gnu11 -Os`; aplicação de teste: `-std=c11 -Os`. |
| `BUILD-03` | Limitar o bootloader por linker a 15 KiB e impedir sobreposição da configuração/aplicação. | Região `FLASH` de 15 KiB e `ASSERT`s no linker. |
| `BUILD-04` | Linkar toda aplicação atualizável a partir de `0x08004000` e terminar antes de `0x0800FC00`. | Linker de referência e validação do `APP_MAX_SIZE`. |
| `BUILD-05` | Gerar ELF, BIN, HEX, MAP e listagem do bootloader. | Alvo `make bootloader`. |
| `BUILD-06` | Permitir testes nativos da lógica sem MCU e testes Python sem barramento real. | `Tests/c` usa ports simulados; `Tests/python` usa barramento injetado. |
| `BUILD-07` | Tratar builds/testes sem hardware como evidência lógica, não elétrica. | Campanha A–J separada em `VALIDATION.md`. |

---

## 4. Arquitetura de software

### 4.1 Separação por camadas

```text
Core/Src/main.c
        |
        v
Application/Src/bootloader.c  <---- máquina de estados e sessão
     |             |
     |             +---- boot_device_config / boot_metadata / boot_image
     |                                      |
     +--------------------+-----------------+
                          v
              Portable/Inc/*.h              Protocol/
             CAN | Flash | Platform      frames | CRC32
                          |
                          v
             Portable/stm32f1/Src/
               HAL + bxCAN + FPEC
```

| Diretório/módulo | Responsabilidade |
|---|---|
| `Core/` | Startup, HAL, clock, GPIO, CAN e laço principal gerados/integrados com CubeMX. |
| `Application/Src/bootloader.c` | Estado global, comandos, sessão, buffer de bloco, timeouts, ACK/NACK e transições. |
| `Application/Src/boot_device_config.c` | Leitura, validação, fallback e persistência do node ID. |
| `Application/Src/boot_metadata.c` | Cabeçalho persistente e markers monotônicos da atualização. |
| `Application/Src/boot_image.c` | CRC da imagem, validação dos vetores e chamada do handoff. |
| `Protocol/` | Enums estáveis, serialização LE, decodificação e CRC-32/ISO-HDLC. |
| `Portable/Inc/` | Contratos independentes de MCU para CAN, Flash e plataforma. |
| `Portable/stm32f1/Src/` | Implementações concretas usando STM32F1 HAL/CMSIS. |
| `Tools/canloader/` | Serialização Python, retries, scanner, cliente de alto nível e CLI. |
| `TestApplication/` | Aplicação relocada mínima e referência para `PROBE`/`ENTER_BOOTLOADER`. |
| `Tests/` | Verificação nativa C e pytest da ferramenta host. |

As camadas `Application` e `Protocol` não chamam diretamente `HAL_CAN_*` ou
`HAL_FLASH_*`. Essa decisão permite testar as regras em um computador e portar
o núcleo para outro MCU sem reescrever a máquina de estados.

### 4.2 Laço de execução

O bootloader é cooperativo e single-thread:

```text
HAL_Init()
SystemClock_Config()
MX_GPIO_Init()
MX_CAN_Init()
bootloader_init()

while (1):
    bootloader_task()
```

Não há RTOS, DMA de protocolo, interrupção de recepção CAN nem alocação
dinâmica. A recepção é consultada na FIFO0. Cada chamada de `bootloader_task()`
processa no máximo oito frames (`BOOT_RX_BUDGET_PER_TASK`) e interrompe o lote
assim que uma resposta fica pendente, evitando sobrescrever a única fila de
resposta.

### 4.3 Contratos das interfaces portáveis

#### CAN

`boot_can_frame_t` transporta `id` de 32 bits, `dlc` e até 8 bytes. O núcleo
depende dos seguintes contratos:

| Função | Contrato |
|---|---|
| `boot_can_init(node)` | valida/configura filtro para o node; ainda não inicia atendimento |
| `boot_can_start()` | coloca o periférico em operação |
| `boot_can_stop()` | para e desinicializa de forma segura para handoff |
| `boot_can_send(frame)` | não bloqueia indefinidamente; retorna `OK`, `BUSY`, `BUS_OFF` ou `ERROR` |
| `boot_can_receive(frame)` | polling; retorna `OK`, `EMPTY`, `BUS_OFF` ou `ERROR` |
| `boot_can_get_state()` | informa `OK`, `BUS_OFF` ou erro; disponível, mas não usado pela máquina atual |
| `boot_can_tx_idle()` | verdadeiro somente quando nenhuma transmissão continua pendente |

O núcleo mantém a resposta quando `send` não retorna `OK`; por isso um port deve
usar `BUSY` como estado transitório e nunca reportar sucesso antes de entregar o
frame ao periférico.

#### Flash

As regiões abstratas são `APPLICATION`, `METADATA` e `DEVICE_CONFIG`. Os status
possíveis são `OK`, `INVALID_ARGUMENT`, `ALIGNMENT`, `RANGE`, `ERASE`,
`PROGRAM`, `VERIFY` e `HARDWARE`.

| Função | Contrato |
|---|---|
| `boot_flash_init()` | valida geometria/capacidade e prepara o port |
| `boot_flash_is_valid_range()` | não altera estado; rejeita overflow e saída da região |
| `boot_flash_erase()` | recebe offset relativo e páginas inteiras; deve conferir o erase |
| `boot_flash_write()` | recebe offset relativo, dados alinhados e comprimento múltiplo da unidade; deve conferir a programação |
| `boot_flash_read()` | copia bytes da região sem permitir leitura fora do limite |

O núcleo pressupõe que `OK` em erase/write inclui readback bem-sucedido. Um novo
port não deve retornar `OK` apenas porque o comando foi aceito pelo controlador
de Flash.

#### Plataforma

| Função | Contrato |
|---|---|
| `boot_platform_millis()` | contador monotônico módulo `2^32`, usado por subtração sem sinal |
| `boot_platform_led_set(on)` | indicação lógica independente da polaridade física |
| `boot_platform_uid_word(i)` | words 0..2 do identificador físico; índice inválido retorna zero no port F1 |
| `boot_platform_take_boot_request()` | testa e consome atomicamente no nível lógico o pedido one-shot |
| `boot_platform_set_boot_request()` | persiste pedido através do próximo reset de sistema |
| `boot_platform_prepare_handoff()` | neutraliza clocks/tick/IRQs que não podem vazar para a aplicação |
| `boot_platform_jump(vector, msp, reset)` | não retorna; instala contexto da aplicação e salta |
| `boot_platform_reset()` | solicita reset e não retorna |

“Atomicamente no nível lógico” significa que o consumidor só aceita o par
completo de magias; as duas escritas físicas BKP são sequenciais, e estados
intermediários não são interpretados como pedido válido.

---

## 5. Configuração concreta do STM32F103C8Tx

### 5.1 Clock

O bootloader usa:

| Sinal | Valor |
|---|---:|
| HSE | 8 MHz |
| PLL | HSE × 9 |
| SYSCLK | 72 MHz |
| HCLK/AHB | 72 MHz |
| PCLK1/APB1 | 36 MHz |
| PCLK2/APB2 | 72 MHz |
| latência de Flash | 2 wait states |

Essa configuração é requisito para o timing CAN fixado no firmware. HSE ausente
ou que não atinja o estado ready leva ao `Error_Handler()` e não chega à máquina
do bootloader. Um oscilador que inicia, mas possui frequência diferente de
8 MHz, não é detectado: SYSCLK e bitrate ficam incorretos.

### 5.2 Timing CAN

O bxCAN usa modo normal com:

| Parâmetro | Valor |
|---|---:|
| clock do periférico | 36 MHz |
| prescaler | 4 |
| Sync Segment | 1 TQ |
| BS1 | 13 TQ |
| BS2 | 4 TQ |
| SJW | 1 TQ |
| total | 18 TQ/bit |
| bitrate | `36 MHz / (4 × 18) = 500 kbit/s` |
| sample point | `(1 + 13) / 18 ≈ 77,8%` |

O firmware habilita retransmissão automática e recuperação automática de
bus-off (`AutoBusOff`). Não há negociação nem autodetecção de bitrate.

O port detecta bus-off ao enviar/receber, mas a máquina de estados não chama
`boot_can_get_state()` nem reinicializa explicitamente o periférico. Se houver
uma resposta pendente durante bus-off, ela pode impedir novas recepções até a
recuperação automática do bxCAN. Esse comportamento precisa ser caracterizado
sob falha real de barramento.

### 5.3 Filtro CAN

O banco 0 é configurado em modo lista, escala de 16 bits, FIFO0. Os quatro
slots recebem:

1. `0x600 + node`: comando unicast;
2. `0x700 + node`: dados da aplicação;
3. `0x500`: broadcast;
4. repetição de `0x600 + node` para completar a lista.

O software também reconfirma o ID depois da retirada da FIFO. Apenas data frames
com identificador padrão são aceitos. Frames RTR, extended ou com DLC incompatível
são rejeitados.

### 5.4 LED PC13

| Estado | Indicação |
|---|---|
| `BOOT_WINDOW` | aceso continuamente durante a janela |
| `WAIT_HOST` | alterna a cada 500 ms |
| `PREPARE_UPDATE`, `ERASE`, `RECEIVE`, `VERIFY` | alterna a cada 125 ms |
| `READY`, `RUN_APPLICATION`, `RESET` | apagado |
| `INIT` ou `ERROR` | alterna a cada 100 ms |

O LED é auxiliar de diagnóstico; nenhuma decisão de integridade depende dele.

---

## 6. Mapa de memória

### 6.1 Flash e SRAM

Todos os fins inclusivos abaixo pertencem à região indicada.

| Região | Início | Fim inclusivo | Tamanho |
|---|---:|---:|---:|
| código do bootloader | `0x08000000` | `0x08003BFF` | `0x3C00` = 15 KiB |
| configuração do dispositivo | `0x08003C00` | `0x08003FFF` | `0x400` = 1 KiB |
| aplicação | `0x08004000` | `0x0800FBFF` | `0xBC00` = 47 KiB = 48128 bytes |
| metadata | `0x0800FC00` | `0x0800FFFF` | `0x400` = 1 KiB |
| SRAM | `0x20000000` | `0x20004FFF` | `0x5000` = 20 KiB |

```text
0x08000000  +----------------------------------+
            | bootloader: código e constantes  | 15 KiB
0x08003C00  +----------------------------------+
            | device config / node ID          | 1 KiB
0x08004000  +----------------------------------+
            | aplicação relocada + vetores     | 47 KiB
0x0800FC00  +----------------------------------+
            | metadata da imagem               | 1 KiB
0x08010000  +----------------------------------+
```

O linker do bootloader expõe somente os 15 KiB iniciais como região executável
`FLASH`. A página `DEVICE_CONFIG` é declarada separadamente e não recebe seções
do ELF. As asserções confirmam que o limite da região de código é `0x08003C00`,
rejeitando qualquer overflow, e que a partição completa do bootloader termina
exatamente em `0x08004000`.

Na SRAM, o linker reserva no mínimo 1 KiB para stack e zero bytes para heap. O
contexto da máquina, a resposta pendente e o buffer do bloco são estáticos. A
aplicação de referência também reserva 1 KiB de stack em seu linker.

### 6.2 Regiões acessíveis pelo port de Flash

A API abstrata conhece apenas:

- `BOOT_FLASH_REGION_APPLICATION`;
- `BOOT_FLASH_REGION_METADATA`;
- `BOOT_FLASH_REGION_DEVICE_CONFIG`.

Cada operação recebe região, offset relativo e comprimento. Antes de calcular o
endereço físico, o port verifica:

- região conhecida;
- offset dentro do tamanho;
- ausência de overflow em `offset + length`;
- comprimento não nulo quando exigido;
- alinhamento de página para erase;
- alinhamento de 2 bytes para programação.

Na programação, um halfword já igual ao valor solicitado é tratado como
idempotente; um halfword diferente de `0xFFFF` e também diferente do desejado é
rejeitado, pois a Flash não pode realizar transição de zero para um sem erase.

Não existe uma região correspondente ao código do bootloader na API gravável.
Assim, um comando de update não pode escolher a página de configuração,
metadata ou bootloader como destino de dados da aplicação.

### 6.3 Requisito para o binário da aplicação

O arquivo enviado é um `.bin` bruto, sem cabeçalho do CanLoader. Ele deve:

1. ter sido linkado com a tabela de vetores em `0x08004000`;
2. conter pelo menos 8 bytes;
3. ter no máximo 48128 bytes;
4. iniciar com MSP e Reset Handler válidos para o STM32F103C8Tx;
5. configurar `SCB->VTOR` para `0x08004000` durante seu startup, mesmo que o
   bootloader já o faça no handoff;
6. usar apenas a faixa `0x08004000..0x0800FBFF` para código/dados persistentes;
7. usar o mesmo bitrate e a mesma pinagem se implementar o controle CAN exigido.

O linker de referência está em
[`TestApplication/linker.ld`](../TestApplication/linker.ld).

---

## 7. Configuração persistente do dispositivo

### 7.1 Formato

Os 12 primeiros bytes da página `0x08003C00` possuem o seguinte registro LE:

| Offset | Tamanho | Campo | Regra |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | `0x46434C43`; bytes `43 4C 43 46`, texto `CLCF` na memória |
| `4..5` | 2 | `format_version` | `1` |
| `6` | 1 | `node_id` | `1..127` |
| `7` | 1 | `node_id_inv` | complemento bit a bit do node |
| `8..11` | 4 | `crc32` | CRC-32/ISO-HDLC dos bytes `0..7` |

O restante da página fica reservado. Na leitura, qualquer falha de magic,
versão, intervalo, complemento ou CRC faz o bootloader usar node 1.

### 7.2 Alteração do node

`SET_NODE_ID` somente é aceito por unicast nos estados `BOOT_WINDOW`,
`WAIT_HOST` ou `READY`.

```text
host no node antigo
    |
    | SET_NODE_ID(novo)
    v
validar 1..127
    |
apagar somente 0x08003C00..0x08003FFF
    |
gravar registro + reler + conferir
    |
marcar pedido de boot em BKP
    |
ACK no ID do node antigo
    |
aguardar >=50 ms e TX ocioso, ou limite de 250 ms
    |
reset -> carregar node novo -> filtro novo
```

Se o novo valor já for o atual, o comando retorna ACK sem apagar a página e sem
reset. Uma perda de energia durante reconfiguração pode corromper o registro; o
fallback será node 1, sem afetar código, aplicação ou metadata.

Atualizações normais da aplicação não apagam esta página. Um mass erase por SWD
apaga e restaura o comportamento default.

---

## 8. Metadata da imagem e transação persistente

### 8.1 Layout

A última página da Flash começa com 30 bytes significativos:

| Offset | Tamanho | Campo | Valor/regra |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | `0x444D4C43`; `CLMD` na memória |
| `4..5` | 2 | `format_version` | `1` |
| `6..7` | 2 | `protocol_version` | `1` |
| `8..11` | 4 | `image_size` | `8..0xBC00` |
| `12..15` | 4 | `image_crc32` | CRC dos bytes reais da imagem |
| `16..19` | 4 | `image_version` | `0` na versão atual |
| `20..23` | 4 | `header_crc32` | CRC dos bytes `0..19` |
| `24..25` | 2 | marker `update` | `00 00` quando comprometido |
| `26..27` | 2 | marker `valid` | `00 00` quando comprometido |
| `28..29` | 2 | marker `invalid` | `00 00` quando comprometido |
| `30..1023` | 994 | reservado | permanece apagado (`0xFF`) |

Cada marker ocupa um halfword independente. Isso permite transições monotônicas
de `1` para `0` sem reapagar a página no meio da atualização.

### 8.2 Interpretação de estado

| Estado lógico | update | valid | invalid | Executável? |
|---|---|---|---|---|
| vazio/ilegível | `FF FF` ou cabeçalho inválido | qualquer | qualquer | não |
| `UPDATING` | `00 00` | `FF FF` | `FF FF` | não |
| `VALID` | `00 00` | `00 00` | `FF FF` | somente após revalidar imagem |
| `INVALID` | `00 00` | qualquer | `00 00` | não |

O marker `invalid` tem precedência. Combinações rasgadas, desconhecidas ou com
CRC de cabeçalho incorreto tornam a metadata ilegível e falham de modo seguro.

### 8.3 Ordem resistente a interrupção

O começo de uma atualização segue obrigatoriamente:

1. mudar para `ERASE` e marcar `app_valid=false` no contexto volátil;
2. apagar a página de metadata;
3. gravar cabeçalho com tamanho/CRC esperado e marker `update`;
4. reler e confirmar estado `UPDATING`;
5. apagar somente as páginas cobertas pela nova imagem;
6. receber e programar todos os blocos;
7. recalcular o CRC total e validar vetores;
8. programar marker `valid`;
9. reler metadata e revalidar a imagem completa.

Se houver reset entre a mudança volátil de `app_valid` e a primeira alteração
persistente da metadata, a metadata/imagem anterior ainda pode permanecer
`VALID` e iniciar normalmente. Depois que a alteração persistente começa e
antes do marker `valid` novo, o próximo boot encontra metadata vazia,
incompleta, `UPDATING` ou `INVALID`, e permanece em `WAIT_HOST`. Se a interrupção
ocorrer depois de programar `valid`, a inicialização ainda recalcula CRC e
vetores antes de permitir execução. Em nenhum caso uma imagem parcial passa
pela decisão de boot.

O layout é **single-slot**: após o erase, a imagem anterior não existe mais.
Existe recuperação por nova atualização, mas não rollback automático.

---

## 9. Inicialização e decisão de boot

`bootloader_init()` executa a seguinte sequência:

1. zera o contexto estático;
2. inicia LED e timestamps;
3. inicializa o port de Flash e verifica capacidade física;
4. carrega e valida o node ID persistente;
5. consulta e consome uma solicitação one-shot da aplicação nos registradores
   de backup;
6. configura filtro e inicia CAN para o node carregado;
7. lê metadata e valida completamente a imagem;
8. entra em `BOOT_WINDOW` se a imagem for válida ou `WAIT_HOST` se inválida.

Falha na inicialização de Flash ou CAN leva a `ERROR`. Nesse estado o laço apenas
mantém a indicação rápida de erro; não existe tentativa automática de
reinicializar o periférico.

### 9.1 Janela normal

Com imagem válida e sem pedido da aplicação, `BOOT_WINDOW` dura 1500 ms. Se
nenhum pedido aceito capturar o bootloader, o CAN é parado e a imagem é
revalidada imediatamente antes do jump.

Durante a janela:

- `PROBE` responde, mas **não captura** o bootloader;
- frame malformado, versão incorreta, comando desconhecido/NACK ou comando
  mutável broadcast não capturam;
- um comando válido que produza ACK, exceto `PROBE`, retém ou muda o estado;
- `START_UPDATE`, `RUN_APPLICATION`, `ABORT`, `ENTER_BOOTLOADER` e
  `SET_NODE_ID` aplicam suas transições próprias.

### 9.2 Janela solicitada pela aplicação

A aplicação escreve o seguinte par de valores antes do reset:

| Registro | Valor |
|---|---:|
| `BKP->DR1` | `0xB007` |
| `BKP->DR2` | `0x4C44` |

Depois executa `NVIC_SystemReset()`. Na próxima inicialização, o bootloader
habilita acesso ao domínio backup, exige as duas magias, limpa os dois
registradores e usa 10000 ms em vez de 1500 ms. A marca é one-shot: outro reset
sem nova solicitação volta à janela normal.

Se a aplicação for inválida, a duração não importa, pois o destino já é
`WAIT_HOST` indefinido.

---

## 10. Máquina de estados

```text
[reset do MCU] -> INIT -- falha de Flash/filtro/start --> ERROR
                    |
                    +-- imagem inválida ----------------> WAIT_HOST
                    |
                    +-- imagem válida ------------------> BOOT_WINDOW
                                                            |
                       +------------------------------------+----------------+
                       | expiração                          | START_UPDATE   |
                       v                                    v                |
                boot_image_jump()                    PREPARE_UPDATE <-------+
                                                            |
                                                   SET_CRC + ERASE
                                                            v
                                                          ERASE
                                                            |
                                                            v
                                                         RECEIVE
                                                            |
                                                         FINISH
                                                            v
                                                          VERIFY
                                                            |
                                                            v
                                                          READY

BOOT_WINDOW -- consulta/ENTER/ABORT aceito -------------> WAIT_HOST
WAIT_HOST ou READY -- START_UPDATE ---------------------> PREPARE_UPDATE
WAIT_HOST/READY/BOOT_WINDOW -- RUN válido --------------> RUN_APPLICATION -> jump
BOOT_WINDOW/WAIT_HOST/READY -- SET_NODE_ID novo válido -> RESET -> reset do MCU
BOOT_WINDOW/WAIT_HOST/READY -- SET_NODE_ID igual -------> mesmo estado, após ACK
PREPARE_UPDATE/RECEIVE -- ABORT/timeout/falha ----------> WAIT_HOST
```

### 10.1 Estados e invariantes

| Estado | Invariante e operações principais |
|---|---|
| `INIT` | contexto sendo preparado; nenhuma operação host deve depender dele |
| `BOOT_WINDOW` | imagem válida; timer de 1,5 s ou 10 s ativo |
| `WAIT_HOST` | estado recuperável, sem expiração automática |
| `PREPARE_UPDATE` | sessão/tamanho aceitos; CRC ainda precisa ser confirmado antes do erase |
| `ERASE` | metadata e aplicação estão sendo alteradas de forma síncrona |
| `RECEIVE` | metadata não executável; exatamente um bloco pode estar ativo |
| `VERIFY` | CRC/vetores/marker estão sendo processados de forma síncrona |
| `READY` | imagem nova validada; aceita verificação, execução ou novo update |
| `RUN_APPLICATION` | ACK enfileirado; aguarda drenagem ou limite absoluto |
| `RESET` | ACK de reendereçamento enfileirado; aguarda drenagem ou limite absoluto |
| `ERROR` | inicialização falhou; não processa protocolo |

`ERASE` e `VERIFY` existem para tornar explícito o estado lógico, mas as rotinas
de Flash e CRC são síncronas dentro da chamada que processa o frame.

Se `ENTER_BOOTLOADER` chegar quando o próprio bootloader já está ativo, ele
responde ACK e muda para `WAIT_HOST` sem executar outro reset. O comportamento é
idempotente e cancela a expiração da boot window. O handler não restringe o
estado: ele também pode interromper `PREPARE_UPDATE`, `RECEIVE`,
`RUN_APPLICATION` ou `RESET`. Nesse caso não chama `clear_transfer()` nem marca
metadata `INVALID`; uma metadata que já estava `UPDATING` continua não
executável, e o contexto volátil permanece até ABORT, timeout ou novo update.

### 10.2 Contexto interno e fila de resposta

Existe uma única instância estática de contexto. Seus grupos de dados são:

| Grupo | Campos/conceito |
|---|---|
| ciclo de vida | estado atual, início do estado, última atividade e timestamp do LED |
| imagem | tamanho esperado, CRC esperado e bytes comprometidos |
| sessão | byte de sessão ativo e flag de CRC recebido |
| bloco | próximo/último índice, sequência, comprimento, próximo frame e bytes no buffer |
| estado derivado | node, validade da aplicação, pedido de boot e flags de bloco/duplicata |
| saída | exatamente um `pending_response` e sua flag |
| armazenamento temporário | buffer de 101 bytes para bloco de 100 bytes mais alinhamento |

`flush_response()` tenta enviar a resposta pendente antes de retirar novos
frames. Se o mailbox estiver ocupado, ela permanece pendente; `queue_response()`
não substitui uma resposta já existente. O laço somente volta a receber quando
a pendência é liberada, exceto quando a rotina de timeout remove explicitamente
uma resposta antiga para publicar o NACK assíncrono de timeout.

Essa organização evita várias respostas simultâneas e mantém o consumo de RAM
determinístico, mas também significa que congestionamento/bus-off pode exercer
backpressure sobre todo o parser.

---

## 11. Protocolo CAN v1

### 11.1 Perfil

| Propriedade | Valor |
|---|---|
| enlace | CAN clássico 2.0, data frames |
| identificador | padrão de 11 bits |
| bitrate F1 | 500 kbit/s |
| payload máximo | 8 bytes |
| byte order | little-endian |
| versão | 1 |
| sessão | 1 byte; update exige 1..255 |
| bloco máximo | 100 bytes no target atual |
| dados por frame | 1..5 bytes |

### 11.2 Identificadores

| Direção | Fórmula | Exemplo node 1 | Função |
|---|---:|---:|---|
| host → todos | `0x500` | `0x500` | controle broadcast permitido |
| dispositivo → host | `0x580 + node` | `0x581` | ACK/NACK |
| host → dispositivo | `0x600 + node` | `0x601` | comandos de controle |
| host → dispositivo | `0x700 + node` | `0x701` | frames DATA |

Os IDs máximos continuam dentro de `0x7FF` para todo node válido. Operações que
mudam estado não são executadas quando chegam em `0x500`; somente `DISCOVER`,
`INFO` e `PROBE` são permitidos por broadcast no bootloader.

Pela arbitragem CAN, o broadcast `0x500` possui prioridade maior que respostas,
comandos e DATA. Entre respostas simultâneas, o menor node usa o menor ID e
vence primeiro. Com nodes exclusivos, os controladores que perdem a arbitragem
retransmitem depois sem corromper a mensagem; o problema crítico ocorre quando
dois dispositivos usam o mesmo ID e tentam transmitir payloads divergentes.

### 11.3 Pedido de controle

ID unicast de comando ou broadcast, DLC exatamente 8:

| Byte | Campo |
|---:|---|
| 0 | `version = 0x01` |
| 1 | `command` |
| 2 | `session` |
| 3 | `parameter` |
| 4..7 | `value`, `uint32_t` little-endian |

Campos reservados devem ser zero. Versão diferente gera
`UNSUPPORTED_VERSION`; ID/DLC/layout incorreto gera `MALFORMED_FRAME` quando o
frame chega ao decoder.

Para interoperabilidade, todo host e toda aplicação compatível devem usar
sessões `1..255`. O bootloader exige valor não zero formalmente ao iniciar um
update, mas aceita zero em algumas consultas stateless; o encoder Python exige
sessão não zero em todo pedido e a aplicação de referência ignora sessão zero.

### 11.4 BEGIN_BLOCK

`BEGIN_BLOCK` usa o ID de comando e DLC 8:

| Byte | Campo | Regra |
|---:|---|---|
| 0 | versão | `1` |
| 1 | comando | `0x20` |
| 2 | sessão | sessão ativa |
| 3 | `block_sequence` | `block_index & 0xFF` |
| 4..5 | `block_index` | 16 bits LE |
| 6 | `block_length` | `1..100`, exatamente o tamanho esperado |
| 7 | reservado | zero |

O endereço é implícito: blocos válidos são cumulativos e o próximo offset é
`bytes_committed`. O host não pode saltar nem retroceder para um endereço livre.

### 11.5 DATA

ID `0x700 + node`, DLC 4..8:

| Byte | Campo | Regra |
|---:|---|---|
| 0 | sessão | mesma sessão do update |
| 1 | sequência do bloco | mesma de `BEGIN_BLOCK` |
| 2 | sequência do frame | inicia em zero |
| 3..DLC-1 | payload | 1..5 bytes |

DATA não transporta versão nem byte de comando; o tipo é implícito pelo ID.
Uma resposta ao commit usa `command=0x21`.

### 11.6 Resposta

ID `0x580 + node`, DLC exatamente 8:

| Byte | Campo |
|---:|---|
| 0 | `version = 0x01` |
| 1 | `kind`: ACK `0x80` ou NACK `0x81` |
| 2 | comando relacionado |
| 3 | sessão ecoada |
| 4..7 | resultado ou erro, `uint32_t` LE |

O cliente descarta frames de outro ID, extended, DLC inválido, versão, sessão
ou comando divergente. Consultas `DISCOVER`/`INFO` usam sessões correlacionadoras
distintas para que uma resposta antiga não satisfaça outra consulta.

### 11.7 Comandos

| Comando | Código | Entrada | Estado/regra | `ACK.value` |
|---|---:|---|---|---:|
| `DISCOVER` | `0x01` | `parameter=0`, `value=0` | consulta operacional | capabilities |
| `INFO` | `0x02` | `parameter=0`, `value=selector` | consulta operacional | valor do seletor |
| `PROBE` | `0x03` | `parameter=selector`, `value=0` | bootloader/app; não captura autoboot | valor do seletor |
| `SET_NODE_ID` | `0x04` | `value=1..127` | unicast; janela/espera/pronto | node novo |
| `START_UPDATE` | `0x10` | sessão não zero, tamanho | janela/espera/pronto | tamanho negociado do bloco |
| `SET_IMAGE_CRC` | `0x11` | CRC esperado | preparação, mesma sessão | CRC esperado |
| `ERASE` | `0x12` | zero | preparação, CRC recebido | tamanho da imagem |
| `BEGIN_BLOCK` | `0x20` | layout especial | recebimento, mesma sessão | bytes comprometidos |
| `DATA` | `0x21` | layout especial | bloco ativo | bytes comprometidos ao fechar bloco |
| `FINISH` | `0x30` | zero | todos os bytes recebidos | CRC calculado |
| `VERIFY` | `0x31` | zero | imagem persistida deve validar | CRC persistido |
| `RUN_APPLICATION` | `0x32` | zero | imagem persistida deve validar | CRC persistido |
| `ENTER_BOOTLOADER` | `0x33` | zero | app compatível ou bootloader | zero |
| `ABORT` | `0x3F` | zero | sessão coincide se ativa | zero |

Duplicatas intencionais:

- `START_UPDATE` idêntico em `PREPARE_UPDATE` retorna o bloco negociado;
- `SET_IMAGE_CRC` repetido com o mesmo CRC retorna ACK;
- `ERASE` repetido antes do primeiro bloco retorna o tamanho sem novo erase;
- `BEGIN_BLOCK` repetido do bloco ativo reinicia o buffer volátil;
- `BEGIN_BLOCK` do último bloco já comprometido retorna o fim do bloco;
- `FINISH` repetido em `READY`, na mesma sessão, retorna o CRC armazenado.

Há uma permissividade específica na implementação atual: a duplicata de
`ERASE`, já em `RECEIVE`, com zero bytes comprometidos e nenhum bloco ativo, é
reconhecida antes de validar `parameter` e `value`. Portanto, mesmo uma repetição
com esses campos reservados não zero pode receber ACK. Hosts conformes sempre
devem retransmitir o frame original com campos zero.

### 11.8 INFO

| Seletor | Valor | Retorno |
|---|---:|---|
| capabilities | 0 | palavra de capacidades |
| base da aplicação | 1 | `0x08004000` |
| tamanho máximo | 2 | `0x0000BC00` / 48128 |
| tamanho armazenado | 3 | metadata legível ou zero |
| CRC armazenado | 4 | metadata legível ou zero |
| UID word 0 | 5 | `UID_BASE + 0` |
| UID word 1 | 6 | `UID_BASE + 4` |
| UID word 2 | 7 | `UID_BASE + 8` |

### 11.9 PROBE e capabilities

`PROBE` usa seletores 0 para capabilities e 1, 2 e 3 para as três words do UID.
O scanner envia quatro pedidos broadcast separados e coleta todas as respostas
durante uma janela configurável por seletor.

Não há retry, jitter ou atraso aleatório entre respostas de scan. O bootloader
responde NACK a seletor PROBE desconhecido; a aplicação de referência apenas o
ignora. O scanner lista somente nodes presentes na resposta de capabilities e
mostra UID ausente (`-`) quando perde uma ou mais das três words.

A palavra de capabilities é:

| Bits | Significado |
|---:|---|
| 0 | aplicação válida |
| 1 | update ativo |
| 2 | resposta originada pela aplicação executando |
| 7..3 | reservado |
| 15..8 | node ID |
| 23..16 | versão do protocolo |
| 31..24 | target code; `0xF1` no port atual |

O UID textual mostrado pelo host concatena `word2 || word1 || word0`, com oito
dígitos hexadecimais por word, totalizando 24 dígitos.

### 11.10 Códigos NACK

| Código | Valor | Significado |
|---|---:|---|
| `NONE` | 0 | sem erro; reservado no enum |
| `INVALID_COMMAND` | 1 | comando/seletor desconhecido |
| `INVALID_STATE` | 2 | operação incompatível com o estado |
| `INVALID_SEQUENCE` | 3 | bloco/frame fora de ordem ou divergente |
| `INVALID_ADDRESS` | 4 | endereço inválido previsto pela interface |
| `INVALID_SIZE` | 5 | tamanho da imagem/bloco/payload/node inválido |
| `FLASH_ERASE` | 6 | falha de erase/preparação persistente |
| `FLASH_WRITE` | 7 | falha de programação/verificação persistente |
| `CRC_MISMATCH` | 8 | CRC calculado diferente |
| `TIMEOUT` | 9 | sessão expirou |
| `UNSUPPORTED_VERSION` | 10 | versão não suportada |
| `INVALID_APPLICATION` | 11 | metadata, CRC ou vetores inválidos |
| `MALFORMED_FRAME` | 12 | ID, DLC ou campos incompatíveis |
| `SESSION` | 13 | sessão zero/divergente |
| `CAN` | 14 | erro CAN reservado pelo protocolo |
| `INTERNAL` | 15 | erro interno reservado |

Nem todo código reservado é necessariamente emitido pelo port atual.

---

## 12. CRC-32/ISO-HDLC

Imagem, metadata e configuração do node usam a mesma variante:

| Parâmetro | Valor |
|---|---:|
| largura | 32 bits |
| polinômio normal | `0x04C11DB7` |
| polinômio refletido no algoritmo C | `0xEDB88320` |
| init | `0xFFFFFFFF` |
| refin/refout | verdadeiro/verdadeiro |
| xorout | `0xFFFFFFFF` |
| vetor `123456789` | `0xCBF43926` |

O CRC da imagem cobre exatamente os `image_size` bytes do `.bin`, sem incluir
preenchimento de página ou o `0xFF` usado para completar um último halfword
ímpar. No host, o resultado equivalente é:

```python
zlib.crc32(data) & 0xFFFFFFFF
```

Não existe CRC adicional do CanLoader em cada frame ou bloco. Durante o
transporte, cada frame depende dos mecanismos de detecção do próprio CAN; a
integridade fim a fim é confirmada pelo CRC da imagem completa em `FINISH` e
`VERIFY`.

CRC detecta corrupção acidental. Não fornece autenticidade, autorização,
confidencialidade ou proteção contra replay/downgrade.

---

## 13. Fluxo completo de atualização

```text
Host                                      Bootloader
 |                                            |
 | DISCOVER --------------------------------> | confirma presença
 | INFO(APP_MAX_SIZE) ----------------------> | informa 48128
 | START_UPDATE(size, session) -------------> | valida; ACK block_size=100
 | SET_IMAGE_CRC(crc) ----------------------> | guarda CRC; ACK crc
 | ERASE -----------------------------------> | metadata UPDATING, erase app
 | <----------------------------------------- | ACK image_size
 |                                            |
 | BEGIN_BLOCK(index=0,length=100) ---------> | abre buffer; ACK committed=0
 | DATA(seq=0,payload<=5) ------------------> |
 | ...                                        | acumula somente em RAM
 | DATA(seq=19,payload<=5) -----------------> | programa 100 bytes + readback
 | <----------------------------------------- | ACK(DATA, committed=100)
 |                                            |
 | [repete por todos os blocos]               |
 |                                            |
 | FINISH ----------------------------------> | CRC + vetores + marker VALID
 | <----------------------------------------- | ACK CRC
 | VERIFY ----------------------------------> | revalida metadata e imagem
 | <----------------------------------------- | ACK CRC
 | RUN_APPLICATION (opcional) -------------> | ACK, drena CAN, handoff
```

### 13.1 Negociação e limites

O target F1 responde `100` a `START_UPDATE`. O host não fixa esse valor na
transferência: usa o bloco devolvido, desde que esteja entre 1 e 100. Somente o
último bloco pode ser menor. O firmware exige que cada bloco tenha exatamente
`min(100, bytes_restantes)`.

O erase da aplicação é arredondado para páginas de 1024 bytes. Se uma imagem
nova for menor que a anterior, páginas antigas além desse arredondamento podem
permanecer fisicamente gravadas, mas estão fora de `image_size`, do CRC e do
intervalo permitido ao Reset Handler; logo não fazem parte da imagem válida.
Esses bytes antigos continuam fisicamente legíveis e não possuem cobertura de
integridade; o processo não implementa apagamento seguro da sobra.

Um bloco cheio produz 20 frames DATA de 5 bytes. Não existe ACK por frame; há
ACK somente quando o bloco inteiro foi gravado e conferido.

### 13.2 Sequenciamento

Para cada bloco, o firmware exige:

- `block_index == next_block_index`;
- `block_sequence == block_index modulo 256`;
- comprimento igual ao esperado;
- `frame_sequence` crescente a partir de zero;
- offset `frame_sequence × 5` dentro do bloco;
- payload exatamente igual a `min(5, bytes restantes no bloco)`;
- sessão igual à sessão ativa.

O buffer possui 101 bytes para acomodar o preenchimento até a unidade de
programação. Um último bloco de tamanho ímpar recebe `0xFF` apenas no byte físico
extra antes de programar o halfword.

### 13.3 Retry e idempotência

O host usa quatro tentativas por default: uma inicial e três retransmissões.
Para um bloco sem ACK:

1. reenvia `BEGIN_BLOCK`;
2. aceita `bytes_committed` apenas se for o início ou o fim exato do bloco;
3. se for o início, reenvia todos os DATA;
4. se for o fim, considera o ACK anterior perdido e avança;
5. qualquer contagem intermediária/adiantada é erro de protocolo.

DATA duplicado anterior à sequência esperada é ignorado somente se seu payload
coincidir byte a byte com o buffer. Payload divergente recebe
`INVALID_SEQUENCE`.

### 13.4 Timeout

| Timer | Valor | Consequência |
|---|---:|---|
| inatividade no firmware | 5000 ms | sai de preparação/recepção e emite NACK assíncrono `ABORT/TIMEOUT` |
| ACK normal no host | 0,5 s | retransmite |
| erase no host | 4 s | retransmite antes de expirar a sessão de 5 s |
| retries | 3 | total de quatro tentativas |

Em `RECEIVE`, timeout tenta marcar a metadata `INVALID`; em
`PREPARE_UPDATE`, a aplicação anterior ainda não foi apagada. Nos dois casos o
contexto da sessão é limpo e o estado volta a `WAIT_HOST`.

Na implementação, “atividade” não significa somente bytes do firmware. Todo
comando conhecido e corretamente decodificado atualiza `last_activity_ms` antes
do dispatch, inclusive `DISCOVER`, `INFO`, `PROBE` e pedidos que depois resultem
em certos NACKs. DATA atualiza o timer quando um novo frame válido é aceito.
Assim, tráfego de inventário/consulta pode prolongar uma transferência parada.

O host reconhece um NACK assíncrono com `command=ABORT` e `value=TIMEOUT` mesmo
enquanto aguarda outro comando **da mesma sessão**, encerrando imediatamente a
operação. Um timeout de outra sessão é descartado pela correlação normal.

---

## 14. Validação da imagem e handoff

### 14.1 Condições de validade

Uma imagem somente é executável se todas as condições forem verdadeiras:

1. metadata possui magic, versões e CRC de cabeçalho válidos;
2. estado persistente é exatamente `VALID`;
3. `image_size` está entre 8 e 48128;
4. CRC dos bytes da aplicação coincide com `image_crc32`;
5. MSP inicial é maior que `0x20000000` e menor ou igual a `0x20005000`;
6. MSP é alinhado em 8 bytes;
7. Reset Handler tem bit Thumb (`bit 0 = 1`);
8. endereço do handler sem bit Thumb está em
   `[0x08004000, 0x08004000 + image_size)`.

O cálculo de CRC lê a Flash em chunks de 128 bytes, evitando buffer proporcional
ao tamanho da imagem.

### 14.2 Preparação do salto

Antes do jump:

1. CAN é parado e desinicializado;
2. imagem é validada novamente;
3. RCC e HAL são desinicializados;
4. interrupções globais são desabilitadas temporariamente;
5. SysTick é zerado/desligado;
6. todas as IRQs NVIC são desabilitadas e limpas;
7. pendências de SysTick e PendSV são limpas;
8. barreiras DSB/ISB são executadas;
9. rotina assembly grava `SCB->VTOR = 0x08004000`;
10. `CONTROL`, `BASEPRI`, `FAULTMASK` e `PRIMASK` são restaurados a zero;
11. MSP recebe o primeiro word da aplicação;
12. execução salta para o Reset Handler.

Se a revalidação final falhar, o firmware solicita reset. No reset seguinte a
imagem inválida não é executada e o target fica recuperável.

### 14.3 Entrega do ACK de RUN

O firmware primeiro enfileira `ACK(RUN_APPLICATION, stored_crc)`. O caminho
normal espera pelo menos 50 ms, ausência de resposta pendente e os três mailboxes
TX livres. Para não bloquear a disponibilidade, 250 ms após o pedido o jump é
forçado mesmo que o ACK não tenha sido transmitido. Nesse caso extremo, a
aplicação inicia, mas o host pode registrar timeout.

---

## 15. Contrato exigido da aplicação

O bootloader sozinho não consegue receber `ENTER_BOOTLOADER` enquanto a
aplicação controla o MCU. Portanto, cada aplicação de produção que deseje reboot
remoto e inventário durante execução deve implementar o contrato abaixo.

### 15.1 Requisitos mínimos de link/startup

- linker com origem `0x08004000` e comprimento máximo `0xBC00`;
- tabela de vetores no início exato da imagem;
- VTOR ajustado para `0x08004000` no startup;
- MSP/Reset Handler dentro dos limites validados;
- nenhuma gravação da página `0x0800FC00` de metadata;
- nenhuma gravação da página `0x08003C00` de configuração, exceto por uma API
  cuidadosamente coordenada com o formato do bootloader.

### 15.2 Descoberta durante execução

A aplicação deve:

1. ler e validar o mesmo registro persistente de node ID;
2. usar fallback node 1 se inválido;
3. configurar CAN no mesmo bitrate/pinagem;
4. aceitar `PROBE` unicast e broadcast;
5. devolver capabilities com `APPLICATION_VALID` e `APPLICATION_RUNNING`;
6. devolver as três words do UID para seus seletores;
7. usar o ID de resposta calculado com o node atual.

### 15.3 Entrada remota no bootloader

A aplicação deve aceitar `ENTER_BOOTLOADER` **somente por unicast no próprio
node**, com protocolo 1, sessão não zero, campos reservados zero e DLC 8.

Depois deve:

1. enviar `ACK(ENTER_BOOTLOADER, session, 0)`;
2. aguardar confirmação de transmissão quando possível;
3. habilitar clocks PWR/BKP e acesso de escrita ao backup domain;
4. escrever `DR1=0xB007` e `DR2=0x4C44`;
5. executar barreira de dados;
6. chamar `NVIC_SystemReset()`.

A aplicação de teste espera 50 ms e confirmação TX; força o reset em 250 ms se
o ACK não concluir. Em uma aplicação com RTOS, a mesma semântica deve ser
preservada com exclusão mútua e sincronização adequadas.

`BKP->DR1` e `BKP->DR2` tornam-se recursos reservados por esse contrato. A
aplicação não deve usá-los simultaneamente para outro protocolo/estado RTC sem
coordenar o formato, ou poderá gerar/perder pedidos de boot.

### 15.4 Aplicação de referência

[`TestApplication/Src/main.c`](../TestApplication/Src/main.c) implementa o
contrato sem HAL e sem alocação dinâmica. Ela:

- usa HSI de 8 MHz e bxCAN a 500 kbit/s;
- responde `PROBE` e `ENTER_BOOTLOADER`;
- pisca duas vezes (80 ms ligado, 100 ms desligado, 80 ms ligado e 740 ms de
  pausa);
- deixa o LED continuamente aceso se receber `PRIMASK != 0`, tornando falha do
  handoff observável;
- usa SysTick de 1 ms e laço não bloqueante.

Ela é uma referência de integração e teste, não uma aplicação final de produto.

No firmware de referência, PCLK1 é 8 MHz, prescaler CAN é 1, BS1 é 13 TQ e BS2
é 2 TQ: são 16 TQ por bit e sample point de 87,5%. O timing difere do
bootloader, mas o bitrate permanece 500 kbit/s.

Esse CAN deriva do HSI interno na aplicação de teste. Uma aplicação de produção
deve verificar a tolerância do oscilador ao longo de tensão e temperatura contra
o orçamento de erro do barramento, preferindo uma fonte de clock com precisão
adequada quando necessário. A escolha HSI ainda não foi validada fisicamente.

---

## 16. Operação com vários dispositivos

### 16.1 Descoberta

`canloader ls` envia, em sequência, quatro broadcasts `PROBE`: capabilities e
três words do UID. Para cada seletor, coleta respostas durante `--scan-timeout`
(default 0,2 s), valida versão/comando/sessão e associa o node ao ID de resposta.

Uma linha válida apresenta:

- node derivado de `response_id - 0x580`;
- target code;
- versão do protocolo;
- modo `application`, `update` ou `bootloader`;
- validade da aplicação;
- UID, se as três respostas foram coletadas.

O node anunciado dentro de capabilities deve coincidir com o node derivado do
ID; entradas divergentes são ignoradas.

### 16.2 Atualização direcionada

Todos os comandos mutáveis usam `--node`. Exemplo para atualizar somente o node
7:

```sh
CAN_BITRATE=500000 python -m canloader reboot \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX \
  --node 7

CAN_BITRATE=500000 python -m canloader update firmware.bin \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX \
  --node 7 \
  --run
```

O `reboot` é necessário quando a aplicação está executando, pois a aplicação de
referência não implementa `DISCOVER`, `INFO` nem comandos de update; ela expõe
somente `PROBE` e `ENTER_BOOTLOADER`. Se o node já estiver no bootloader,
`ENTER_BOOTLOADER` apenas o mantém em `WAIT_HOST`, portanto o mesmo fluxo é
seguro.

Os filtros dos demais nodes não aceitam `0x607`/`0x707`, e broadcasts de update
são ignorados. Assim, apenas o node 7 altera sua Flash, desde que todos os nodes
tenham endereços únicos.

### 16.3 Provisionamento de nodes

Toda placa com a página de configuração apagada usa node 1. Consequentemente,
várias placas novas com o mesmo bootloader entram na rede com o mesmo endereço.
O provisionamento seguro é:

1. conectar uma placa nova por vez;
2. gravar/iniciar o bootloader;
3. executar `set-node` para atribuir endereço único;
4. confirmar com `ls` e UID;
5. desconectar ou adicionar a placa já configurada à rede final;
6. repetir para cada dispositivo.

Se dois dispositivos possuem o mesmo node, suas respostas usam o mesmo CAN ID.
Valores divergentes podem provocar erro de bit e retransmissões; o scanner não
consegue separá-los. Não há address claiming, arbitragem de endereço nem
provisionamento individual por UID na versão atual.

Além disso, `SET_NODE_ID` enviado ao endereço duplicado é aceito por **todos**
os dispositivos que compartilham aquele filtro; o UID não pode ser usado como
destino. Isolar fisicamente as placas é, portanto, obrigatório para corrigir o
conflito de forma determinística.

### 16.4 Comando set-node

```sh
CAN_BITRATE=500000 python -m canloader reboot \
  --interface slcan --channel /dev/cu.usbmodemXXXX --node 1

CAN_BITRATE=500000 python -m canloader set-node 7 \
  --interface slcan --channel /dev/cu.usbmodemXXXX --node 1
```

A CLI recebe o ACK no endereço antigo, cria um cliente para o node novo e usa
`DISCOVER` para confirmar o reset/reendereçamento.

Se o ACK antigo for perdido, a gravação e o reset ainda podem ter acontecido. O
cliente retransmite no endereço antigo e pode terminar em timeout sem tentar o
novo endereço, pois só faz essa confirmação depois de receber o ACK. Nessa
situação, pare comandos mutáveis e execute `canloader ls` para localizar o node
efetivo pelo UID.

---

## 17. Ferramenta host Python

### 17.1 Estrutura

| Arquivo | Responsabilidade |
|---|---|
| `constants.py` | IDs, comandos, seletores, capabilities e NACKs |
| `protocol.py` | frames, validação e serialização LE |
| `crc.py` | CRC equivalente com `zlib.crc32` |
| `bus.py` | adaptador mínimo em torno de `python-can` |
| `client.py` | scanner, correlação, retries, blocos e update completo |
| `cli.py` | argumentos, subcomandos, saída e códigos de erro |
| `errors.py` | hierarquia de falhas configuráveis, CAN, protocolo e dispositivo |

O cliente aceita um barramento injetado, permitindo testes sem importar ou abrir
hardware CAN.

### 17.2 Instalação

Python **3.10 ou superior** é obrigatório. O Python 3.9 fornecido por versões do
Xcode não atende ao `requires-python` do pacote.

```sh
python3 --version
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e 'Tools/canloader[test]'
```

Dependências declaradas:

| Pacote | Restrição | Uso |
|---|---:|---|
| `setuptools` | `>=68` | backend de build |
| `python-can` | `>=4.3` | backends CAN |
| `pyserial` | `>=3.5` | transporte serial usado por SLCAN |
| `pytest` | `>=7`, opcional `test` | testes automatizados |

As dependências possuem apenas limites inferiores e o repositório não contém
lockfile. Para uma entrega reproduzível, o processo de release deve registrar
as versões efetivamente instaladas ou introduzir um arquivo de lock compatível
com a política do projeto.

### 17.3 Subcomandos

| Comando | Resultado |
|---|---|
| `ls` | inventário broadcast que não captura o autoboot nem altera persistência |
| `info` | lê capabilities, mapa, metadata e UID de um node |
| `update FILE` | transfere e verifica `.bin`; `--run` executa ao final |
| `verify` | pede revalidação da imagem persistida |
| `run` | executa a aplicação válida |
| `reboot` | pede à aplicação para resetar e confirma o bootloader |
| `set-node NEW` | persiste endereço, aguarda reset e confirma node novo |

Opções comuns dos comandos por node:

- `--interface`: backend `python-can`;
- `--channel`: canal/dispositivo;
- `--node`: endereço 1..127;
- `--timeout`: ACK normal, default 0,5 s;
- `--erase-timeout`: default 4 s;
- `--retries`: retransmissões após a primeira tentativa, default 3;
- `--verbose`: log dos frames TX/RX.

`ls` não usa `--node`; possui `--scan-timeout` por seletor.

### 17.4 macOS com SLCAN

Descubra o dispositivo serial antes e depois de conectar o adaptador:

```sh
ls /dev/cu.*
```

Em um CANable/CAN adapter com firmware SLCAN, use o caminho `/dev/cu.*`, não o
`/dev/tty.*`, para uma sessão iniciada pelo Mac:

```sh
CAN_BITRATE=500000 python -m canloader ls \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX
```

Exemplo completo no canal observado durante o desenvolvimento:

```sh
CAN_BITRATE=500000 python -m canloader info \
  --interface slcan \
  --channel /dev/cu.usbmodem2074318558421 \
  --node 1 \
  --verbose
```

O nome `/dev/cu.usbmodem...` pode mudar após reconexão. O bitrate deve coincidir
com 500000. A CLI passa `interface` e `channel` ao `python-can`; a variável
`CAN_BITRATE` é consumida pelo backend SLCAN, que envia ao adaptador os comandos
de bitrate/abertura apropriados. O CanLoader não altera o firmware do adaptador,
não oferece `--bitrate` próprio e não executa configuração SocketCAN do sistema.

### 17.5 Linux/SocketCAN

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
ip -details -statistics link show can0

python -m canloader ls --interface socketcan --channel can0
```

O uso de `sudo` ocorre apenas na configuração externa do sistema; a CLI nunca o
invoca.

---

## 18. Requisitos e processo de build

### 18.1 Software necessário

- GNU Make;
- GNU Arm Embedded Toolchain com `arm-none-eabi-gcc`, `objcopy`, `objdump` e
  `size`;
- alternativamente, toolchain GNU empacotado no STM32CubeIDE;
- Python 3.10+ para host/testes;
- compilador C nativo com suporte C11 para testes C;
- STM32CubeProgrammer CLI e ST-LINK para gravação inicial;
- interface CAN e ferramentas próprias do sistema para validação física.

Nenhuma versão exata do GCC ARM é fixada no repositório. O linker usa a palavra
`READONLY`, disponível em GCC 11 ou superior; os comentários do script indicam
que ela deve ser removida se um GCC 10 for usado.

O projeto `.ioc` que originou a configuração registra STM32CubeMX 6.18.1,
device database `DB.6.0.181` e STM32Cube FW_F1 V1.8.7. Esses valores são a base
de reprodução dos arquivos gerados, não uma promessa de compatibilidade com
qualquer versão futura do CubeMX/HAL.

No macOS, o Makefile procura automaticamente o toolchain em:

```text
/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/
  com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin
```

Também procura em `$HOME/apps/stm32cubeide/...`; se não encontrar, usa os
executáveis `arm-none-eabi-*` do `PATH`. É possível sobrescrever
`STM32_GNU_BIN` ou `TOOLCHAIN_PREFIX` explicitamente.

Essa autodetecção vale para o compilador, não para
`STM32_Programmer_CLI`. No macOS, instale o STM32CubeProgrammer separadamente,
adicione sua pasta ao `PATH` ou invoque o executável contido no bundle/plugin do
STM32CubeIDE por caminho absoluto.

### 18.2 Flags principais

Bootloader:

```text
-mcpu=cortex-m3 -mthumb -mfloat-abi=soft
-std=gnu11 -Os -g3
-ffunction-sections -fdata-sections -Wall -Wextra
-DUSE_HAL_DRIVER -DSTM32F103xB
--specs=nosys.specs --specs=nano.specs
-Wl,--gc-sections -static
```

Aplicação de teste:

```text
-mcpu=cortex-m3 -mthumb
-std=c11 -Os -g3 -ffreestanding -fno-builtin
-ffunction-sections -fdata-sections
-Wall -Wextra -Werror -nostdlib
```

### 18.3 Comandos

Na raiz:

```sh
make bootloader
make test-application
make test
```

Quality gate completo a partir de estado limpo:

```sh
make clean
make all
make test PYTHON=.venv/bin/python
```

Alvos disponíveis:

| Alvo | Resultado |
|---|---|
| `make bootloader` | compila bootloader |
| `make test-application` | compila aplicação relocada |
| `make all` | ambos os firmwares |
| `make test-c` | testes nativos C |
| `make test-python` | pytest da ferramenta host |
| `make test` | testes C e Python |
| `make clean` | remove somente os artefatos gerados pelos Makefiles |

### 18.4 Artefatos

Bootloader:

```text
build/bootloader/CanLoader.elf
build/bootloader/CanLoader.bin
build/bootloader/CanLoader.hex
build/bootloader/CanLoader.map
build/bootloader/CanLoader.list
```

Aplicação:

```text
TestApplication/build/canloader_test_app.elf
TestApplication/build/canloader_test_app.bin
TestApplication/build/canloader_test_app.hex
TestApplication/build/canloader_test_app.map
```

O `.bin` do bootloader contém somente suas seções linkadas; a página de device
config não faz parte do arquivo. Gravar isoladamente esse `.bin` não altera um
node já persistido. A página estará apagada/default somente em MCU virgem, após
mass erase ou se for apagada explicitamente.

No quality gate de 18 de agosto de 2026, com GNU Tools for STM32 14.3.1, os
artefatos apresentaram:

| Imagem | text | data | bss | `.bin` | Limite de Flash aplicável |
|---|---:|---:|---:|---:|---:|
| bootloader | 10716 | 12 | 1304 | 10728 bytes | 15360 bytes |
| aplicação de teste | 1596 | 4 | 1044 | 1600 bytes | 48128 bytes |

Identificadores exatos dessa geração:

| Item | Identificador SHA-256/estado |
|---|---|
| base Git | `34f933d91207`; a implementação multi-node estava no worktree ainda não commitado |
| `CanLoader.bin` | `1d2df5cac686e0c319fa3b7c433e14f35f00d7269408c233a92a2931aebeced3` |
| `canloader_test_app.bin` | `66761a1c42eff7182d246cfebd48f8a4d5150ca31a819f2e3484c67900638ee2` |

Esses números são um snapshot de rastreabilidade, não constantes do protocolo;
podem mudar com compilador ou código. Os limites impostos pelos linkers é que
são normativos. Para uma release reproduzível, as alterações devem ser
commitadas/tagueadas e os hashes regenerados a partir desse commit limpo.

---

## 19. Gravação inicial e primeiro update

### 19.1 Gravação por SWD

O mass erase abaixo é destrutivo para todo o target e deve ser usado somente
quando essa limpeza é desejada:

```sh
STM32_Programmer_CLI -c port=SWD -e all
STM32_Programmer_CLI \
  -c port=SWD \
  -w build/bootloader/CanLoader.bin 0x08000000 \
  -v -rst
```

Para a ligação ST-LINK, mantenha BOOT0 baixo e conecte ao menos SWDIO/PA13,
SWCLK/PA14, GND comum e referência/alimentação compatível; NRST é recomendado
quando disponível. Confirme o MCU físico antes de executar `-e all`.

Depois do reset, como metadata e configuração estão apagadas:

- node efetivo é 1;
- aplicação é inválida;
- estado é `WAIT_HOST`;
- LED alterna a cada 500 ms.

### 19.2 Consulta inicial no macOS

```sh
CAN_BITRATE=500000 python -m canloader info \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX \
  --node 1 \
  --verbose
```

Valores esperados para Flash apagada:

```text
Protocol version: 1
Node: 1
Capabilities: 0xF1010100
Application base: 0x08004000
Application maximum size: 48128 bytes
Stored image size: 0 bytes
Stored image CRC: 0x00000000
Device UID: <24 dígitos hexadecimais>
```

### 19.3 Primeiro update

```sh
CAN_BITRATE=500000 python -m canloader update \
  TestApplication/build/canloader_test_app.bin \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX \
  --node 1 \
  --run \
  --verbose
```

O fluxo esperado é transferência, `FINISH`, `VERIFY`, ACK de execução e padrão
de duas piscadas da aplicação.

### 19.4 Retorno ao bootloader

```sh
CAN_BITRATE=500000 python -m canloader reboot \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX \
  --node 1
```

A aplicação deve responder, resetar e o bootloader deve permanecer disponível
por 10 s se ninguém o capturar. A própria CLI envia `DISCOVER` para confirmar a
transição; o ACK desse comando captura `BOOT_WINDOW` e muda para `WAIT_HOST`.
Depois de um `reboot` confirmado, portanto, o bootloader aguarda indefinidamente
até receber `run`, update, ABORT/reset ou outro comando que mude seu estado. A
confirmação de `set-node` produz o mesmo efeito no endereço novo.

---

## 20. Tratamento de falhas

| Falha | Comportamento atual |
|---|---|
| Flash física menor que 64 KiB | inicialização entra em `ERROR` |
| `HAL_CAN_Init()` falha em `MX_CAN_Init()` | entra no `Error_Handler()` anterior à máquina e permanece parado |
| filtro CAN ou `boot_can_start()` falha em `bootloader_init()` | entra em `ERROR`, LED rápido |
| bus-off durante operação | port retorna `BUS_OFF`; não há transição/reinit explícita; ABOM do periférico tenta recuperação |
| metadata vazia/corrompida | aplicação considerada inválida; `WAIT_HOST` |
| config do node corrompida | fallback node 1 |
| tamanho fora de 8..48128 | NACK `INVALID_SIZE` |
| sessão zero em `START_UPDATE` ou sessão divergente em comando/DATA da transferência | NACK `SESSION` |
| bloco/frame fora de ordem | NACK `INVALID_SEQUENCE` |
| erase falha | metadata inválida quando possível; NACK `FLASH_ERASE` |
| preparação da metadata falha | retorna `FLASH_ERASE` ao protocolo mesmo se a causa interna foi programação/readback |
| programação/readback falha | metadata inválida quando possível; NACK `FLASH_WRITE` |
| CRC final diferente | metadata inválida; NACK `CRC_MISMATCH` |
| vetores inválidos | metadata inválida; NACK `INVALID_APPLICATION` |
| inatividade de 5 s | aborta sessão, NACK assíncrono `TIMEOUT` |
| reset/perda de energia durante update | estado persistente não permite jump; aguarda recuperação |
| ACK de RUN não sai em 250 ms | jump é forçado; host pode observar timeout |
| ACK de SET_NODE não sai em 250 ms | reset é forçado; host pode precisar localizar o node novo com `ls` |

O port atual não possui watchdog de software, fila de múltiplas respostas,
retomada persistente de offset nem recuperação de uma sessão após reset.

IWDG e WWDG não são inicializados nem alimentados pelo bootloader. Fault
handlers, handler default e `Error_Handler()` anteriores à inicialização da
máquina permanecem em laço infinito. Se uma aplicação iniciar o IWDG e fizer um
reset que não o desative, o bootloader precisa ser adaptado para alimentá-lo;
esse watchdog não pode ser desligado livremente por software no STM32F1.

O padrão de LED de `ERROR` existe somente para falhas detectadas depois que
`bootloader_init()` controla o contexto. Uma falha de HSE/clock ou HAL anterior
entra no `Error_Handler()` do código CubeMX, desabilita IRQs e fica parada, sem
o padrão de diagnóstico da máquina.

As rotinas de erase/programação executam a partir do mesmo banco de Flash e são
síncronas; não existe cópia de rotina para RAM. Durante essas operações o laço
não atende novos frames. A adequação dos timeouts depende, portanto, do tempo
real de Flash e da carga do barramento.

---

## 21. Testes e critérios de aceitação

### 21.1 Testes C nativos

Cobrem, entre outros:

- vetor conhecido do CRC e helpers LE;
- decodificação e rejeição de frames de controle/bloco/DATA;
- geração dinâmica de IDs de resposta;
- limites de imagem, bloco, sequência e sessão;
- duplicatas idempotentes;
- timeout e invalidação de update parcial;
- comportamento da boot window diante de ruído/broadcast proibido;
- falhas de metadata/erase;
- handoff forçado quando ACK não pode sair;
- node dinâmico, PROBE, UID e reconfiguração;
- janela de 10 s solicitada pela aplicação;
- `ENTER_BOOTLOADER` idempotente;
- layout, CRC e transições da metadata;
- persistência/fallback da configuração do node;
- vetores, CRC e validação integral da imagem.

### 21.2 Testes Python

Cobrem:

- IDs e serialização little-endian;
- broadcast PROBE e extração do node pelo response ID;
- limites de node, payload, bloco e resposta;
- CRC conhecido;
- mapeamento de NACKs;
- adaptação `python-can`;
- retry após timeout;
- descarte de respostas alheias/atrasadas;
- sessões distintas em consultas INFO;
- `set-node` e `enter-bootloader` com ACK exato;
- scan de múltiplos nodes e UIDs;
- timeout assíncrono do dispositivo;
- perda do ACK de bloco e recuperação idempotente;
- tamanho de bloco negociado;
- fluxo completo de update/progresso/verify;
- ABORT best-effort após falha;
- parsing de todos os subcomandos da CLI.

### 21.3 Validação em hardware

Os testes automatizados não exercitam transceptor, terminação, ruído, Flash
física, timing real, perda de alimentação nem múltiplos nodes reais. A campanha
obrigatória está dividida em A–J em [`VALIDATION.md`](VALIDATION.md):

| Sessão | Objetivo |
|---|---|
| A | permanência no bootloader sem aplicação |
| B | CAN, INFO, IDs e mapa |
| C | update completo |
| D | igualdade do CRC host/target |
| E | ACK e handoff |
| F | autoboot após 1500 ms |
| G | interrupção de update e proibição de imagem parcial |
| H | recuperação por novo update |
| I | descoberta da aplicação e reboot remoto |
| J | persistência/reendereçamento do node |

Somente resultados observados no MCU devem ser registrados como aprovados.

No quality gate de 18 de agosto de 2026, os três executáveis C retornaram `OK`
e o pytest retornou `31 passed`. Esse registro comprova a execução das
verificações lógicas naquela revisão, mas não altera o estado pendente da
validação física A–J.

Não há workflow de CI, relatório de cobertura ou configuração de análise
estática (`clang-tidy`, `mypy`, `ruff` etc.) no repositório atual. Os gates
automatizados disponíveis são os Makefiles e as suítes locais descritas acima.

### 21.4 Matriz de rastreabilidade consolidada

Esta matriz liga as famílias de requisitos da seção 3 às principais fontes de
implementação, evidências automatizadas e sessões de bancada. “Aprovado” na
coluna lógica significa que o quality gate descrito acima passou; não substitui
a validação física indicada na última coluna.

| Requisitos | Implementação principal | Evidência automatizada | Validação física relacionada | Estado atual |
|---|---|---|---|---|
| `HW-01`..`HW-09` | `Config/`, `Core/`, `Portable/stm32f1/`, startup e linkers | cross-build do bootloader e da aplicação; testes dos ports simulados | preparação, A, B, E e F | build aprovado; elétrica/MCU pendentes |
| `RF-01`..`RF-11` | `bootloader.c`, `boot_metadata.c`, `boot_image.c` e LED do port | `test_bootloader`, `test_metadata_image` e testes de fluxo Python | A..I | lógica aprovada; bancada pendente |
| `MULTI-01`..`MULTI-08` | `boot_device_config.c`, PROBE/IDs no protocolo, aplicação de referência e scanner Python | cenários multinó em C e Python | I e J, incluindo ensaio com mais de um node | lógica aprovada; rede real pendente |
| `ROB-01`..`ROB-12` | máquina de estados, Flash/metadata, validação de imagem e retries do cliente | três suítes C; testes Python de retry, duplicata, timeout, ABORT e ACK perdido | C..H | lógica aprovada; falha física/energia pendente |
| `PROTO-01`..`PROTO-05` | `Protocol/` e adaptador CAN do target | `test_protocol` e testes de serialização Python | B..E | wire format aprovado logicamente; CAN real pendente |
| `HOST-01`..`HOST-05` | `Tools/canloader/pyproject.toml` e pacote `canloader` | 31 testes pytest e parsing de todos os subcomandos | B..E e I..J | suíte aprovada; adaptador/barramento pendentes |
| `BUILD-01`..`BUILD-07` | Makefiles, linkers, startup e projeto CubeMX | `make clean`, `make all` e `make test` | preparação e registro A..J | build/teste local aprovados; campanha pendente |

---

## 22. Limitações e requisitos não atendidos

### 22.1 Segurança

A versão atual **não é secure boot**. Não existem:

- assinatura digital;
- chave pública/privada ou cadeia de confiança;
- autenticação do host;
- MAC de mensagens;
- criptografia do firmware ou do barramento;
- autorização por node/UID;
- anti-rollback ou contador monotônico;
- proteção contra replay de uma imagem válida;
- versão de imagem funcional; `image_version` permanece zero.

Qualquer participante capaz de transmitir no barramento pode iniciar update de
um node conhecido e fornecer CRC coerente. A proteção implementada é contra
corrupção acidental e escrita fora da partição, não contra um atacante.

### 22.2 Disponibilidade e rollback

- apenas um slot de aplicação;
- sem conservação da imagem anterior após erase;
- sem retomada depois de reset;
- sem update paralelo no mesmo node;
- uma única sessão e um bloco ativo;
- sem janela dinâmica de fluxo ou ACK por frame DATA; existe um ACK no commit de cada bloco;
- sem wear leveling ou contagem de ciclos de Flash;
- sem watchdog de atualização;
- sem caracterização de carga máxima do barramento.

O linker reserva no mínimo 1 KiB para a stack do bootloader, mas essa reserva é
somente uma verificação de layout no link. Não existe análise estática de pior
caso, medição de *high-water mark*, canário de stack ou evidência de margem sob
o pior aninhamento de interrupções. O consumo máximo de stack deve ser medido no
target antes de uma liberação de produção.

### 22.3 Rede

- CAN clássico somente;
- IDs padrão de 11 bits somente;
- bitrate fixo em 500 kbit/s;
- sem CAN FD/BRS;
- sem J1939 real, PGNs, DM14/DM15/DM16 ou TP;
- sem address claiming;
- sem atraso aleatório para respostas de scan;
- nodes duplicados não podem ser distinguidos;
- cenário multinó ainda requer validação elétrica/temporal real.

Os estados `RUN_APPLICATION` e `RESET` não bloqueiam o parser durante a espera
de 50–250 ms. Tráfego concorrente válido nesse intervalo pode mudar novamente o
estado e cancelar o jump/reset originalmente esperado. A CLI normal serializa
os pedidos, mas isso deve ser considerado em redes com mais de um controlador.

### 22.4 Compatibilidade de imagem

O `.bin` não possui manifesto com MCU, origem, versão ou assinatura. A CLI
confere somente tamanho contra o target e o CRC resultante. É responsabilidade
do processo de release garantir que o arquivo foi linkado para o
STM32F103C8Tx, em `0x08004000`, e que sua aplicação implementa o contrato correto.

### 22.5 Proteção física do bootloader

O protocolo não expõe a região do código do bootloader, mas a implementação não
configura option bytes de write protection/readout protection. SWD, mass erase
ou código local privilegiado ainda podem alterar o bootloader e sua configuração.

---

## 23. Portabilidade para outro MCU

A máquina de estados, CRC, layouts de protocolo, metadata e cliente host são
reutilizáveis, mas portar exige um target completo:

1. criar projeto, startup, CMSIS/HAL e flags do novo core;
2. redefinir Flash/SRAM, origem da aplicação e páginas persistentes;
3. criar linker do bootloader e da aplicação com asserções equivalentes;
4. escolher unidade mínima real de programação;
5. revisar os formatos persistentes e seus paddings para que toda escrita seja
   múltipla dessa unidade;
6. escolher tamanho de bloco divisível por essa unidade;
7. implementar `boot_can_*` para o periférico do target;
8. implementar `boot_flash_*` com região, alinhamento, erase e readback;
9. implementar tick, UID, pedido de boot, reset e handoff em
   `boot_platform_*`;
10. atribuir novo `BOOT_TARGET_CODE`;
11. compilar e repetir testes lógicos e campanha de hardware.

Em particular, o registro atual de configuração possui 12 bytes. Em um MCU com
unidade mínima de programação de 8 bytes, `boot_device_config_store_node()`
precisa gravar um formato/padding alinhado, por exemplo 16 bytes; apenas alterar
`BOOT_FLASH_PROGRAM_UNIT` faria a escrita de 12 bytes falhar no port de Flash.

Para um STM32G0, isso inclui FDCAN, nova geometria de Flash, startup Cortex-M0+
e novo handoff. Trocar somente um `#define` não constitui um port. Se o wire
format mudar para CAN FD, IDs de 29 bits ou payload maior, deve ser criada nova
versão de protocolo.

---

## 24. Checklist de implementação e liberação

### 24.1 Bootloader

- [ ] target e HSE correspondem ao projeto;
- [ ] código cabe nos 15 KiB definidos pelo linker;
- [ ] build gera BIN/HEX/ELF/MAP sem erro;
- [ ] configuração e metadata não recebem seções acidentalmente;
- [ ] testes C e Python passam em estado limpo;
- [ ] binário inicial é verificado por SWD.

### 24.2 Aplicação

- [ ] linkada em `0x08004000`;
- [ ] tamanho menor ou igual a 48128;
- [ ] VTOR configurado corretamente;
- [ ] MSP e Reset Handler válidos;
- [ ] não ocupa metadata/configuração;
- [ ] responde `PROBE` com node/UID/capabilities;
- [ ] aceita `ENTER_BOOTLOADER` por unicast;
- [ ] grava as duas magias BKP e reseta após o ACK;
- [ ] firmware enviado é exatamente o `.bin` usado para calcular o CRC.

### 24.3 Rede e provisionamento

- [ ] transceptor adequado em cada MCU;
- [ ] GND comum;
- [ ] duas terminações de 120 Ω, somente nas extremidades;
- [ ] adaptador configurado em 500 kbit/s;
- [ ] unicidade de node assegurada durante provisionamento com placas isoladas;
- [ ] UID registrado junto ao node/endereço físico;
- [ ] inventário `node ↔ UID` de `canloader ls` coincide com o registro de provisionamento;
- [ ] as três words de UID foram coletadas para cada node esperado;
- [ ] update de um node não altera os demais.

### 24.4 Evidência de validação

- [ ] saída do quality gate arquivada;
- [ ] versões das ferramentas registradas;
- [ ] hash/CRC dos artefatos registrados;
- [ ] campanha A–J executada;
- [ ] interrupção de energia/reset testada durante update;
- [ ] handoff e padrão de LED observados;
- [ ] teste real com mais de um node executado;
- [ ] limitações de segurança aceitas pelo contexto de uso.

---

## 25. Glossário e convenções

### 25.1 Siglas

| Termo | Significado neste projeto |
|---|---|
| ACK | *Acknowledgement*: resposta positiva que confirma o comando e sua sessão. |
| ABOM | *Automatic Bus-Off Management*: recuperação automática de bus-off do bxCAN. |
| BKP | Domínio/registradores de backup do STM32; `DR1` e `DR2` transportam a solicitação de boot através de um reset de sistema. |
| CAN | *Controller Area Network* clássico usado como transporte do protocolo. |
| CRC | *Cyclic Redundancy Check*; aqui, CRC-32/ISO-HDLC da imagem. |
| DLC | *Data Length Code*: quantidade de bytes de dados do frame CAN. |
| FPEC | *Flash Program/Erase Controller*: controlador de programação e erase da Flash do STM32F1. |
| HSE | *High-Speed External*: oscilador externo de 8 MHz usado pelo bootloader. |
| HSI | *High-Speed Internal*: oscilador interno usado pela aplicação de referência. |
| MSP | *Main Stack Pointer*: primeiro vetor da aplicação e stack inicial do Cortex-M3. |
| NACK | *Negative Acknowledgement*: resposta de rejeição acompanhada por código de erro. |
| SLCAN | Protocolo serial ASCII, no estilo LAWICEL, usado entre o macOS e certos adaptadores USB-CAN. |
| SWD | *Serial Wire Debug*: interface usada na gravação inicial e depuração. |
| TQ | *Time Quantum*: unidade elementar que compõe o tempo de um bit CAN. |
| UID | Identificador físico de 96 bits gravado de fábrica no STM32. |
| VTOR | *Vector Table Offset Register*: registrador que aponta para a tabela de vetores ativa. |

### 25.2 Termos e unidades

| Termo | Convenção adotada |
|---|---|
| aplicação | Firmware atualizável, armazenado a partir de `0x08004000`. |
| bootloader | Firmware residente que valida, recebe e executa a aplicação. |
| default | Valor de fallback; para o node ID, é 1. |
| erase | Apagamento de uma ou mais páginas de Flash para o estado `0xFF`. |
| handoff | Sequência de desativação do bootloader e transferência de controle para a aplicação. |
| host | Computador/controlador que executa a ferramenta `canloader`. |
| marker | Campo persistente que representa `UPDATING`, `VALID` ou `INVALID`. |
| metadata | Registro persistente de tamanho, CRC, versão, marker e checksum da imagem. |
| node | Endereço lógico CAN de 1 a 127; não é sinônimo de UID. |
| readback | Releitura após erase/programação para confirmar o conteúdo físico gravado. |
| target | Combinação concreta de MCU, memória, clock, periféricos e port; aqui, STM32F103C8Tx. |
| word | Palavra de 32 bits, salvo indicação explícita; *halfword* significa 16 bits. |
| KiB | 1024 bytes. Tamanhos de memória usam base binária. |

---

## 26. Referências internas

- [`README.md`](../README.md): visão geral e quick start;
- [`ARCHITECTURE.md`](ARCHITECTURE.md): arquitetura e portabilidade;
- [`MEMORY_MAP.md`](MEMORY_MAP.md): mapa e formatos persistentes;
- [`PROTOCOL.md`](PROTOCOL.md): especificação isolada do wire protocol;
- [`VALIDATION.md`](VALIDATION.md): campanha integrada A–J;
- [`LIMITATIONS.md`](LIMITATIONS.md): limitações conhecidas;
- [`TCC_DIVERGENCES.md`](TCC_DIVERGENCES.md): delimitação em relação ao TCC;
- [`Tools/canloader/README.md`](../Tools/canloader/README.md): uso da ferramenta;
- [`TestApplication/README.md`](../TestApplication/README.md): aplicação de
  referência.

Este arquivo deve ser atualizado sempre que houver mudança de versão do
protocolo, mapa de memória, formato persistente, target, unidade de programação,
timing CAN, comandos, contrato da aplicação ou critérios de validação.
