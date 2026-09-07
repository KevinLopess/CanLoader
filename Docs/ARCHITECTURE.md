# Arquitetura do CanLoader

Esta arquitetura implementa somente o protocolo atual. Não existe seleção de
formato em runtime nem compatibilidade on-wire com o protótipo anterior de IDs
CAN standard. O valor interno `0x02` distingue os quadros atuais, mas não nomeia
uma segunda versão lançada do CanLoader.

## 1. Objetivos arquiteturais

A implementação foi separada para que regras de atualização, protocolo e
persistência possam ser testadas sem HAL. A existência dos ports STM32F1 e
STM32G0 demonstra essa separação no código, embora cada novo modelo ainda exija
um port explícito e validação própria. Os principais invariantes são:

- nenhuma entrada CAN escolhe diretamente uma região física de Flash;
- somente uma sessão de update e uma operação DM/TP ficam ativas;
- identidade J1939 é NAME; Source Address é rota mutável;
- metadata fica não executável antes do primeiro erase da aplicação;
- o laço principal não aloca memória dinamicamente nem bloqueia esperando CAN;
- o handoff só ocorre após revalidar metadata, CRC e vetores.

## 2. Camadas e dependências

```text
Core/Src/main.c ou Targets/<target>/Core/
        |
        v
Application/Src/bootloader.c  <---------- Config/boot_config.h
  |       |         |       |
  |       |         |       +---- boot_device_config.c
  |       |         +------------ boot_image.c
  |       +---------------------- boot_metadata.c
  v
Protocol/
  boot_j1939.c
  boot_j1939_address_claim.c
  boot_j1939_tp.c
  boot_protocol.c
  boot_crc32.c
        |
        v
Portable/Inc/{boot_can,boot_flash,boot_platform}.h
        |
        v
Portable/stm32f1/Src/       Portable/stm32g0/Src/
  HAL + bxCAN + FPEC          HAL + FDCAN + Flash
  Cortex-M3                   Cortex-M0+

Tools/canloader/              mesma especificação no host
Tests/c e Tests/python        dublês e vetores de regressão
```

| Componente | Responsabilidade |
|---|---|
| `bootloader.c` | FSM, ownership, manifest, operações DM, fila TX, timeout e handoff |
| `boot_device_config.c` | NAME/preferred SA default e journal persistente |
| `boot_metadata.c` | cabeçalho da imagem e markers `UPDATING/VALID/INVALID` |
| `boot_image.c` | CRC, MSP, Reset Handler e salto |
| `boot_j1939.c` | compor/decompor ID de 29 bits e PDU1/PDU2 |
| `boot_j1939_address_claim.c` | Request, claim, disputa, fallback e Cannot Claim |
| `boot_j1939_tp.c` | receptor TP RTS/CTS e BAM limitado |
| `boot_protocol.c` | Proprietary A, DM14, DM15, DM16 e endianess |
| `boot_crc32.c` | CRC-32/ISO-HDLC incremental |
| `Portable/Inc` | contratos independentes de MCU |
| port STM32F1 | HAL CAN, FPEC, UID, LED, backup registers, reset e assembly de jump |
| port STM32G0 | HAL FDCAN, Flash dual-bank, UID, LED, backup registers, reset e handoff Cortex-M0+ |
| host Python | Address Claim do Tool, scanner, TP sender, retry e CLI |

`Application` não conhece tipos da HAL. `Protocol` opera apenas sobre
`boot_can_frame_t`, buffers e inteiros de largura fixa.

### 2.1 Bare-metal e fronteira da HAL

Bare-metal e HAL não são conceitos mutuamente exclusivos. O firmware não usa
RTOS, scheduler, threads ou alocação dinâmica; a execução continua sendo
bare-metal. CMSIS fornece o startup, as definições do núcleo Cortex-M e as
operações necessárias ao handoff. A HAL é uma implementação dos adaptadores de
hardware dos dois targets atuais.

