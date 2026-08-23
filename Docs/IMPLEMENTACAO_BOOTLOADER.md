# Implementação técnica completa do CanLoader v2

| Campo | Valor |
|---|---|
| target de referência | STM32F103C8Tx / Cortex-M3 / Blue Pill |
| protocolo on-wire | CanLoader v2, exclusivamente |
| enlace | CAN clássico, CEFF de 29 bits, 500 kbit/s |
| estado da evidência | build e testes automatizados; campanha HIL ainda obrigatória |
| fonte de verdade | código, linkers, testes e pacote Python deste repositório |

## 1. Finalidade, versão e estado

Este documento registra como o bootloader foi construído, quais requisitos
dirigiram as decisões e quais garantias a implementação realmente oferece. Ele
cobre firmware embarcado, aplicação de referência, ferramenta host, protocolo,
persistência, build, testes e limitações.

O sistema atual é **v2-only**:

- `BOOT_PROTOCOL_VERSION=2` no target;
- `PROTOCOL_VERSION=2` no pacote Python;
- todos os frames de aplicação usam CAN clássico com ID estendido de 29 bits;
- não há parser, filtro, resposta ou fallback para os IDs standard do v1;
- toda aplicação atual deve ser linkada em `0x08005000`.

O contrato byte a byte completo está em [PROTOCOL.md](PROTOCOL.md). A mudança
incompatível e sua justificativa estão em
[MIGRATION_V1_TO_V2.md](MIGRATION_V1_TO_V2.md).

### 1.1 Declaração normativa

O v2 usa três categorias claramente separadas:

| Categoria | Elementos | Alegação permitida |
|---|---|---|
| J1939-21/J1939-81 | CEFF de 29 bits, PGN/PDU, NAME, Address Claim, Request, Commanded Address e TP clássico | mecanismos J1939 implementados; requer validação final contra edição licenciada |
| subset diagnóstico | layouts e estados selecionados de DM14/DM15/DM16 | baseado em material público histórico de J1939-73; não é conformidade integral atual |
| CanLoader Proprietary A | identidade do produto, manifest, sessão, CRC, finalização, execução e aborto | extensão proprietária documentada no PGN `0xEF00` |

Não houve certificação SAE. Os bytes 2..4 de DM15 carregam códigos internos
CanLoader e não devem ser chamados de EDCP/Error Indicator normativo.

### 1.2 Por que a versão 2 substitui, em vez de coexistir com, a versão 1

A escolha de uma versão única foi deliberada para o TCC II. Um firmware
dual-stack precisaria manter simultaneamente dois esquemas de ID, descoberta,
fragmentação, endereçamento, persistência e teste. Além do custo de Flash, isso
criaria uma ambiguidade importante: o target poderia ser identificado por um
`node_id` antigo em um fluxo e por NAME/SA em outro. O v2 elimina essa dupla
autoridade e torna observável, em toda operação, qual identidade está sendo
atualizada.

Esta tabela é histórica; os valores antigos abaixo não são aceitos pelo software
atual:

| Decisão | Implementação anterior | Implementação v2-only | Motivo |
|---|---|---|---|
| CAN-ID | standard de 11 bits | CEFF estendido de 29 bits | representar prioridade, PGN, SA e DA J1939 |
| identidade | `node_id` 1..127 | NAME de 64 bits | separar identidade estável da rota mutável |
| descoberta | broadcast proprietário | Request + Address Claimed + IDENTIFY | inventariar qualquer CA J1939 e depois reconhecer CanLoader |
| reendereçamento | comando de node | Commanded Address por NAME/BAM | atingir somente o NAME escolhido |
| transferência longa | fragmentação própria | TP RTS/CTS; BAM restrito | reutilizar controle de fluxo e sequenciamento J1939 |
| acesso à imagem | comandos proprietários | subset DM14/DM15/DM16 | modelar erase/write como operações de memória |
| ciclo de produto | misturado ao transporte | Proprietary A separado | manter manifest, sessão e boot fora do subset DM |
| aplicação | base `0x08004000` | base `0x08005000` | acomodar a pilha v2 e o journal persistente |

Não existe negociação automática. Um host antigo não atualiza o target v2,
um host v2 não tenta frames antigos, e a troca inicial do próprio bootloader é
feita por SWD/ST-LINK. A atualização por CAN continua limitada à partição da
aplicação.

## 2. Escopo funcional

O bootloader deve:

1. permanecer recuperável quando não existir aplicação válida;
2. descobrir e reivindicar endereço em uma rede J1939;
3. expor identidade NAME estável e UID físico;
4. permitir que o host escolha um único NAME entre vários dispositivos;
5. receber manifest, apagar e escrever somente a partição da aplicação;
6. detectar corrupção e vetores inválidos antes do jump;
7. não executar imagem parcial após reset/queda de energia;
8. permitir verificação e execução explícitas;
9. permitir que a aplicação solicite retorno remoto ao bootloader;
10. persistir preferred Source Address sem ser apagado por update;
11. funcionar por SLCAN no macOS e SocketCAN no Linux;
12. ser testável em C/Python sem hardware e validável em placa.

### 2.1 Fora do escopo desta revisão

- assinatura/secure boot, criptografia e autenticação do Tool;
- anti-rollback e política de versão da imagem;
- slot A/B ou resume persistente;
- CAN FD, ETP e payloads acima de 1785 bytes;
- READ remoto, DM18, EDCP ou seed-key;
- provisionamento remoto do NAME de produção;
- port funcional para outro MCU;
- certificação SAE ou validação física já concluída.

## 3. Requisitos de construção

### 3.1 Hardware e plataforma

| ID | Requisito | Implementação/verificação |
|---|---|---|
| HW-01 | Executar no STM32F103C8Tx com pelo menos 64 KiB Flash/20 KiB SRAM. | registrador de tamanho da Flash + linker/asserts |
| HW-02 | Usar CAN clássico a 500 kbit/s. | bxCAN normal; timing distinto porém equivalente no bootloader e app |
| HW-03 | Usar transceptor externo em PA11/PA12. | pinagem CubeMX e documentação elétrica |
| HW-04 | Sinalizar estados em PC13 ativo-baixo. | `boot_platform_led_set` e FSM |
| HW-05 | Programar Flash por unidade física de 2 bytes e apagar por 1 KiB. | port FPEC/HAL com alinhamento e readback |
| HW-06 | Operar sem heap e sem RTOS. | buffers/contexto estáticos e laço cooperativo |
| HW-07 | Usar CAN_H/CAN_L, GND comum e duas terminações de 120 Ω nas extremidades. | requisito de montagem/HIL |
| HW-08 | Instalar ou migrar o bootloader por SWD, com boot pela Flash principal. | protocolo não grava sua própria partição |

### 3.2 Boot e imagem

| ID | Requisito | Implementação/verificação |
|---|---|---|
| BOOT-01 | Com imagem válida, aguardar 1500 ms antes de autoboot. | `BOOT_WINDOW_MS` |
| BOOT-02 | Após pedido da aplicação, aguardar 10000 ms. | backup registers + `BOOT_REQUEST_WINDOW_MS` |
| BOOT-03 | Sem imagem válida, permanecer em serviço. | `WAIT_HOST`, sem reset loop |
| BOOT-04 | Validar metadata, tamanho, CRC, MSP e Reset Handler. | `boot_metadata`, `boot_image` |
| BOOT-05 | Relocar vector table e restaurar estado ARM no handoff. | `prepare_handoff` + rotina assembly |
| BOOT-06 | Tentar entregar ACK antes de jump/reset, sem bloquear indefinidamente. | atraso 50 ms; limite absoluto 250 ms |

### 3.3 Rede e identidade

| ID | Requisito | Implementação/verificação |
|---|---|---|
| NET-01 | Transmitir somente frames extended/data no perfil. | tipo explícito em `boot_can_frame_t` |
| NET-02 | Ter NAME determinístico e preferred SA persistente. | journal v2 e default derivado do UID96 |
| NET-03 | Executar Address Claim e aguardar 250 ms. | FSM de claim |
| NET-04 | Menor NAME vence conflito. | comparação uint64 numérica |
| NET-05 | Target AAC deve procurar fallback e poder anunciar Cannot Claim. | faixa `0x80..0xF7`, SA `0xFE` |
| NET-06 | Responder Request for Address Claimed. | PGN `0xEA00` global/local |
| NET-07 | Reendereçar por NAME sem afetar outro ECU. | Commanded Address/BAM |
| NET-08 | Host deve selecionar alvo por NAME e rota atual por SA. | AddressBook e identity lock |
| NET-09 | Nunca transmitir resposta enfileirada com SA que acabou de ser perdido. | limpeza da fila + abort dos mailboxes antes do novo filtro |

