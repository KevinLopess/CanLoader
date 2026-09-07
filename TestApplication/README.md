# Aplicação de teste do CanLoader

O diretório contém aplicações mínimas para STM32F103C8Tx e NUCLEO-G0B1RE. Elas
validam o handoff do bootloader e mantêm o dispositivo gerenciável na rede
J1939 depois do salto. Usam exclusivamente o protocolo atual do CanLoader, não
executam atualização da própria partição e não substituem uma aplicação de
produto.

## Objetivos

A imagem foi construída para comprovar em hardware que:

- a aplicação é executada a partir da partição correta;
- VTOR, MSP, Reset Handler e estado das interrupções chegam válidos no handoff;
- o mesmo NAME e Source Address continuam visíveis depois do salto;
- `canloader ls` e `canloader info` distinguem aplicação e bootloader;
- a aplicação aceita `ENTER_BOOTLOADER`, confirma o pedido e solicita reset;
- Commanded Address altera e persiste o preferred SA sem trocar o NAME;
- um update iniciado enquanto a aplicação roda retorna primeiro ao bootloader.

## Variante STM32F103C8Tx

### Mapa de memória

| Item | Valor |
|---|---:|
| origem da aplicação e vector table | `0x08005000` |
| tamanho máximo | `0xAC00` bytes (43 KiB) |
| último endereço da aplicação | `0x0800FBFF` |
| página de metadata | `0x0800FC00..0x0800FFFF` |
| SRAM | `0x20000000..0x20004FFF` (20 KiB) |

O linker falha se a imagem ultrapassar `0x0800FC00`. A página de metadata não é
incluída no `.bin`; ela é criada pelo bootloader durante o update. O
`SystemInit()` posiciona VTOR em `0x08005000`, retorna o clock ao HSI de 8 MHz e
desliga HSE/PLL para tornar o início independente do estado deixado pelo
bootloader.

### Implementação em runtime

A aplicação é freestanding, sem HAL, heap ou alocação dinâmica. Ela reutiliza
diretamente os módulos compartilhados de:

- CRC e metadata;
- configuração persistente do dispositivo;
- composição e parsing J1939;
- Address Claim;
- receptor TP;
- mensagens Proprietary A do CanLoader.

Depois de configurar GPIO, a aplicação:

1. rejeita o handoff se `PRIMASK` ainda estiver diferente de zero;
2. carrega o último NAME/preferred SA válido ou os defaults derivados do UID96;
3. inicializa o receptor TP e autoriza BAM apenas para Commanded Address;
4. configura bxCAN em PA11/PA12 para 500 kbit/s;
5. inicia a FSM de Address Claim, que coloca o primeiro Address Claimed na
   fila de transmissão;
6. configura SysTick a 1 ms;
7. entra no laço cooperativo com `WFI`; nesse laço, transmite a fila, processa
   a janela de arbitragem de 250 ms, atende a rede e atualiza o LED.

O filtro bxCAN da aplicação aceita qualquer data frame extended e rejeita
frames standard/RTR. Os módulos de protocolo validam PGN, destino, DLC e campos
antes de executar uma ação. Essa escolha mantém a aplicação de teste pequena;
o bootloader usa filtros de destino mais restritos.

PA11/PA12 são sinais lógicos CAN_RX/CAN_TX e não devem ser ligados diretamente
a CAN_H/CAN_L. O ensaio exige transceptor compatível com 3,3 V no lado do MCU,
GND comum e terminação nas duas extremidades do barramento.

## Serviços J1939 e CanLoader

Enquanto está executando, a aplicação:

- reivindica o mesmo NAME e preferred SA usados pelo bootloader;
- responde a Request for Address Claimed;
- resolve disputa de SA pelo menor NAME e usa fallback AAC em `0x80..0xF7`;
- responde a `IDENTIFY` e aos seletores `INFO`;
- anuncia as flags `APPLICATION_VALID` e `APPLICATION_RUNNING`;
- recebe Commanded Address de 9 bytes por TP.BAM;
- aceita `ENTER_BOOTLOADER` dirigido ao seu SA;
- responde com NACK `INVALID_STATE` aos demais comandos mutáveis.

O INFO expõe capabilities, base/tamanho máximo da aplicação, unidade de
apagamento, tamanho/CRC da metadata válida, UID96, NAME e SA atual.
IDENTIFY/INFO e todas as respostas usam
Proprietary A `0xEF00` com identificadores CAN extended de 29 bits.

### Retorno remoto ao bootloader

Ao receber `ENTER_BOOTLOADER`, a aplicação enfileira o ACK e agenda o reset. O
caminho normal espera ao menos 50 ms, fila vazia e confirmação da transmissão;
um limite de 250 ms força o reset mesmo se o barramento não ficar ocioso.

Imediatamente antes do reset, são gravadas duas assinaturas nos registradores de
backup: `BKP_DR1=0xB007` e `BKP_DR2=0x4C44`. O bootloader confere e zera os dois
valores, de modo que o pedido seja one-shot, e mantém a janela de serviço aberta
por 10 s. Se nenhum update o capturar, a aplicação válida volta a executar. O
comando de host correspondente é:

```sh
canloader reboot --interface socketcan --channel can0 \
  --address 0x80 --name 0x8000000000123456
```

`canloader update` detecta `APPLICATION_RUNNING` e executa essa transição
automaticamente antes de ERASE/WRITE.

### Commanded Address