A HAL não é o mecanismo de portabilidade do núcleo. Essa função pertence aos
contratos `boot_can.h`, `boot_flash.h` e `boot_platform.h`. Um port futuro pode
implementar esses contratos com HAL, LL ou registradores, sem alterar
`Application` e `Protocol`. O comando `make check-portability` verifica essa
fronteira e rejeita referências a HAL, LL, cabeçalhos de dispositivo STM32 ou
`main.h` nos diretórios comuns.

O acesso direto a registradores deve ser adotado de forma localizada e apoiado
por uma comparação reproduzível de tamanho, tempo e comportamento em hardware.
Isso evita trocar drivers já exercitados por código específico de família sem
uma necessidade mensurável.

### 2.2 Mapa de memória do STM32F103C8

O target assume 64 KiB de Flash, páginas de 1 KiB e 20 KiB de SRAM:

| Região | Início | Fim inclusivo | Tamanho | Uso |
|---|---:|---:|---:|---|
| código do bootloader | `0x08000000` | `0x08004BFF` | 19 KiB | vetores, lógica, HAL e protocolo |
| configuração do dispositivo | `0x08004C00` | `0x08004FFF` | 1 KiB | journal de NAME/preferred SA |
| aplicação | `0x08005000` | `0x0800FBFF` | 43 KiB (`0xAC00`) | imagem atualizável |
| metadata | `0x0800FC00` | `0x0800FFFF` | 1 KiB | tamanho, CRC e estado da imagem |
| SRAM | `0x20000000` | `0x20004FFF` | 20 KiB | dados, buffers, heap nulo e stack |

O linker do bootloader permite seções carregáveis somente nos primeiros
19 KiB. A página de configuração é acessada pela abstração de Flash e não é
incorporada ao `.bin`. A aplicação possui linker independente, VTOR em
`0x08005000` e limite rígido em `0x0800FC00`; a metadata também não pertence ao
binário da aplicação.

### 2.3 Mapa de memória do STM32G0B1RE

O segundo target usa 512 KiB de Flash, páginas de 2 KiB e 144 KiB de SRAM:

| Região | Início | Fim inclusivo | Tamanho | Uso |
|---|---:|---:|---:|---|
| código do bootloader | `0x08000000` | `0x080077FF` | 30 KiB | vetores, lógica, HAL e protocolo |
| configuração do dispositivo | `0x08007800` | `0x08007FFF` | 2 KiB | journal de NAME/preferred SA |
| aplicação | `0x08008000` | `0x0807F7FF` | 478 KiB (`0x77800`) | imagem atualizável |
| metadata | `0x0807F800` | `0x0807FFFF` | 2 KiB | tamanho, CRC e estado da imagem |
| SRAM | `0x20000000` | `0x20023FFF` | 144 KiB | dados, buffers, heap nulo e stack |

Os endereços são isolados nos headers de `Config/targets/`. A ferramenta host
lê base, tamanho máximo e unidade de apagamento do próprio dispositivo; ela
não deriva o mapa a partir do código de produto.

## 3. Inicialização

O `main` gerado pelo CubeMX executa:

```text
HAL_Init
  -> SystemClock_Config
  -> MX_GPIO_Init
  -> MX_CAN_Init
  -> bootloader_init
  -> forever bootloader_task
```

`bootloader_init()`:

1. zera o contexto e acende o LED;
2. inicializa o port de Flash e confere tamanho e mapeamento exigidos pelo
   target;
3. carrega o último registro NAME/SA válido ou gera defaults pelo UID96;
4. inicializa Address Claim e receptor TP;
5. autoriza BAM exclusivamente para Commanded Address de 9 bytes;
6. consome, se presente, a assinatura one-shot de entrada no bootloader;
7. configura filtros CAN para o SA preferido, destino global e Commanded
   Address;
8. inicia CAN e transmite Address Claimed;
9. valida metadata, CRC e vetores da aplicação;
10. entra em `BOOT_WINDOW` se a aplicação for válida, ou `WAIT_HOST` se não.