### 3.4 Transporte e protocolo

| ID | Requisito | Implementação/verificação |
|---|---|---|
| PROTO-01 | Usar PGN normalizado PDU1/PDU2 e transmitir com prioridade 6. | `boot_j1939.c`, `j1939.py` |
| PROTO-02 | Mensagens até 8 bytes podem ser diretas. | Proprietary A/DM14/DM15/DM16 curto |
| PROTO-03 | DM16 longo deve usar TP RTS/CTS dirigido. | receptor alvo + sender host |
| PROTO-04 | Commanded Address deve usar BAM global. | payload de 9 bytes/two DT |
| PROTO-05 | Limitar TP clássico a 1785 bytes. | encoder/decoder C e Python |
| PROTO-06 | Conceder no máximo 8 pacotes por CTS. | `BOOT_J1939_TP_CTS_WINDOW` |
| PROTO-07 | Expirar TP sem progresso em 1250 ms. | deadline renovável |
| PROTO-08 | Separar ciclo de imagem em Proprietary A. | PGN `0xEF00` |
| PROTO-09 | Usar DM14/15/16 sem inserir header de sessão em seus bytes. | key/seed `FFFF`; sessão no Proprietary A |
| PROTO-10 | Rejeitar frame malformado, origem/destino/tamanho/estado incorretos. | validação antes de mutação |

### 3.5 Atualização e Flash

| ID | Requisito | Implementação/verificação |
|---|---|---|
| UPD-01 | Aceitar imagem de 8 a 44032 bytes. | manifest e mapa v2 |
| UPD-02 | Uma única sessão mutável por target. | owner SA + session 1..255 |
| UPD-03 | Persistir `UPDATING` antes de apagar aplicação. | `boot_metadata_begin` |
| UPD-04 | ERASE expressa page count de 1024 B. | DM14 ERASE/profile |
| UPD-05 | WRITE é sequencial em blocos de 100 B; último menor. | `expected_write_length` |
| UPD-06 | Não permitir lacuna, overlap nem endereço fora da aplicação. | pointer exato + região abstrata |
| UPD-07 | Confirmar remontagem TP e depois resultado Flash separadamente. | EOM_ACK seguido de DM15 |
| UPD-08 | Fechar operação repetindo pointer/length. | DM14 Operation Completed |
| UPD-09 | Timeout global de 5000 ms sem atividade. | `service_update_timeout` |
| UPD-10 | FINALIZE só marca VALID após CRC/vetores e readback. | estado VERIFY/metadata |

### 3.6 Persistência e robustez

| ID | Requisito | Implementação/verificação |
|---|---|---|
| PERS-01 | Config incompleta/corrompida não pode virar válida. | CRC, inverso e commit final |
| PERS-02 | Reduzir erase em reendereçamentos sucessivos. | 32 slots append-only |
| PERS-03 | Metadata parcial nunca autoriza jump. | markers independentes monotônicos |
| PERS-04 | Erase/write devem ser verificados fisicamente. | readback F1 |
| PERS-05 | Update não apaga journal nem código. | regiões disjuntas |
| PERS-06 | Falha deve manter caminho de recuperação por CAN. | bootloader fora da região gravável |

### 3.7 Host, qualidade e documentação

| ID | Requisito | Implementação/verificação |
|---|---|---|
| HOST-01 | Python 3.10+ e `python-can`. | `pyproject.toml` |
| HOST-02 | Suportar SLCAN/macOS e SocketCAN/Linux. | `bus.py` e argumentos CLI |
| HOST-03 | Expor `ls/info/update/verify/run/reboot/set-address`. | `cli.py` |
| HOST-04 | Correlacionar versão, SA/DA, comando e sessão. | decoders/client |
| HOST-05 | Implementar retries idempotentes e TP.ABORT best-effort. | `client.py`, `transport.py` |
| QUAL-01 | Compilar com warnings e testes C `-Werror`. | Makefiles |
| QUAL-02 | Ter testes de wire format byte a byte em C e Python. | `Tests/c`, `Tests/python` |
| QUAL-03 | Registrar limitações normativas e segurança. | documentação dedicada |
| QUAL-04 | Não declarar hardware validado sem evidência. | plano de validação pendente |
| QUAL-05 | Arquivar build, size/map, logs e captura CAN da versão liberada. | checklist e matriz HIL A–N |

## 4. Arquitetura de software

O núcleo é dividido em quatro níveis:

```text
Core/main (HAL init + scheduler cooperativo)
                 |
Application (FSM, metadata, imagem, configuração)
                 |
Protocol (J1939, claim, TP, DM/Proprietary A, CRC)
                 |
Portable interfaces -> port STM32F1
```

### 4.1 Módulos embarcados

| Arquivo/módulo | Responsabilidade |
|---|---|
| `Application/Src/bootloader.c` | contexto, FSM, comandos, ownership, DM, timeout e fila TX |
| `boot_device_config.c` | default NAME e journal NAME/SA |
| `boot_metadata.c` | serialização e transições persistentes da imagem |
| `boot_image.c` | CRC/vetores/validação/jump |
| `boot_j1939.c` | ID de 29 bits e PGN |
| `boot_j1939_address_claim.c` | gestão de endereço |
| `boot_j1939_tp.c` | receptor RTS/CTS e BAM |
| `boot_protocol.c` | payloads Proprietary A, DM14, DM15 e DM16 |
| `boot_crc32.c` | CRC-32/ISO-HDLC incremental |
| `boot_can_stm32f1.c` | filtro, RX/TX e estado bxCAN |
| `boot_flash_stm32f1.c` | limites, erase, halfword e verify |
| `boot_platform_stm32f1.c` | tick, UID, LED, request, reset e handoff |

Os contratos `boot_can.h`, `boot_flash.h` e `boot_platform.h` impedem que a FSM
dependa de tipos HAL. O diagrama detalhado está em
[ARCHITECTURE.md](ARCHITECTURE.md).

### 4.2 Estruturas estáticas

Não existe `malloc`. O contexto contém:

- fila circular TX de 8 frames;
- orçamento RX de 8 frames por iteração;
- buffer TP de 101 bytes para DM16 de 100 bytes;
- buffer de bloco e padding de halfword;
- buffer de 9 bytes para Commanded Address;
- FSM de claim e FSM TP;
- manifest/timestamps/owner/operação atual e anterior.

O build integrado medido possui `bss=1648` bytes; heap é zero e o linker
reserva 1024 bytes de stack.

### 4.3 Inventário da mudança v2 no repositório

| Área | O que foi alterado | Por que foi alterado |
|---|---|---|
| `Config/boot_config.h` | versão 2, SA/timers, bloco 100 e mapa 20 KiB | centralizar invariantes compiláveis do novo perfil |
| `Config/boot_device_config_format.h` | journal de NAME/preferred SA | substituir identidade por node e tolerar registros rasgados |
| `Portable/Inc/boot_can.h` | ID com 29 bits e tipo standard/extended explícito | impedir truncamento silencioso para 11 bits |
| port bxCAN | TX extended e filtros PS local/global com IDE/RTR | receber somente o tráfego candidato ao perfil |
| `boot_j1939.*` | encoder/decoder PDU1/PDU2 e PGNs | manter aritmética de ID fora da FSM |
| `boot_j1939_address_claim.*` | NAME, Request, disputa, fallback e Cannot Claim | separar identidade de endereço operacional |
| `boot_j1939_tp.*` | receiver RTS/CTS e BAM restrito | remontar payloads acima de 8 bytes com controle de fluxo |
| `boot_protocol.*` | Proprietary A e codecs DM14/15/16 | separar mensagens do produto de transporte/rede |
| `bootloader.c` | ownership por SA/session, operações DM, TP e fila TX | coordenar update dirigido e retries sem gravar bloco parcial |
| metadata | grava protocol version 2 e continua com markers monotônicos | recusar imagem/registro de contrato incompatível |
| linkers | código 19 KiB, config 1 KiB e app em `0x08005000` | acomodar a pilha v2 sem sobrepor persistência/aplicação |
| TestApplication | claim, IDENTIFY/INFO/ENTER e Commanded Address | provar o contrato que a aplicação real deverá cumprir |
| pacote Python | `j1939`, `address_claim`, `transport`, scanner e client v2 | operar SLCAN/SocketCAN e bloquear o target por NAME |
| testes | vetores EFF/PGN, claim, TP, DM, journal e fluxo integrado | substituir evidência do protocolo antigo por evidência v2 |