O payload aceito é `NAME_LE64 + novo_SA`. Ele deve ter exatamente 9 bytes, o
NAME precisa coincidir e o novo endereço deve estar em `0x00..0xFD`. Quando a
validação passa, a aplicação grava um novo registro no journal de configuração
em `0x08004C00..0x08004FFF` e reinicia Address Claim sem reset obrigatório.

Antes de colocar o novo Address Claimed na fila, `apply_claim_address_change()`
chama `discard_pending_tx()`: `tx_head` e `tx_count` voltam a zero e, se o
mailbox 0 estiver ocupado, `CAN_TSR_ABRQ0` solicita seu cancelamento. Esta
aplicação nunca usa os mailboxes 1 e 2. O novo claim só é carregado no mailbox 0
quando `CAN_TSR_TME0` volta a indicar mailbox vazio, evitando transmitir depois
da troca uma resposta que ainda carregava o SA anterior.

A implementação local de Flash permite escrita e erase somente nessa página de
configuração. Ela não oferece à aplicação um caminho para modificar código,
metadata ou a partição do bootloader.

### Indicação pelo LED

O LED ativo-baixo em PC13 repete continuamente:

1. ligado por 80 ms;
2. desligado por 100 ms;
3. ligado por 80 ms;
4. desligado por 740 ms.

O ciclo total é de 1 s. LED continuamente aceso significa que `main()` recebeu
`PRIMASK!=0`; isso indica falha no contrato de handoff e deve ser tratado como
erro, não como execução normal da aplicação.

## Build

Com GNU Arm Embedded Toolchain no `PATH`, a partir da raiz do repositório:

```sh
make test-application
```

Para a NUCLEO-G0B1RE:

```sh
make TARGET=stm32g0b1re test-application
```

Ou diretamente neste diretório:

```sh
make
make TARGET=stm32g0b1re
```

Um único `Makefile` inclui a configuração correspondente em `Targets/<target>/target.mk`.
Todos os artefatos seguem o padrão `build/<target>/test-application/` na raiz
do repositório:

| Arquivo | Uso |
|---|---|
| `build/<target>/test-application/canloader_test_app.elf` | símbolos, seções e debug |
| `build/<target>/test-application/canloader_test_app.map` | auditoria de memória e referências |
| `build/<target>/test-application/canloader_test_app.bin` | entrada do comando `canloader update` |
| `build/<target>/test-application/canloader_test_app.hex` | inspeção/programação controlada por endereço |

Confira o endereço da vector table e o tamanho antes do ensaio. A partir da
raiz do repositório:

```sh
arm-none-eabi-size build/stm32f103c8/test-application/canloader_test_app.elf
arm-none-eabi-objdump -h build/stm32f103c8/test-application/canloader_test_app.elf
```

O caminho de atualização esperado é:

```sh
canloader update build/stm32f103c8/test-application/canloader_test_app.bin \
  --interface socketcan --channel can0 \
  --address 0x80 --name 0x8000000000123456 --run --verbose
```

Substitua endereço e NAME pelos valores descobertos com `canloader ls`. No
macOS, use o backend SLCAN e a porta `/dev/cu.*` conforme
[o plano de validação](../Docs/VALIDATION.md).

## Variante NUCLEO-G0B1RE

A variante G0 é linkada em `0x08008000`, usa até 489472 bytes da partição de
aplicação e posiciona VTOR nesse endereço. Ela inicializa HSI/PLL em 64 MHz,
FDCAN1 em modo CAN clássico a 500 kbit/s e o LED LD4 em PA5. PC4 é RX e PC5 é
TX, ambos em AF3.

Os artefatos ficam em:

```text
build/stm32g0b1re/test-application/canloader_test_app.elf
build/stm32g0b1re/test-application/canloader_test_app.bin
build/stm32g0b1re/test-application/canloader_test_app.hex
build/stm32g0b1re/test-application/canloader_test_app.map
```

O update correspondente é:

```sh
canloader update \
  build/stm32g0b1re/test-application/canloader_test_app.bin \
  --interface socketcan --channel can0 \
  --address 0x80 --name 0x8000000000123456 --run --verbose
```

O port G0 compila e seus contratos comuns são testados no host, mas a execução
na NUCLEO-G0B1RE ainda precisa ser comprovada em bancada. A montagem e os testes
específicos estão em [STM32G0B1RE.md](../Docs/STM32G0B1RE.md).

## Critérios de aprovação

Após um update completo:

- `canloader verify` retorna o mesmo CRC do `.bin`;
- `canloader run` inicia o padrão de duas piscadas;
- `canloader ls` mostra o mesmo NAME e o modo `application`;
- `canloader info` informa a base, o máximo e a unidade de apagamento do target,
  além de
  `APPLICATION_RUNNING`;
- `canloader reboot` observa o novo claim do bootloader dentro da janela de
  10 s;
- reset físico sem comandos causa autoboot depois da janela normal de 1,5 s.

Os ensaios multinó, disputa de SA, persistência e interrupção de energia estão
detalhados em [Docs/VALIDATION.md](../Docs/VALIDATION.md).

## Limites desta aplicação

- não implementa ERASE/WRITE/FINALIZE nem TP sender;
- não autentica comandos, firmware ou Commanded Address;
- não usa watchdog e não diagnostica detalhadamente bus-off;
- aceita somente CAN clássico a 500 kbit/s;
- usa uma fila TX estática de oito frames;
- se a inicialização CAN falhar, o LED continua piscando, mas os serviços de
  rede ficam indisponíveis;
- não transforma um ensaio sem hardware em evidência de validação física.