Address Claim ocorre em paralelo à janela de boot. O tráfego operacional só é
despachado depois de o claim completar seus 250 ms; Request/Address Claimed e
TP necessário a Commanded Address continuam sendo tratados pela infraestrutura.

## 4. Laço cooperativo

Cada `bootloader_task()` executa, em ordem:

1. atualiza indicação de LED;
2. avança Address Claim e coleta seus frames;
3. avança timeout TP e coleta CTS/EOM/ABORT;
4. tenta drenar a fila TX;
5. recebe no máximo oito frames CAN;
6. para a recepção se a fila TX ficar sem espaço;
7. executa novamente claim/TP e fila TX;
8. executa erase pendente somente após o DM15 PROCEED ter sido drenado;
9. verifica timeout global de update;
10. decide autoboot, jump solicitado ou reset.

Há uma fila circular estática de oito frames. A recepção e transmissão não
fazem espera ocupada. Erase, programação e cálculo de CRC são síncronos por
limitação do target, mas não há heap, RTOS ou callback de interrupção na lógica
de aplicação.

## 5. Máquina de estados do bootloader

| Estado | Papel | Saída normal |
|---|---|---|
| `INIT` | inicialização transitória | `BOOT_WINDOW`, `WAIT_HOST` ou `ERROR` |
| `BOOT_WINDOW` | espera 1,5 s, ou 10 s após pedido da app | update, espera explícita ou jump |
| `WAIT_HOST` | serviço sem sessão mutável | `PREPARE_UPDATE`, `RUN_APPLICATION` |
| `PREPARE_UPDATE` | tamanho e owner aceitos; aguarda CRC/ERASE | `ERASE` |
| `ERASE` | metadata `UPDATING` e erase de páginas | `RECEIVE` ou `WAIT_HOST` |
| `RECEIVE` | DM14/DM16 sequencial e close | `VERIFY` ou erro |
| `VERIFY` | CRC/vetores/marker valid em finalização | `READY` ou `WAIT_HOST` |
| `READY` | imagem válida e sessão concluída em RAM | novo update ou jump |
| `RUN_APPLICATION` | drena ACK e faz handoff | aplicação |
| `RESET` | reservado para transição que exige reset | reset |
| `ERROR` | falha de inicialização de Flash/CAN/claim | somente sinalização |

`RESET` permanece na enumeração, mas o Commanded Address atual persiste e
reinicia Address Claim sem resetar o MCU.

```text
                       imagem válida
RESET -> INIT ----------------------------> BOOT_WINDOW
          |                                    | 1,5 s / 10 s
          | imagem inválida                    v
          +-------------------------------> WAIT_HOST
                                              |
                                      START_UPDATE
                                              v
                                       PREPARE_UPDATE
                                              |
                                  SET_CRC + DM14 ERASE
                                              v
                                           ERASE
                                              |
                                         DM15/close
                                              v
                                          RECEIVE
                                    DM14/DM16 × blocos
                                              |
                                           FINALIZE
                                              v
                                           VERIFY
                                      falha /       \ sucesso
                                  WAIT_HOST          READY
                                                        |
                                                       RUN
                                                        v
                                                RUN_APPLICATION
                                                        |
                                                     handoff
```

## 6. Contexto e ownership

O contexto mantém:

- estado e timestamps;
- validade da aplicação;
- NAME, preferred/current SA;
- owner SA e sessão proprietária de 1 byte;
- tamanho e CRC esperados;
- bytes já comprometidos;
- operação DM atual e última operação;
- pointer/length atual e anterior;
- flags de erase, DM16 esperado e close pendente;
- buffers estáticos de TP/DM16, Commanded Address e fila TX.

`START_UPDATE` vincula a sessão ao Source Address do Tool. O host, por sua vez,
vincula esse SA ao NAME observado. Frames mutáveis de outra origem ou sessão
recebem erro. Se o SA do target mudar durante update por disputa, o update é
invalidado; a ferramenta precisa resolver novamente o mesmo NAME.

O firmware embarcado não armazena o NAME do host na sessão; essa é uma
limitação conhecida. A proteção contra troca de identidade do Tool é feita no
cliente e a rede não possui autenticação criptográfica.