O código antigo não é compilado condicionalmente e não existe chave de
compatibilidade. A rastreabilidade histórica permanece no controle de versão e
em [MIGRATION_V1_TO_V2.md](MIGRATION_V1_TO_V2.md), não no binário.

## 5. Configuração concreta do STM32F103

### 5.1 Clock do bootloader

- HSE = 8 MHz;
- PLL ×9;
- SYSCLK/HCLK = 72 MHz;
- PCLK1 = 36 MHz;
- Flash latency = 2;
- SysTick/HAL tick = 1 ms.

### 5.2 CAN do bootloader

```text
bitrate = PCLK1 / (prescaler * (1 + BS1 + BS2))
        = 36 MHz / (4 * (1 + 13 + 4))
        = 500 kbit/s
sample point = (1 + 13) / 18 = 77,78 %
SJW = 1 TQ
```

Modo normal, AutoBusOff e retransmissão automática ficam habilitados. PA11 é
RX e PA12 TX; o transceptor externo converte níveis lógicos para diferencial.

A aplicação de referência retorna o clock a HSI 8 MHz e usa prescaler 1,
BS1=13, BS2=2: `8 MHz/(1*16)=500 kbit/s` e sample point 87,5%.

### 5.3 Filtros bxCAN

O port do bootloader configura dois bancos mask de 32 bits:

- destino no byte PS igual ao SA local;
- destino global `0xFF`.

A máscara inclui IDE e RTR, portanto aceita apenas extended data frames. Como o
mesmo byte PS representa DA nos PGNs PDU1 e Group Extension em PDU2, o filtro é
intencionalmente amplo; a camada J1939 faz a validação semântica de PGN/DA.
Quando o claim muda o SA, o bootloader primeiro limpa sua fila software, chama
`boot_can_abort_pending_tx()` para abortar os três mailboxes bxCAN e só então
reconfigura o filtro local. Se o abort/reconfigure falhar, entra em ERROR. A
ordem impede que um frame já preparado continue no barramento com o SA que o
target acabou de perder.

A aplicação de teste aceita todos os extended data frames em um banco e filtra
em software, simplificando seu driver sem HAL. Ao trocar de SA, ela limpa a
fila e solicita `ABRQ0` no único mailbox que utiliza.

### 5.4 Integração elétrica e gravação inicial

PA11/PA12 são sinais lógicos de CAN e **não** podem ser ligados diretamente a
CAN_H/CAN_L. A placa precisa de um transceptor compatível com os níveis do MCU,
alimentação e modo de standby corretamente definidos. CAN_H, CAN_L e uma
referência de GND devem acompanhar toda a bancada. A topologia linear recebe
120 Ω em cada extremidade física; com a rede desligada, duas terminações em
paralelo medem aproximadamente 60 Ω entre CAN_H e CAN_L. Stubs longos,
terminações adicionais e ausência de terra de referência devem ser tratados como
falha de montagem, não de protocolo.

O bootloader não faz self-update. Sua primeira instalação, assim como a migração
de uma imagem antiga, exige SWD/ST-LINK em `0x08000000`, com BOOT0 configurado
para iniciar pela Flash principal. Para a migração v2 é recomendado apagar toda
a Flash antes de gravar o bootloader, pois nem a aplicação relocada nem o formato
persistente antigo são compatíveis. Depois dessa instalação, somente a partição
da aplicação é atualizada pelo CAN.

## 6. Mapa de memória e orçamento

| Região | Intervalo | Tamanho |
|---|---:|---:|
| bootloader code | `0x08000000..0x08004BFF` | 19 KiB |
| device config | `0x08004C00..0x08004FFF` | 1 KiB |
| aplicação | `0x08005000..0x0800FBFF` | 43 KiB / `0xAC00` |
| metadata | `0x0800FC00..0x0800FFFF` | 1 KiB |
| SRAM | `0x20000000..0x20004FFF` | 20 KiB |

### 6.1 Justificativa histórica e medição atual

O primeiro link integrado do v2 não cabia nos 15 KiB de código anteriores e
reportou excesso de 516 bytes. O mapa passou a reservar 20 KiB totais ao
bootloader, dos quais 19 KiB para código e 1 KiB para configuração. A medição
do artefato final auditado é:

```text
text=16456, data=12, bss=1648
ocupação Flash text+data=16468
capacidade code=19456
margem=2988 bytes (aprox. 15,4 %)
```

É uma fotografia do build, não promessa futura. Cada release precisa repetir
`size`, conferir `.map` e as asserts do linker. Detalhes completos estão em
[MEMORY_MAP.md](MEMORY_MAP.md).

A aplicação de referência do mesmo build mede `text=7316`, `data=4` e
`bss=1296`: 7320 bytes carregáveis na partição de 44032 bytes e 1300 bytes de
RAM estática contabilizada, incluindo sua reserva de stack.

## 7. Configuração persistente NAME/SA

### 7.1 Default de desenvolvimento

O firmware lê as três words UID do STM32, serializa 12 bytes little-endian e
calcula CRC-32. O Identity Number é:

```text
identity = CRC32(uid96) & 0x001FFFFF
identity = 1, se o resultado for zero
```

O NAME combina identity com Manufacturer Code, ECU/Function instances,
Function, Vehicle System, Industry Group todos zero e `AAC=1`. Preferred SA é
`0x80`.

Manufacturer Code zero não é válido como identidade de produção atribuída; o
default só serve para bancada. O campo flag `NAME_PROVISIONED` existe no formato
persistente, embora o fluxo remoto desta revisão só altere o endereço.

A derivação é determinística, mas não garante unicidade global: 96 bits do UID
são comprimidos para os 21 bits de Identity Number. Portanto, colisão continua
matematicamente possível e deve ser verificada por inventário. Um produto deve
usar Manufacturer Code atribuído e Identity Number controlado pelo fabricante,
e persistir esse NAME antes da liberação.

### 7.2 Journal v2

A página guarda 32 slots de 32 bytes:

| Offset | Campo |
|---:|---|
| 0 | magic `0x46434C43` (`CLCF`) |
| 4 | format version 2 |
| 6 | record size 32 |
| 8 | generation uint32 |
| 12 | NAME uint64 LE |
| 20 | preferred SA |
| 21 | complemento do SA |
| 22 | flags uint16 |
| 24 | CRC32 dos bytes 0..23 |
| 28 | commit `0000`, programado por último |
| 30 | reservado `FFFF` |

O scanner percorre todos os slots, ignora registros inválidos/rasgados e escolhe
a geração modular mais nova. Se a página enche, ela é apagada e o próximo
registro vai ao slot zero. Após gravar, o código relê e compara generation,
NAME, SA e flags.

Commanded Address persiste primeiro; somente após sucesso o target carrega o
registro, reconfigura CAN e reinicia claim. Uma queda durante compactação pode
eliminar o journal inteiro e provocar fallback aos defaults, limitação de página
única documentada.

## 8. Metadata transacional da imagem

O registro de 30 bytes contém:

| Offset | Campo |
|---:|---|
| 0 | magic `0x444D4C43` (`CLMD`) |
| 4 | format version 1 |
| 6 | protocol version 2 |
| 8 | image size |
| 12 | image CRC32 |
| 16 | image version, atualmente 0 |
| 20 | CRC32 dos bytes 0..19 |
| 24 | marker update |
| 26 | marker valid |
| 28 | marker invalid |

Markers apagados são `FFFF`; programados são `0000`. A interpretação é:

```text
update=0000, valid=FFFF, invalid=FFFF -> UPDATING
update=0000, valid=0000, invalid=FFFF -> VALID
invalid=0000                         -> INVALID
qualquer cabeçalho/combinação inválida -> não executável
```

`boot_metadata_begin()` apaga a página, grava manifest + marker update e relê
antes de a aplicação ser apagada. `mark_valid()` só programa bits de 1 para 0
após CRC/vetores. `mark_invalid()` tem precedência e também é monotônico.

### 8.1 Raciocínio de segurança contra queda de energia

Flash NOR permite programar bits de 1 para 0 sem erase. Por isso os markers
mudam monotonicamente e o estado executável não depende de reescrever um enum:

| Momento da queda | Estado esperado no próximo boot |
|---|---|
| durante erase/gravação do cabeçalho | magic/CRC/marker inválido; não executa |
| depois de `UPDATING`, antes do erase da app | app pode existir fisicamente, mas não executa |
| durante erase ou qualquer WRITE | `UPDATING`; não executa |
| depois de todos os blocos, antes de FINALIZE | `UPDATING`; não executa |
| durante CRC/vetores ou programação de VALID | sem marker VALID completo; não executa |
| depois de VALID completo | boot relê cabeçalho, CRC, MSP e handler antes do jump |

Essa ordem privilegia integridade em vez de disponibilidade: o início de uma
atualização revoga imediatamente a autorização da imagem anterior, ainda que
seus bytes não tenham sido apagados. Como existe um único slot, a recuperação é
um novo update completo.

## 9. Identificador J1939 e PGNs

O ID contém prioridade, R, DP, PF, PS e SA. Para PDU1 (`PF<0xF0`), PS é DA e
não integra o PGN; para PDU2, PS é Group Extension. O encoder exige R=0,
PGN canônico de até `0x1FFFF` e transmite com prioridade 6. O decoder expõe a
prioridade recebida, mas o dispatcher atual não rejeita um PGN conhecido apenas
por ter prioridade diferente; a interoperabilidade deve usar 6, e uma política
RX mais restritiva pode ser acrescentada se o produto exigir.

| PGN | Nome | Uso |
|---:|---|---|
| `0xD700` | DM16 | dados de WRITE |
| `0xD800` | DM15 | resposta de memória |
| `0xD900` | DM14 | pedido de memória |
| `0xEA00` | Request | solicitar Address Claimed |
| `0xEB00` | TP.DT | pacotes de dados |
| `0xEC00` | TP.CM | RTS/CTS/EOM/BAM/ABORT |
| `0xEE00` | Address Claimed | NAME↔SA / Cannot Claim |
| `0xEF00` | Proprietary A | controle CanLoader |
| `0xFED8` | Commanded Address | alterar preferred SA |

Com Tool `0xF9` e target `0x80`, DM14 usa `0x18D980F9` e resposta DM15
`0x18D8F980`. Todos os exemplos e layouts estão em PROTOCOL.md.

## 10. NAME, Address Claim e descoberta

### 10.1 Identidade, rota e tipo de target

O v2 evita tratar um único byte como se fosse identidade permanente. Os campos
têm papéis distintos:

| Campo | Persistência | Função | É autenticação? |
|---|---|---|---|
| UID96 STM32 | silício do MCU | inventário físico e origem do default NAME | não |
| NAME64 | journal/default determinístico | identidade usada em claim e seleção | não |
| preferred SA | journal | primeira rota tentada após reset | não |
| current SA | RAM/claim | endereço efetivo usado nos IDs PDU1 | não |
| target code `0xF1` | constante de build | identifica a família STM32F1 nas capabilities | não |
| session | RAM, 1 byte | correlaciona um update até reset/abort/timeout | não |

O identificador recomendado ao operador é o NAME. O SA é necessário para
rotear o frame atual; o UID é informação adicional apresentada por `info`. O
host confere NAME↔SA antes e durante a operação, mas nenhuma dessas conferências
constitui prova criptográfica.

### 10.2 FSM de Address Claim

O NAME de 64 bits é enviado little-endian. Na disputa, o menor valor numérico
vence. O target segue:

```text
load NAME/preferred SA
  -> send Address Claimed
  -> wait 250 ms
       | sem conflito -> CLAIMED/operacional
       | perde        -> próximo 0x80..0xF7
       | esgotou      -> espera 0..153 ms, Cannot Claim com SA 0xFE
```

Se recebe NAME maior no mesmo SA, o target reanuncia seu claim e reinicia a
janela. NAME igual é tratado como repetição. Request do PGN `0xEE00`, global ou
local, provoca nova resposta.

Se perde o SA durante uma atualização, o target invalida/limpa a sessão antes
de aceitar a nova rota. Respostas pendentes do SA anterior são descartadas tanto
da fila quanto dos mailboxes; continuar uma sessão atravessando a mudança seria
ambíguo para owner, DA e correlação do host.

### 10.3 Descoberta pelo host

O host também reivindica endereço. Seu default é SA `0xF9` e NAME constante de
laboratório `0xA000000000000001`; ambos são configuráveis. `canloader ls`:

1. completa o claim do Tool;
2. envia Request global;
3. coleta todos os NAME/SA, inclusive Cannot Claim;
4. preserva conflito em vez de sobrescrever um mapa por SA;
5. envia IDENTIFY individual a cada SA não contestado;
6. marca como CanLoader apenas resposta correlacionada com versão 2, target
   code `0xF1`, SA anunciado e flags J1939/Address Claim/TP RX.

Antes de operação mutável, `BootloaderClient` resolve o `--name` esperado no SA
e observa claims durante esperas. Mudança/takeover gera erro de identidade.
Se o próprio Tool perde o SA para um NAME menor, o cliente invalida a rota e o
cache de perfil, seleciona fallback AAC e executa novo claim/IDENTIFY antes de
qualquer comando posterior; uma sessão de update presa ao SA antigo não é
migrada e precisa recomeçar.

## 11. Commanded Address

O payload lógico é `NAME_LE64 || newSA`, nove bytes, no PGN global `0xFED8`.
Como excede oito bytes, o host envia BAM:

```text
CM: 20 09 00 02 FF D8 FE 00
DT1: 01 name0 name1 name2 name3 name4 name5 name6
DT2: 02 name7 newSA FF FF FF FF FF
```

O target autoriza BAM somente para esse PGN e capacidade de 9 bytes. Ele ignora
NAME diferente, SA `FE/FF`, falha de persistência e comando durante update. Não
existe ACK próprio. O host aguarda o mesmo NAME no novo SA por uma janela
completa de claim, exige ocupante único nesse endereço e conclui com IDENTIFY
dirigido. Se o NAME perder o endereço e migrar durante a arbitragem, o comando
falha em vez de reportar sucesso transitório. Em timeout, o orçamento
`--retries` retransmite o BAM inteiro; persistir novamente o mesmo preferred SA
é uma operação idempotente. O retry espera ao menos `transport_timeout` para
que um BAM parcialmente recebido expire no target antes da nova TP.CM.

## 12. Transport Protocol

### 12.1 Escopo do receiver embarcado

- mensagem de 9 a 1785 bytes;
- uma sessão ativa;
- RTS/CTS para a expectativa DM16 exata;
- BAM apenas para Commanded Address;
- janela CTS máxima 8;
- deadline 1250 ms renovado a cada TP.DT;
- EOM_ACK após remontagem completa;
- ABORT reason 1 (busy), 2 (resources/validação) ou 3 (timeout).

RTS só é aceito se source, destination, PGN, total size e packet count
coincidirem com a expectativa criada por DM14 WRITE. `sender-max` precisa ser
não zero e limita, junto da janela local 8 e dos pacotes restantes, cada CTS.
Cada DT tem sequence byte e sete bytes. O último copia apenas os bytes restantes.

Ao completar DM16, o receiver coloca EOM_ACK na fila e entrega evento à FSM; a
FSM então valida count, grava Flash e coloca DM15. Portanto EOM_ACK não garante
programação.

Frames de origem/destino/PGN alheios podem ser ignorados. Sequência errada
encerra a sessão e, no target atual, usa ABORT reason 2. Timeout em BAM não gera
ABORT por ser broadcast.

### 12.2 Sender host

O host envia RTS, espera CTS, respeita exatamente a janela e só conclui após
EOM_ACK correspondente em size/count/PGN. Em exceção, tenta TP.ABORT reason 250.
BAM não espera resposta e usa 50 ms após o BAM e antes de cada pacote DT,
inclusive o primeiro.

Para bloco de 100 bytes, DM16 lógico tem 101 bytes (`FF+data`), 15 DT e duas
janelas 8+7.

Os 100 bytes são uma escolha de produto, não o limite de TP. O valor é par e
portanto alinha os blocos intermediários a halfword; limita o buffer estático a
101 bytes; mantém retry restrito a uma unidade pequena; e ainda exercita mais de
uma janela CTS. O último bloco pode ser menor e ímpar, recebendo padding `FF`
somente para a programação física.

## 13. Proprietary A

DM14/15/16 descrevem uma operação de memória, mas não representam conceitos
do produto como tamanho total da imagem, CRC esperado, modo aplicação,
finalização ou handoff. Esses conceitos permanecem no PGN destination-specific
Proprietary A `0xEF00`. Isso evita atribuir semântica CanLoader a campos
padronizados que não a possuem e permite que o DM mantenha key/seed `FFFF`.

### 13.1 Request e response

```text
request : [02, command, session, parameter, valueLE32]
response: [02, ACK|NACK, echoed_command, echoed_session, valueLE32]
ACK=80, NACK=81
```