## 7. Caminho de recepção

```text
boot_can_receive
  -> valida extended/data frame
  -> Address Claim observa todo frame relevante
  -> boot_j1939_decode_frame
       |
       +-- Address Claimed/Request: já tratados
       +-- TP.CM/TP.DT: receptor TP
       +-- antes de claim operacional: descarta aplicação
       +-- Proprietary A local: controle/manifest
       +-- DM14 local: operação de memória
       +-- DM16 local direto: bloco de 1..7 bytes
```

Os filtros físicos reduzem o tráfego, mas não substituem a validação de PGN,
destino e campos feita em software. Frames standard, RTR e IDs acima de 29 bits
não viram frames do protocolo.

## 8. Address Claim e mudança de endereço

O módulo de claim usa estados `INITIAL`, `CLAIMING`, `CLAIMED`,
`WAIT_CANNOT_CLAIM` e `CANNOT_CLAIM`. O menor NAME numérico vence. O target AAC
perdedor tenta a faixa `0x80..0xF7`; depois agenda Cannot Claim em atraso
pseudoaleatório de 0 a 153 ms.

Request pode ser global ou dirigido ao SA atual e solicita especificamente
Address Claimed. Commanded Address chega por BAM, é aceito somente se NAME e
tamanho coincidirem, novo SA estiver em `0x00..0xFD` e nenhum update estiver
ativo. A configuração é persistida, relida, os filtros são reconfigurados e um
novo claim começa.

Toda mudança efetiva do SA, causada por Commanded Address ou por disputa de
claim, passa por uma transição de TX antes de expor a nova rota. O bootloader:

1. zera `tx_head`/`tx_count`, descartando respostas ainda na fila circular;
2. chama `boot_can_abort_pending_tx()`, que cancela os três mailboxes bxCAN ou
   os três buffers da fila FDCAN, conforme o target;
3. invalida uma sessão de update ativa, caso a mudança tenha vindo de disputa;
4. reconfigura os filtros para o novo SA, destino global e Commanded Address;
5. somente então coleta o Address Claimed pendente do módulo de claim para a
   fila TX.

Falha ao solicitar o abort dos mailboxes ou ao reconfigurar os filtros leva o
bootloader a `ERROR`. Essa ordem impede que uma resposta já enfileirada com o SA
anterior seja transmitida depois do novo claim.

### 8.1 Identidade default e journal

Na ausência de registro persistente válido, o firmware calcula os 21 bits de
Identity Number aplicando CRC-32 ao UID96 do STM32, usa Manufacturer Code `0`,
zera os demais campos funcionais, ativa o bit Arbitrary Address Capable e parte
do preferred SA `0x80`. Esse NAME é adequado apenas à bancada: produção exige
Manufacturer Code e identidade administrados corretamente.

A página de configuração contém registros append-only de 32 bytes: 32 slots no
F103 e 64 no G0. Cada registro inclui magic, versão de formato 2, tamanho,
geração, NAME, endereço e seu complemento, flags e CRC. A unidade física que
contém o commit é programada por último: 2 bytes no F103 e os 8 bytes finais no
G0. Na leitura, slots parciais ou com CRC inválido são ignorados. Quando a
página fica cheia, a próxima mudança apaga a página e reinicia no slot zero;
essa compactação de página única é uma fronteira de tolerância a queda de
energia.

## 9. Receptor TP

O target possui um único receptor TP:

- expectativa RTS é armada somente após DM14 WRITE aceito;
- `(source, destination, PGN, total_size)` precisam coincidir;
- janela concedida é `min(8, max_do_sender, pacotes_restantes)`;
- cada TP.DT deve ter a próxima sequência exata;
- deadline de 1250 ms é renovado a cada progresso;
- mensagem completa gera EOM_ACK antes do processamento da operação;
- BAM é autorizado apenas para Commanded Address;
- sessão incompatível ativa gera ABORT reason 1;
- RTS inválido usa reason 2; timeout usa reason 3;
- frames TP alheios à sessão podem ser ignorados.