Frames são sempre PDU1 dirigidos. O decoder exige DLC 8, versão 2 e DA não
global. O perfil exige sessão `1..255`, inclusive para consultas; zero é
rejeitado. O cliente valida PGN, SA, DA, versão, comando e sessão antes de
associar uma resposta a uma requisição.

### 13.2 Comandos

| Código | Nome | Regras principais | ACK value |
|---:|---|---|---:|
| `01` | IDENTIFY | parameter/value zero; não abre update; session não zero | capabilities |
| `02` | INFO | value zero; selector válido; session não zero | dado solicitado |
| `10` | START_UPDATE | session não zero, size 8..44032 | block size 100 |
| `11` | SET_IMAGE_CRC | mesmo owner/session, PREPARE | CRC esperado |
| `30` | FINALIZE | todos bytes, sem DM/close, value=CRC | CRC calculado |
| `31` | VERIFY | parameter/value zero | CRC persistido |
| `32` | RUN_APPLICATION | imagem válida | CRC persistido |
| `33` | ENTER_BOOTLOADER | app: request/reset; bootloader: permanece | zero |
| `3F` | ABORT | owner quando update ativo | zero |

O enum C ainda chama o comando `0x30` de `FINISH`; a nomenclatura pública v2 é
FINALIZE.

### 13.3 INFO

| Selector | Valor |
|---:|---|
| 0 | capabilities |
| 1 | `0x08005000` |
| 2 | `0x0000AC00` / 44032 |
| 3 | tamanho da metadata legível ou zero |
| 4 | CRC da metadata legível ou zero |
| 5..7 | UID words 0..2 |
| 8..9 | NAME low/high |
| 10 | Source Address atual |

No bootloader, selectors 3/4 podem refletir um cabeçalho legível mesmo em estado
UPDATING/INVALID; eles não afirmam que a imagem é executável. O bit
`APPLICATION_VALID` e o comando VERIFY são as fontes para essa decisão. A
aplicação de referência só publica esses valores quando a metadata está VALID.

Capabilities:

```text
bit 0 application valid
bit 1 update active
bit 2 application running (setado pela aplicação; não pelo bootloader)
bit 3 J1939
bit 4 Address Claim
bit 5 TP RX
bits 15..8 current SA
bits 23..16 protocol version 2
bits 31..24 target code F1
```

### 13.4 NACK/boot_error

| Código | Significado |
|---:|---|
| 0 | none |
| 1 | invalid command |
| 2 | invalid state |
| 3 | invalid sequence |
| 4 | invalid address |
| 5 | invalid size |
| 6 | Flash erase |
| 7 | Flash write |
| 8 | CRC mismatch |
| 9 | timeout |
| 10 | unsupported version |
| 11 | invalid application |
| 12 | malformed frame |
| 13 | session/owner |
| 14 | CAN |
| 15 | internal |
| 16 | busy |
| 17 | unsupported operation |

O mesmo enum aparece no `value` de NACK e, truncado a 24 bits, no campo
diagnóstico proprietário de DM15.

## 14. DM14, DM15 e DM16

### 14.1 DM14

DM14 tem DLC 8:

```text
b0       length bits 7..0
b1[7:5] length bits 10..8
b1[4]   pointer type (deve ser 0/direct)
b1[3:1] command
b1[0]   reservado fixo 1
b2..5   pointer uint32 LE
b6..7   key/user level = FFFF
```

O decoder rejeita length acima de 1784, objeto indireto e key diferente no
dispatcher. Esse é o limite do codec; o updater desta Blue Pill aceita no
máximo 100 bytes por WRITE e, em ERASE, no máximo 43 páginas. O significado da
unidade é definido pela operação: páginas em ERASE, bytes em WRITE.

### 14.2 ERASE

`length=ceil(expected_size/1024)` páginas, pointer `0x08005000`. Após validar
manifest/CRC/estado, o target enfileira DM15 PROCEED. O erase só começa quando
essa resposta saiu da fila e os mailboxes estão ociosos:

1. metadata `UPDATING` é escrita e verificada;
2. exatamente `length*1024` bytes da aplicação são apagados;
3. erase é conferido;
4. estado muda para RECEIVE;
5. DM15 COMPLETED é enviado;
6. fica `close_pending` até DM14 command 4 com os mesmos pointer/length.

### 14.3 WRITE

Pointer precisa ser `APP_BASE+bytes_committed`. Length precisa ser
`min(100, expected_size-bytes_committed)`. Para 1..7 bytes, DM16 é direto com
count no byte 0. Para 8..100, a mensagem lógica TP começa em `FF`.

Somente após DM15 PROCEED o host envia DM16. O target programa halfwords; se o
último length for ímpar, acrescenta `FF` fora do CRC. Em sucesso, atualiza
`bytes_committed`, registra operação anterior, envia DM15 COMPLETED e exige
close com pointer/length idênticos.

### 14.4 DM15 proprietário

```text
b0/b1 length allowed + status; bits reservados b1.4 e b1.0 = 1
b2..4 boot_error CanLoader LE24
b5    00
b6..7 FFFF
```

Status aceitos: PROCEED 0, BUSY 1, COMPLETED 4, FAILED 5. Essa utilização de
bytes 2..5 é documentada como extensão, não EDCP SAE.

### 14.5 STATUS e close

STATUS do owner retorna:

- BUSY com length atual durante erase;
- PROCEED com length atual aguardando DM16;
- COMPLETED com length anterior quando close pendente;
- COMPLETED/zero quando não há operação.

Operation Completed repete pointer/length anterior e não recebe resposta de
sucesso; sua função é liberar `close_pending`. Operation Failed recebido limpa
a operação/TP best-effort sem DM15. Não há timer close de 100 ms: o timeout
global de 5 s cobre essa espera.

Como o close não tem ACK positivo, o cliente o transmite uma vez mais o número
configurado de retries. O target libera `close_pending` na primeira cópia e
trata duplicatas byte a byte equivalentes como idempotentes e silenciosas, para
não deixar um DM15 tardio contaminar a operação seguinte. Ainda não há consulta
dedicada que confirme o close; perder todas as cópias faz o próximo WRITE
receber INVALID_SEQUENCE.

### 14.6 Fronteira de proteção da Flash

O pointer de DM14 é absoluto por exigência do perfil, mas não constitui uma
permissão arbitrária de escrita. ERASE só aceita exatamente `APP_BASE` e o número
de páginas derivado do manifest. WRITE só aceita
`APP_BASE+bytes_committed` e o comprimento exato do bloco esperado. Depois
dessas validações, a FSM converte o acesso em offset da região
`BOOT_FLASH_REGION_APPLICATION`; o port não recebe endereço bruto do host.

A abstração de Flash mantém regiões independentes para aplicação, metadata e
device config, valida `offset <= size` antes de calcular o restante e exige:

- erase alinhado e múltiplo de 1024 bytes;
- write alinhado e múltiplo de halfword;
- célula apagada (`FFFF`) ou já igual ao valor pedido;
- leitura de volta de cada halfword programado;
- varredura de cada halfword apagado após erase;
- unlock apenas durante a operação e lock ao terminar.

Essas regras protegem bootloader/config/metadata contra um pointer de aplicação
malformado. Elas não substituem WRP/RDP, detecção de subtensão ou assinatura de
firmware.

## 15. Sessão, ownership e idempotência

`START_UPDATE` grava em RAM `owner_address`, `session`, `expected_size` e
timestamp. Session zero é inválida. Outro SA/session recebe SESSION ou BUSY.

Comportamentos repetíveis:

- START idêntico retorna novamente 100;
- SET_CRC idêntico ecoa o CRC; valor diferente é erro;
- ERASE atual duplicado retorna BUSY/PROCEED;
- ERASE duplicado, enquanto ainda é a última operação registrada, retorna COMPLETED;
- WRITE atual duplicado retorna PROCEED;
- WRITE duplicado, enquanto ainda é a última operação registrada, retorna COMPLETED;
- FINALIZE repetido em READY, mesmo owner/session/CRC, retorna CRC;
- frame de resposta atrasado só é aceito pelo host se toda correlação coincidir.

Uma repetição divergente não reinicia a operação. O host usa no máximo três
retransmissões além da primeira por default e nunca transforma EOM_ACK perdido
em confirmação de Flash.

## 16. Máquina de estados

### 16.1 Inicialização

`bootloader_init()` executa uma sequência fail-closed:

1. zera todo o contexto estático, liga o LED e entra em INIT;
2. lê `FLASHSIZE_BASE` e recusa MCU com menos de 64 KiB;
3. carrega o último journal NAME/SA válido ou gera os defaults;
4. inicializa FSMs de Address Claim e TP e autoriza apenas o BAM de Commanded
   Address;