O buffer de DM16 é dimensionado como marcador `FF` mais o bloco do target: 101
bytes no F103 e 97 no G0. A biblioteca genérica conhece o limite TP 1785, mas o
bootloader não reserva esse espaço em RAM. O perfil limita WRITE a no máximo
100 bytes e o tamanho efetivo é anunciado por `START_UPDATE`.

## 10. Transação de Flash

```text
START_UPDATE(size)
  -> SET_IMAGE_CRC(crc)
  -> INFO(FLASH_ERASE_UNIT_SIZE)
  -> DM14 ERASE(APP_BASE, ceil(size/erase_unit))
       -> DM15 PROCEED enfileirado e transmitido
       -> metadata_begin: erase metadata, grava UPDATING, relê
       -> erase das páginas da aplicação, readback
       -> DM15 COMPLETED
       -> DM14 close com mesmo pointer/length
  -> para offset = 0; offset < size; offset += bloco_anunciado
       -> DM14 WRITE(APP_BASE+offset, expected_length)
       -> DM15 PROCEED
       -> DM16 direto ou TP
       -> programa unidades físicas e verifica
       -> DM15 COMPLETED
       -> DM14 close com mesmo pointer/length
  -> FINALIZE(expected_crc)
       -> recalcula CRC
       -> valida MSP e Reset Handler
       -> programa VALID e relê
       -> valida tudo novamente
```

EOM_ACK significa apenas que o receptor TP recebeu todos os bytes. A confirmação
de Flash é DM15. O close DM14 mantém pointer/length da operação; não há timer de
close separado, e a sessão inteira expira após 5 s sem atividade.

Um bloco final que não completa a unidade física recebe padding `0xFF`: até um
byte no F103 e até sete no G0. `bytes_committed`, tamanho da metadata e CRC
continuam usando o comprimento lógico informado pelo host; o padding não passa
a fazer parte da imagem.

## 11. Idempotência e falhas

- `START_UPDATE` duplicado pelo mesmo owner/session/size retorna o mesmo bloco;
- `SET_IMAGE_CRC` repetido com o mesmo valor é aceito;
- DM14 ERASE/WRITE repetido enquanto atual retorna BUSY/PROCEED;
- DM14 repetido após commit retorna COMPLETED;
- DM16 órfão ou de tamanho diferente é rejeitado;
- pointer de WRITE precisa ser exatamente `APP_BASE+bytes_committed`;
- falha fatal de write invalida metadata e sai da sessão;
- falha TP encerra a operação corrente e permite diagnóstico/retry conforme
  estado, sem aceitar DT órfão;
- `ABORT` do owner invalida a atualização quando a Flash já foi apagada;
- 5 s sem atividade aceita do owner invalidam a sessão e geram NACK assíncrono
  de timeout.

Como o layout é single-slot, invalidar evita boot parcial, mas não recupera a
imagem anterior.

## 12. Validação e handoff

Uma imagem somente é executável se:

1. metadata tem magic, versões e CRC de cabeçalho válidos;
2. estado persistente é `VALID` sem marker `INVALID`;
3. `image_size` está entre 8 e o máximo do target (44032 no F103 ou 489472 no
   G0B1RE);
4. CRC-32/ISO-HDLC da Flash coincide;
5. MSP está na SRAM e alinhado em 8 bytes;
6. Reset Handler tem Thumb bit e aponta dentro do tamanho declarado.

Antes do jump, o port desinicializa CAN/HAL/RCC, desabilita SysTick, desabilita
e limpa IRQs NVIC, limpa PendSV/SysTick pendentes e executa DSB/ISB. A rotina
assembly escreve VTOR, zera os registradores de máscara disponíveis no núcleo,
carrega MSP e salta. No Cortex-M3 isso inclui `BASEPRI` e `FAULTMASK`; o
Cortex-M0+ não possui esses dois registradores.