5. consome, uma única vez, a assinatura de boot nos backup registers;
6. configura os filtros para preferred SA e global, inicia bxCAN e anuncia
   Address Claimed;
7. relê metadata, CRC e vetores da aplicação;
8. entra em BOOT_WINDOW se a imagem for válida ou WAIT_HOST caso contrário.

O Address Claim continua assincronamente no laço principal. A janela de boot
começa ao final da validação da imagem, não depois dos 250 ms de claim. Antes de
`CLAIMED`, o dispatcher trata gerenciamento de rede e TP permitido, mas não
aceita comandos de controle ou DM dirigidos.

### 16.2 Estados

| Estado | Invariante |
|---|---|
| INIT | ports ainda sendo preparados |
| BOOT_WINDOW | aplicação válida; nenhuma sessão de escrita necessária |
| WAIT_HOST | serviço disponível; app inválida ou retenção explícita |
| PREPARE_UPDATE | owner/size definidos; CRC/erase pendentes |
| ERASE | erase aceito e aguardando/realizando Flash |
| RECEIVE | metadata não executável; blocos sequenciais |
| VERIFY | finalização calcula CRC/vetores |
| READY | aplicação válida |
| RUN_APPLICATION | ACK em drenagem e jump pendente |
| RESET | estado reservado |
| ERROR | init de Flash/CAN/claim falhou |

IDENTIFY/INFO não retêm por si só um autoboot além de seu processamento. START
válido durante BOOT_WINDOW move para PREPARE. ENTER no bootloader move para
WAIT_HOST. Uma mudança de SA durante update invalida a sessão para evitar que o
owner/rota fiquem incoerentes.

### 16.3 Iteração cooperativa

Cada `bootloader_task()` atualiza LED, claim e TP, tenta drenar a fila TX e
processa no máximo oito frames RX. Em seguida repete os serviços temporais,
executa erase pendente somente depois de DM15 PROCEED ter saído, avalia timeout
de update e decide jump/reset. O limite RX evita que tráfego contínuo impeça
timers e transmissões. Erase, programação e cálculo de CRC ainda são síncronos;
por isso sua latência real precisa ser medida sob carga de barramento.

## 17. Fluxo completo de atualização

```text
scan/claim e identity lock
  -> INFO(app base/max)
  -> se application_running: ENTER_BOOTLOADER e aguardar reclaim
  -> START_UPDATE(size, session)
  -> SET_IMAGE_CRC(crc, session)
  -> DM14 ERASE(page count)
       <- DM15 PROCEED
       <- DM15 COMPLETED
       -> close ERASE
  -> para cada bloco
       DM14 WRITE(pointer,length)
       <- DM15 PROCEED
       DM16 direto ou TP RTS/CTS
       <- EOM_ACK, se TP
       <- DM15 COMPLETED
       -> close WRITE
  -> FINALIZE(value=expected CRC)
  -> VERIFY
  -> RUN opcional
```

O host calcula CRC-32/ISO-HDLC com `zlib.crc32`, confere tamanho contra INFO e
acompanha progresso somente após commit de blocos.

## 18. CRC e validação da aplicação

CRC usado:

```text
CRC-32/ISO-HDLC (IEEE)
poly normal 0x04C11DB7 / refletido 0xEDB88320
init 0xFFFFFFFF
refin/refout true
xorout 0xFFFFFFFF
check("123456789") = 0xCBF43926
```

A imagem válida exige:

- metadata legível e estado VALID;
- tamanho `8..44032`;
- CRC exato de `[APP_BASE, APP_BASE+image_size)`;
- MSP `>0x20000000`, `<=0x20005000`, alinhado a 8;
- Reset Handler com Thumb bit;
- endereço do handler sem Thumb dentro da imagem declarada.

Não são avaliadas assinatura, versão de hardware, demais vetores ou semântica
da aplicação.

## 19. Handoff

Antes do salto:

1. valida a imagem novamente;
2. para/desinicializa CAN;
3. desinicializa RCC/HAL;
4. desabilita IRQs;
5. zera SysTick;
6. desabilita/limpa todas as IRQs NVIC;
7. limpa PendST/PendSV;
8. executa DSB/ISB;
9. grava `SCB->VTOR=0x08005000`;
10. zera CONTROL/BASEPRI/FAULTMASK/PRIMASK;
11. carrega MSP;
12. salta para Reset Handler.

A aplicação de teste para deliberadamente com LED aceso se observar PRIMASK
mascarado, tornando regressão de handoff visível.

## 20. Contrato da aplicação

Toda aplicação atualizável precisa:

- ser linkada em `0x08005000` e caber antes de `0x0800FC00`;
- colocar `.isr_vector` exatamente na origem;
- configurar VTOR no startup;
- não apagar device config/metadata;
- tolerar estado de periféricos documentado ou reinicializá-los;
- usar o mesmo NAME/preferred SA se quiser descoberta contínua;
- implementar Address Claim/Request;
- responder IDENTIFY/INFO com `APPLICATION_RUNNING`;
- aceitar ENTER_BOOTLOADER dirigido;
- gravar `BKP->DR1=0xB007`, `BKP->DR2=0x4C44`;
- entregar ACK quando possível e resetar em até 250 ms.

A aplicação de referência também aceita Commanded Address por BAM e persiste o
SA. Ela não aceita DM14/15/16 nem update enquanto roda.

O bootloader consome a assinatura uma única vez e a apaga. Reset comum sem a
assinatura abre apenas 1500 ms; reset remoto abre 10000 ms.

### 20.1 Por que usar backup registers

`ENTER_BOOTLOADER` é recebido enquanto a aplicação controla CAN, mas quem deve
alterar Flash é o bootloader após um reset completo. Os registradores
`BKP->DR1/DR2` preservam a intenção através de `NVIC_SystemReset()` sem depender
de RAM `.noinit`, valor de periférico ou frame que precisaria sobreviver ao
reset. Duas magias reduzem aceitação acidental; o consumo one-shot evita que
resets seguintes mantenham para sempre a janela longa. Isso é sinalização, não
autenticação: qualquer Tool capaz de enviar o comando dirigido pode solicitá-la.

## 21. Ferramenta host

### 21.1 Componentes

| Módulo Python | Função |
|---|---|
| `bus.py` | adapta `python-can`, preserva extended IDs |
| `j1939.py` | ID/PGN/NAME e validações |
| `address_claim.py` | claim do Tool, AddressBook e conflitos |
| `transport.py` | TP encoders/sender RTS/CTS/BAM |
| `protocol.py` | Proprietary A e DM codecs |
| `client.py` | scanner, lock de identidade, update/retry |
| `cli.py` | subcomandos e argumentos |

### 21.2 Instalação

```sh
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install -e 'Tools/canloader[test]'
```

Python 3.9 do Xcode não satisfaz `requires-python >=3.10`. Uma criação de venv
interrompida pode deixar diretório sem `bin/activate`; recrie o ambiente.

### 21.3 Comandos

```text
ls            inventário de claims e IDENTIFY
info          capabilities/mapa/metadata/UID/NAME/SA
update FILE   update completo, `--run` opcional
verify        valida imagem persistida
run           solicita handoff
reboot        aplicação -> bootloader
set-address   Commanded Address e confirmação por claim
```

Opções principais: `--interface`, `--channel`, `--bitrate`, `--tty-baudrate`,
`--address`, `--name`, `--source-address`, `--source-name`, `--timeout`,
`--transport-timeout`, `--erase-timeout`, `--claim-timeout`, `--scan-timeout`,
`--retries`, `--verbose`. O default de `--transport-timeout` é 1,5 s.

### 21.4 macOS/SLCAN

No macOS, conecte o adaptador e compare a lista antes/depois:

```sh
ls -1 /dev/cu.*
system_profiler SPUSBDataType
```

Prefira a entrada `/dev/cu.*` correspondente ao adaptador; ela é o canal
serial passado ao backend, não uma interface de rede `can0`. Um nome típico é
`/dev/cu.usbmodem2074318558421`. Primeiro valide descoberta e identidade:

```sh
python -m canloader ls --interface slcan \
  --channel /dev/cu.usbmodem2074318558421 \
  --bitrate 500000 --verbose
```

Depois use o NAME e o SA retornados para bloquear o alvo:

```sh
python -m canloader info --interface slcan \
  --channel /dev/cu.usbmodem2074318558421 --bitrate 500000 \
  --address 0x80 --name 0x8000000000123456

python -m canloader update TestApplication/build/canloader_test_app.bin \
  --interface slcan --channel /dev/cu.usbmodem2074318558421 \
  --bitrate 500000 --address 0x80 --name 0x8000000000123456 \
  --run --verbose
```