Para RUN, o ACK é enfileirado primeiro. O caminho normal aguarda pelo menos
50 ms, fila vazia e mailboxes livres; 250 ms é o limite absoluto, portanto o
jump ainda acontece se o ACK não puder ser observado.

## 13. Aplicação de referência

Cada target possui linker, startup e aplicação de referência coerentes com seu
mapa. A variante F103 é freestanding e linkada em `0x08005000`; a variante G0
usa a HAL do port e é linkada em `0x08008000`. As duas reutilizam os módulos de
NAME/config, J1939, Address Claim, TP e protocolo. Em runtime:

- reivindica o mesmo NAME/SA;
- responde a Request/Address Claimed;
- responde a IDENTIFY e INFO com capability `APPLICATION_RUNNING`;
- aceita Commanded Address por BAM e persiste o novo SA;
- aceita somente `ENTER_BOOTLOADER` entre comandos mutáveis;
- escreve a assinatura nos backup registers;
- espera 50 ms e confirmação TX quando possível, com reset forçado em 250 ms;
- pisca duas vezes para tornar o handoff visível.

A variante F103 usa HSI a 8 MHz e bxCAN em PA11/PA12. A variante G0 usa clock
de 64 MHz e FDCAN1 em PC4/PC5. Ambas operam em CAN clássico a 500 kbit/s. Elas
não implementam update da própria partição em execução e só persistem a página
de configuração ao processar Commanded Address.

Na variante F103, como a aplicação de referência transmite exclusivamente pelo mailbox 0, sua
transição de endereço zera a fila TX e escreve `CAN_TSR_ABRQ0` se esse mailbox
estiver ocupado. O novo Address Claimed é enfileirado em seguida, mas só pode
entrar no mailbox quando `CAN_TSR_TME0` indicar que o cancelamento terminou.

## 14. Host

A CLI é uma pilha raw síncrona para funcionar igualmente em SocketCAN e SLCAN:

```text
bus.py          abertura python-can e conversão de frames
j1939.py        ID, PGN e NAME
address_claim.py AddressBook e claim do Tool
transport.py    TP sender/encoders
protocol.py     Proprietary A e DM14/15/16
client.py       scanner, identity lock, update e retries
cli.py          argumentos e apresentação
```

O scanner reivindica o SA do Tool, pede todos os claims, preserva conflitos por
endereço e só identifica CanLoader por `IDENTIFY` dirigido. Antes de ação
mutável, o cliente resolve o NAME esperado e monitora novos claims para detectar
troca de rota ou takeover.

## 15. Portabilidade

O segundo port confirma que `Application`, `Protocol` e a maior parte dos
testes permanecem comuns. Isso não significa compatibilidade automática com
todo STM32. Para outro MCU, devem ser substituídos, no mínimo:

1. startup/CMSIS/HAL e flags de arquitetura;
2. mapa, linker scripts e tamanho real de Flash/SRAM;
3. geometria de erase e unidade de programação;
4. CAN/FDCAN mantendo CAN clássico de 29 bits se o wire v2 for preservado;
5. UID/default NAME e mecanismo de persistência;
6. tick, LED, backup request, reset e handoff;
7. filtros para destino local/global e Commanded Address;
8. aplicação relocada e testes de boundary/falha de energia.

CAN FD, ETP e secure boot não são meras trocas de port: alteram requisitos e
possivelmente o contrato on-wire. O G0 usa Flash dual-bank internamente, mas o
protocolo permanece *single-slot* e não oferece rollback.

## 16. Integração CubeMX

Ao regenerar um target, verificar:

- chamadas a `bootloader_init()`/`bootloader_task()` nas seções de usuário;
- source folders e include paths de `Application`, `Protocol`, `Portable` e
  `Config`;
- linker e header de target com partições iguais;
- pinos, clock do CAN/FDCAN e 500 kbit/s;
- filtros para data frames extended dirigidos ao SA local, ao destino global e
  ao PGN Commanded Address;
- opção de garbage collection e relatório `.map`;
- vector table da aplicação no `BOOT_APPLICATION_ADDRESS` do target.