Substitua o NAME do exemplo pelo inventariado. Os backends seriais recebem
`bitrate` quando `--interface slcan` ou `--interface seeedstudio`; neste último,
a camada também força `frame_type=EXT`. `--tty-baudrate` é opcional e seleciona
a velocidade da UART do adaptador, diferente dos 500 kbit/s do CAN. A CLI v2 não
lê `CAN_BITRATE` do ambiente: use `--bitrate`. Na serial Lawicel, frames de 29
bits devem aparecer com o prefixo maiúsculo `T`; adaptador/firmware que aceite
somente `t` de 11 bits é incompatível.

### 21.5 Linux/SocketCAN

O operador configura `can0` fora da CLI. A implementação usa CAN raw em user
space; não deve compartilhar NAME/SA com sockets CAN_J1939 do kernel. Exemplo
de preparação, deliberadamente externo ao CanLoader:

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
python -m canloader ls --interface socketcan --channel can0
```

## 22. Atualização de apenas um dispositivo

Em uma rede com vários bootloaders, desde que os NAMEs sejam distintos:

1. cada MCU anuncia seu NAME;
2. preferred SA pode coincidir inicialmente;
3. Address Claim resolve o conflito e atribui rotas diferentes;
4. `ls` apresenta NAME↔SA;
5. o operador passa ambos `--name` e `--address`;
6. o host recusa se a associação mudou/está contestada;
7. DM14/DM16 são PDU1 dirigidos;
8. os outros targets ignoram DA diferente;
9. progresso/CRC final são atribuídos ao NAME bloqueado.

Essa propriedade depende de NAMEs únicos. Dois dispositivos com o mesmo NAME
são erro de provisionamento e não podem ser selecionados com segurança. O NAME
derivado do UID reduz colisões na bancada, mas, por usar apenas 21 bits do
resultado, não substitui provisionamento de produção.

## 23. Timeouts e progresso

| Função | Valor | Natureza |
|---|---:|---|
| Address Claim | 250 ms | J1939/network management |
| Cannot Claim | 0..153 ms | implementação target |
| TP target | 1250 ms | perfil target |
| intervalo BAM → DT e entre DT no host | 50 ms | configuração host |
| update/DM close | 5000 ms | CanLoader proprietário |
| boot normal | 1500 ms | produto |
| boot solicitado | 10000 ms | produto |
| resposta antes de ação | 50 ms | produto |
| ação forçada | 250 ms | produto |

`last_activity_ms` é atualizado por comandos/DM válidos e commits. Ao expirar
update, o target invalida se necessário, limpa TP/operação e envia NACK
assíncrono de ABORT/TIMEOUT ao owner.

## 24. Tratamento de falhas

| Falha | Reação |
|---|---|
| Flash detectada <64 KiB | estado ERROR |
| CAN/claim não inicia | estado ERROR |
| bus-off em runtime | bxCAN usa ABOM; serviço avançado/telemetria não implementados |
| frame standard/RTR | rejeitado no port/decoder |
| versão diferente de 2 | NACK unsupported version quando possível |
| NAME perde SA | fallback; update ativo é invalidado |
| sem SA | Cannot Claim, sem operação dirigida |
| owner/session errado | DM15/NACK session |
| erase fora do manifest | DM15 failed |
| RTS inesperado/tamanho errado | TP.ABORT resources |
| sequência TP errada | ABORT/falha DM |
| timeout TP | ABORT timeout + DM15 failed |
| write Flash falha | metadata invalid, WAIT_HOST, DM15 failed |
| CRC/vetor falha | metadata invalid, NACK, WAIT_HOST |
| timeout update | invalid/clear + NACK timeout |
| ACK não drena em 250 ms | jump/reset ainda ocorre |
| queda de energia | metadata não VALID; requer reenvio integral da imagem |
| config rasgada | slot ignorado; geração válida anterior/default |

O sistema prioriza não executar conteúdo parcial. Ele não promete preservar a
imagem anterior.

## 25. Build e artefatos

O build requer GNU Make e GNU Arm Embedded Toolchain. O Makefile procura
automaticamente o toolchain incluído no STM32CubeIDE no macOS e, se não o
encontrar, usa `arm-none-eabi-*` do `PATH`; `STM32_GNU_BIN` ou
`TOOLCHAIN_PREFIX` podem sobrescrever essa descoberta. Os testes host requerem
Python 3.10 ou mais novo com o extra `test` instalado no ambiente virtual.

```sh
make bootloader
make test-application
make test-c
make test-python PYTHON=.venv/bin/python
make test PYTHON=.venv/bin/python
```

Artefatos:

```text
build/bootloader/CanLoader.elf
build/bootloader/CanLoader.bin
build/bootloader/CanLoader.hex
build/bootloader/CanLoader.map
build/bootloader/CanLoader.list
TestApplication/build/canloader_test_app.{elf,bin,hex,map}
```

Flags centrais: Cortex-M3/Thumb/soft-float, GNU C11, `-Os`, function/data sections,
`--gc-sections`, nano/newlib e `-Wall -Wextra`. Testes C usam `-Wpedantic
-Werror`; aplicação de teste é freestanding/nostdlib.

O linker faz asserts de origem/fim e reserva stack. A configuração CubeIDE deve
usar os mesmos sources/includes/linker; regeneração CubeMX exige revisão das
seções USER CODE.

### 25.1 Primeira colocação em serviço

Uma sequência segura e reproduzível é:

1. registrar commit, versão do compilador e resultado de `size`;
2. apagar a Flash completa por SWD e gravar `CanLoader.bin` em `0x08000000`;
3. conferir BOOT0 baixo, alimentação, transceptor, GND e terminação;
4. energizar sem aplicação e confirmar que o target permanece em WAIT_HOST;
5. executar `canloader ls` e arquivar NAME/SA observado;
6. executar `info` e exigir protocolo 2, base `0x08005000`, máximo 44032 e UID
   esperado;
7. atualizar o binário relocável da aplicação, verificar CRC e executar;
8. na aplicação, repetir `ls/info`, chamar `reboot` e confirmar o mesmo NAME no
   bootloader;
9. manter o ST-LINK conectado/disponível até concluir os ensaios de recuperação.

O arquivo enviado deve ser o `.bin` bruto da aplicação, não o ELF/HEX e nunca
o binário do bootloader. Antes do envio, `arm-none-eabi-size` e o `.map` da
aplicação devem provar que a imagem inicia em `0x08005000` e termina antes da
metadata.

## 26. Testes automatizados

Seis executáveis C cobrem:

- CRC, endianess, IDs e payloads;
- Address Claim/Request/arbitragem/Cannot Claim;
- TP duas janelas, sequência, timeout, frames alheios e BAM;
- defaults/journal/registro rasgado/compactação;
- FSM, ownership e bloco TP completo;
- metadata, markers, bounds, CRC e vetores.

A suíte Python cobre:

- PDU1/PDU2, NAME e frames extended;
- claim do Tool e AddressBook com conflitos;
- codecs Proprietary A/DM;
- RTS/CTS/BAM/ABORT;
- scanner e identity lock;
- update completo, bloco curto e retry por EOM perdido;
- reboot da aplicação e Commanded Address;
- CLI v2 e ausência das opções de endereçamento legadas;
- backends SocketCAN/SLCAN.

Mocks não validam física, arbitragem real, bus-off, energia ou endurance. A
campanha em hardware está em [VALIDATION.md](VALIDATION.md).

### 26.1 Matriz mínima de validação HIL

Os testes abaixo devem gerar log `--verbose`, captura CAN com IDs de 29 bits e
resultado observado. "Aprovado" só pode ser marcado depois da execução física:

| ID | Ensaio | Critério de aceitação |
|---|---|---|
| A | power-on sem metadata válida | permanece descobrível; não salta |
| B | power-on com imagem válida | claim, janela de 1,5 s e handoff correto |
| C | Request/Address Claimed | `ls` associa NAME, SA e IDENTIFY v2 |
| D | dois targets preferindo `0x80` | menor NAME vence; outro escolhe fallback |
| E | Commanded Address + power cycle | mesmo NAME reaparece no SA persistido |
| F | update com dois ECUs ativos | somente o NAME/DA bloqueado altera Flash |
| G | blocos finais de 1..7 e 8 bytes | DM16 direto e transição para TP corretos |
| H | bloco de 100 bytes | CTS em janelas 8+7; EOM antes de DM15 |
| I | perda/duplicação de ACK, DM15 e EOM | retries documentados convergem sem commit duplicado |
| J | TP fora de ordem ou incompleto | ABORT/FAILED e nenhuma gravação parcial do bloco |
| K | CRC incorreto e vetores inválidos | metadata não fica VALID; jump recusado |
| L | corte de energia antes/durante/depois de erase/write/finalize | nunca executa imagem parcial; aceita update integral novo |
| M | ENTER_BOOTLOADER pela aplicação | ACK, reset, assinatura one-shot e janela de 10 s |
| N | carga, desconexão e bus-off | comportamento e tempo de recuperação registrados |

Para o caso F, a evidência precisa registrar os dois NAMEs, seus SAs antes e
depois, CRC/versão de ambas as aplicações e todo PGN dirigido. Para o caso L,
o corte deve variar dentro da operação; desligar apenas entre comandos não testa
o compromisso transacional de metadata/journal. A perda de todas as cópias do
close DM14 deve ser ensaiada separadamente e confirmar a limitação conhecida:
INVALID_SEQUENCE e reinício do update, sem execução de imagem parcial.

## 27. Segurança e limitações

Proteções presentes:

- partições rígidas;
- validação de overflow/alinhamento;
- ownership/sessão não criptográficos;
- CRC e readback;
- metadata fail-safe contra execução parcial;
- vetores e Thumb/MSP;
- update dirigido.

Ausentes: assinatura, autenticação, confidencialidade, anti-replay,
anti-rollback, slot redundante, WRP/RDP gerenciados, watchdog/brown-out
qualificados e NAME de produção. Veja [LIMITATIONS.md](LIMITATIONS.md).

O identity lock do host evita erro operacional por troca de SA ou disputa
observada, mas não autentica criptograficamente o target. No firmware, o owner
da sessão é apenas `(Source Address, session byte)`; NAME do Tool não integra a
autorização. Qualquer participante com acesso ao barramento pode forjar esses
campos, enviar Commanded Address ou instalar uma imagem com CRC recalculado.
Assim, o estado atual é apropriado para bancada/rede confiável, não para uma
fronteira de segurança de produção.

## 28. Portabilidade

Um port futuro precisa definir:

- MCU/clock/startup/HAL/CMSIS;
- mapa e linkers da aplicação/metadata/config;
- geometria real de Flash;
- driver CAN/FDCAN no modo clássico de 29 bits;
- filtros para DA local/global;
- tick, UID, persistência, backup request e handoff;
- aplicação relocada;
- testes de Flash, reset e rede.

Se a unidade de programação não for 2 bytes, o bloco de 100 deve ser revisto ou
o port deve implementar padding seguro. CAN FD/ETP/A-B/secure boot podem exigir
uma nova versão, não apenas recompilação.

## 29. Rastreabilidade resumida

| Grupo | Código principal | Teste principal | Validação física |
|---|---|---|---|
| HW | port STM32F1/Core | build | bancada elétrica |
| BOOT | `bootloader`, `boot_image` | bootloader/metadata | autoboot/handoff |
| NET | J1939 claim/config | claim/config Python+C | multinó/Commanded Address |
| PROTO | J1939/TP/protocol | protocol/TP Python+C | captura 29-bit/TP |
| UPD | bootloader/client | full update mocks | update/interrupção |
| PERS | metadata/config | torn-write/markers | power cut/journal |
| HOST | pacote `canloader` | client/CLI/bus | SLCAN + SocketCAN |
| QUAL | Makefiles/docs | `make test` | relatório A–N |

## 30. Checklist de liberação

### Firmware

- [ ] versão 2 e sem símbolos/IDs v1 ativos;
- [ ] mapa `19K+1K+43K+1K` coerente;
- [ ] `text+data <= 19456` e RAM/stack dentro de 20 KiB;
- [ ] todos os seis testes C executados;
- [ ] metadata e journal testados com torn writes;
- [ ] filtros extended/local/global conferidos;
- [ ] NAME de produção provisionado ou exceção de laboratório registrada.

### Aplicação

- [ ] `.isr_vector=0x08005000`;
- [ ] tamanho ≤44032;
- [ ] mesmo NAME/SA e Address Claim;
- [ ] IDENTIFY/INFO/ENTER implementados;
- [ ] backup request one-shot testado;
- [ ] handoff com IRQs restauradas.

### Host/rede

- [ ] Python 3.10+ e pacote v2 instalado;
- [ ] adaptador transmite extended IDs;
- [ ] Tool possui NAME/SA sem conflito;
- [ ] operações mutáveis usam `--name`;
- [ ] TP duas janelas observado;
- [ ] update de um NAME não altera o outro;
- [ ] recuperação por ST-LINK disponível.

### Evidência

- [ ] commit, toolchain e hashes registrados;
- [ ] build/size/map arquivados;
- [ ] pytest/testes C aprovados;
- [ ] captura e logs `--verbose` arquivados;
- [ ] sessão A–N preenchida;
- [ ] limitações de segurança e conformidade presentes no TCC.

## 31. Glossário e convenções

| Termo | Significado neste projeto |
|---|---|
| CA/ECU | aplicação controladora participante da rede |
| CEFF | Classical Extended Frame Format, CAN clássico com ID de 29 bits |
| NAME | identidade J1939 de 64 bits usada na disputa e no lock do host |
| preferred SA | endereço que a ECU tenta reivindicar primeiro |
| SA/DA | Source Address/Destination Address de um frame PDU1 |
| PGN | Parameter Group Number canônico; em PDU1 o byte DA não o integra |
| PDU1/PDU2 | formato dirigido (`PF<0xF0`) / formato global por Group Extension |
| TP.CM/TP.DT | controle e dados do Transport Protocol clássico |
| RTS/CTS | transferência TP dirigida com controle de fluxo |
| BAM | transferência TP global sem ACK/flow control |
| DM14/15/16 | request, response e dados do subset de acesso à memória |
| metadata | registro transacional que autoriza ou impede executar a imagem |
| journal | registros append-only de NAME/preferred SA na página de config |
| owner | SA do Tool que iniciou a sessão mutável atual |
| identity lock | conferência host de que o NAME esperado continua no SA usado |
| HIL | teste hardware-in-the-loop em placa e barramento físicos |

Inteiros multibyte dos payloads CanLoader são little-endian. Valores de
endereço e PGN são escritos em hexadecimal; tamanhos e tempos usam bytes e
milissegundos, salvo indicação contrária. `Tool` significa a ferramenta de
serviço no computador; `target` significa a Blue Pill em bootloader ou
aplicação de referência.

## 32. Referências internas

- [PROTOCOL.md](PROTOCOL.md): contrato on-wire byte a byte;
- [MEMORY_MAP.md](MEMORY_MAP.md): partições e formatos persistentes;
- [ARCHITECTURE.md](ARCHITECTURE.md): módulos e fluxos;
- [MIGRATION_V1_TO_V2.md](MIGRATION_V1_TO_V2.md): mudança incompatível;
- [VALIDATION.md](VALIDATION.md): campanha de teste;
- [LIMITATIONS.md](LIMITATIONS.md): riscos e fronteiras;
- [TCC_DIVERGENCES.md](TCC_DIVERGENCES.md): redação acadêmica e aderência.

## 33. Referências externas

- [SAE J1939/21_202205 — Data Link Layer](https://saemobilus.sae.org/standards/j193921_202205-data-link-layer)
- [SAE J1939/81_202504 — Network Management](https://saemobilus.sae.org/standards/j193981_202504-network-management)
- [SAE J1939/73_202412 — Application Layer Diagnostics](https://saemobilus.sae.org/standards/j193973_202412-application-layer-diagnostics)
- [SAE J1939 Digital Annex](https://saemobilus.sae.org/standards/j1939da_202607-j1939-digital-annex)
- [Documentação J1939 do kernel Linux](https://docs.kernel.org/networking/j1939.html)
- [Header UAPI J1939 do Linux](https://github.com/torvalds/linux/blob/master/include/uapi/linux/can/j1939.h)
- [TP J1939 do Linux](https://github.com/torvalds/linux/blob/master/net/can/j1939/transport.c)
- [Address Claim do Linux](https://github.com/torvalds/linux/blob/master/net/can/j1939/address-claim.c)
- [can-utils](https://github.com/linux-can/can-utils)
- [Revisão pública histórica J1939-73 arquivada pela UNECE](https://unece.org/fileadmin/DAM/trans/doc/2006/wp29/WP29-140-06e.pdf)

A revisão pública histórica não substitui a edição SAE atual licenciada.
