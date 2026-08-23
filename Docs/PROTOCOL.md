# Protocolo CAN CanLoader v1

**J1939-inspired, not SAE J1939 compliant**

O CanLoader usa conceitos de mensagem de controle, status, fragmentação,
sequenciamento e confirmação por bloco. Ele não implementa PGNs, DM14/DM15/DM16,
address claiming, Transport Protocol J1939 nem identificadores de 29 bits.

## Perfil CAN

| Propriedade | Valor atual |
|---|---|
| Camada de enlace | CAN clássico |
| Identificador | padrão de 11 bits |
| Payload | 0 a 8 bytes; os layouts v1 usam 4 a 8 bytes |
| Bitrate do target F1 | 500 kbit/s |
| Byte order multibyte | little-endian (LE) |
| Versão de protocolo | `1` |
| Sessão | 1 byte; update exige `1..255` |
| Tamanho de bloco | até 100 bytes |
| Dados por frame DATA | 1 a 5 bytes |

Todos os frames são data frames, nunca RTR nem extended. Um frame com DLC ou
tipo diferente do layout esperado é malformado.

## Identificadores

Cada dispositivo usa um node ID no intervalo `1..127`. O valor default é 1 e
fica em um registro persistente separado da aplicação.

| Direção | Fórmula | Nó 1 | Uso |
|---|---:|---:|---|
| bootloader → host | `0x580 + node` | `0x581` | ACK/NACK |
| host → bootloader | `0x600 + node` | `0x601` | comandos de controle |
| host → bootloader | `0x700 + node` | `0x701` | DATA |
| host → todos | `0x500` | `0x500` | DISCOVER, INFO e PROBE |

O filtro bxCAN do nó aceita seus IDs de comando e DATA e o broadcast. Um
controle broadcast bem formado que não seja DISCOVER/INFO/PROBE é silenciosamente
ignorado. O protocolo não implementa escalonamento de respostas broadcast;
vários nós podem responder quase simultaneamente e IDs de nó duplicados tornam
as respostas indistinguíveis. A CLI usa `PROBE` em `ls` e endereçamento unicast
nas operações que alteram estado.

## Layout do pedido de controle

ID `0x600 + node`, DLC exatamente 8:

| Byte | Campo | Descrição |
|---:|---|---|
| 0 | `version` | `0x01` |
| 1 | `command` | código da operação |
| 2 | `session` | sessão do host; update requer valor não zero |
| 3 | `parameter` | normalmente zero; seletor ou sequência quando definido |
| 4 | `value[7:0]` | byte menos significativo |
| 5 | `value[15:8]` |  |
| 6 | `value[23:16]` |  |
| 7 | `value[31:24]` | byte mais significativo |

Os quatro bytes `value` formam um `uint32_t` LE. Campos reservados devem ser
zero.

## Layout especial BEGIN_BLOCK

`BEGIN_BLOCK` continua no ID de comando e usa DLC 8, mas especializa os bytes
3..7:

| Byte | Campo | Regra |
|---:|---|---|
| 0 | `version` | `0x01` |
| 1 | `command` | `0x20` |
| 2 | `session` | sessão ativa |
| 3 | `block_sequence` | `block_index & 0xFF` |
| 4 | `block_index[7:0]` | índice LE |
| 5 | `block_index[15:8]` | índice LE |
| 6 | `block_length` | `1..100`; deve ser o tamanho exato esperado |
| 7 | reservado | `0x00` |

O offset da imagem não é transmitido: ele é calculado internamente como
`block_index * negotiated_block_size`, usando o tamanho devolvido por
`START_UPDATE` (100 no target F1). Isso impede escolher um endereço de Flash
arbitrário.

## Layout DATA

ID `0x700 + node`, DLC `4..8`:

| Byte | Campo | Regra |
|---:|---|---|
| 0 | `session` | sessão ativa |
| 1 | `block_sequence` | igual ao BEGIN_BLOCK atual |
| 2 | `frame_sequence` | começa em zero em cada bloco |
| 3..DLC-1 | `payload` | 1 a 5 bytes consecutivos da imagem |

DATA não carrega `version` nem o código `0x21`; ambos são implícitos pelo ID.
O código `0x21` aparece no campo `command` da resposta ao bloco.

## Layout da resposta

ID `0x580 + node`, DLC exatamente 8:

| Byte | Campo | Descrição |
|---:|---|---|
| 0 | `version` | `0x01` |
| 1 | `kind` | ACK `0x80` ou NACK `0x81` |
| 2 | `command` | comando ao qual a resposta se refere |
| 3 | `session` | sessão ecoada |
| 4..7 | `value` | `uint32_t` LE; resultado em ACK ou erro em NACK |

O host descarta respostas de outro ID, extended, com outra versão, sessão ou
comando. Em pontos críticos, também confere o valor exato do ACK para rejeitar
respostas antigas. Como INFO não ecoa o seletor, cada consulta stateless do host
usa uma sessão correlacionadora distinta; assim, um ACK INFO atrasado não pode
ser aceito pela consulta seguinte.

## Comandos

| Nome | Código | Estado/condição | `parameter` / `value` | `ACK.value` |
|---|---:|---|---|---:|
| `DISCOVER` | `0x01` | qualquer estado operacional | `0` / `0` | capabilities |
| `INFO` | `0x02` | qualquer estado operacional | `0` / seletor | valor selecionado |
| `PROBE` | `0x03` | bootloader ou aplicação compatível | seletor / `0` | valor selecionado |
| `SET_NODE_ID` | `0x04` | `BOOT_WINDOW`, `WAIT_HOST` ou `READY`; unicast | `0` / novo node | novo node |
| `START_UPDATE` | `0x10` | `WAIT_HOST` ou `READY`; duplicata idêntica em `PREPARE_UPDATE` | `0` / tamanho da imagem | tamanho de bloco, `1..100` (`100` no F1) |
| `SET_IMAGE_CRC` | `0x11` | `PREPARE_UPDATE`, mesma sessão | `0` / CRC esperado | CRC esperado |
| `ERASE` | `0x12` | `PREPARE_UPDATE`, CRC recebido; duplicata aceita antes do primeiro bloco | `0` / `0` | tamanho da imagem |
| `BEGIN_BLOCK` | `0x20` | `RECEIVE`, mesma sessão | layout especial | bytes já confirmados |
| `DATA` | `0x21` | frame no ID DATA depois de BEGIN_BLOCK | layout DATA | bytes confirmados ao completar bloco |
| `FINISH` | `0x30` | todos os bytes confirmados e nenhum bloco aberto; duplicata em `READY` | `0` / `0` | CRC calculado |
| `VERIFY` | `0x31` | metadata e imagem precisam validar | `0` / `0` | CRC persistido |
| `RUN_APPLICATION` | `0x32` | metadata e imagem precisam validar | `0` / `0` | CRC persistido |
| `ENTER_BOOTLOADER` | `0x33` | aplicação compatível ou bootloader | `0` / `0` | `0` |
| `ABORT` | `0x3F` | sessão precisa coincidir quando há uma ativa | `0` / `0` | `0` |

`VERIFY` e `RUN_APPLICATION` validam a imagem persistida no momento do pedido;
não dependem de uma sessão de update previamente aberta. Durante
`BOOT_WINDOW`, somente um pedido decodificável que resulte em ACK captura a
janela; ruído, versão inválida, NACK e comando mutável em broadcast não impedem
o autoboot. `PROBE` é a exceção explícita: responde sem capturar a janela.
START/RUN/ABORT mudam diretamente para seus estados de destino.

Depois de persistir um node novo, o bootloader responde no ID antigo, registra
uma solicitação de boot, reinicia e passa a filtrar o endereço novo. A aplicação
compatível trata `ENTER_BOOTLOADER` com ACK, solicitação em registradores de
backup e reset. A solicitação aumenta a janela de boot para 10 s; sem captura,
a aplicação válida volta a executar.

### Seletores INFO

| Seletor | Valor | Resposta |
|---|---:|---|
| `CAPABILITIES` | 0 | palavra de capabilities |
| `APPLICATION_BASE` | 1 | `0x08004000` |
| `APPLICATION_MAX_SIZE` | 2 | `0x0000BC00` / 48128 |
| `STORED_IMAGE_SIZE` | 3 | tamanho da metadata legível ou zero |
| `STORED_IMAGE_CRC` | 4 | CRC da metadata legível ou zero |
| `UID_WORD_0` | 5 | UID STM32 em `UID_BASE + 0` |
| `UID_WORD_1` | 6 | UID STM32 em `UID_BASE + 4` |
| `UID_WORD_2` | 7 | UID STM32 em `UID_BASE + 8` |

### Seletores PROBE

`PROBE` usa `parameter` como seletor e exige `value=0`. Ele é separado de INFO
para permitir inventário não invasivo durante o autoboot.

| Seletor | Valor | Resposta |
|---|---:|---|
| `CAPABILITIES` | 0 | palavra de capabilities |
| `UID_WORD_0` | 1 | primeira palavra do UID |
| `UID_WORD_1` | 2 | segunda palavra do UID |
| `UID_WORD_2` | 3 | terceira palavra do UID |

A palavra `CAPABILITIES` é:

| Bits | Conteúdo |
|---:|---|
| 0 | aplicação atualmente válida |
| 1 | update ativo (`PREPARE_UPDATE`, `ERASE`, `RECEIVE` ou `VERIFY`) |
| 2 | resposta originada pela aplicação em execução |
| 7..3 | reservados, zero |
| 15..8 | node ID |
| 23..16 | versão do protocolo |
| 31..24 | target code; `0xF1` no STM32F1 |

## Códigos NACK

Em um NACK, `response.value` contém exatamente um dos valores `0..15`:

| Enum | Valor | Significado |
|---|---:|---|
| `NONE` | 0 | sem erro; definido para completar o enum |
| `INVALID_COMMAND` | 1 | comando ou seletor desconhecido |
| `INVALID_STATE` | 2 | comando fora do estado atual |
| `INVALID_SEQUENCE` | 3 | bloco/frame incorreto, fora de ordem ou duplicata divergente |
| `INVALID_ADDRESS` | 4 | endereço fora da região de aplicação |
| `INVALID_SIZE` | 5 | imagem, bloco ou payload com tamanho inválido |
| `FLASH_ERASE` | 6 | erase ou preparação de metadata falhou |
| `FLASH_WRITE` | 7 | programação/readback de Flash falhou |
| `CRC_MISMATCH` | 8 | CRC calculado difere do anunciado |
| `TIMEOUT` | 9 | sessão do dispositivo expirou |
| `UNSUPPORTED_VERSION` | 10 | `version` diferente de 1 |
| `INVALID_APPLICATION` | 11 | metadata, estado ou vetores da imagem inválidos |
| `MALFORMED_FRAME` | 12 | ID, DLC, campo reservado ou layout inválido |
| `SESSION` | 13 | sessão zero ou diferente da ativa |
| `CAN` | 14 | erro CAN previsto pelo protocolo |
| `INTERNAL` | 15 | erro interno previsto pelo protocolo |

Nem todos os códigos precisam ser emitidos por todos os ports atuais; os valores
são parte estável do wire protocol v1.

## Transporte por blocos

Depois de `START_UPDATE`, a imagem é dividida usando o tamanho de bloco indicado
no ACK, limitado a 100 bytes. O target F1 anuncia 100; um port com unidade de
programação de 8 bytes pode, por exemplo, anunciar 96. Somente o último bloco
pode ser menor. Para cada bloco:

1. o host envia `BEGIN_BLOCK(index, length)`;
2. o bootloader responde com a contagem cumulativa de bytes já gravados;
3. se a contagem é o início do bloco, o host envia frames DATA numerados a partir
   de zero, com no máximo 5 bytes;
4. o bootloader mantém o bloco em RAM e somente grava quando recebe o comprimento
   inteiro em ordem;
5. depois do readback da Flash, responde `ACK(DATA, bytes_committed)`.

No F1, um bloco cheio de 100 bytes usa 20 frames DATA. Não existe ACK por DATA
individual; existe um ACK ao final do bloco.

As seguintes invariantes são conferidas:

- `block_index` precisa ser o próximo índice cumulativo;
- `block_sequence == block_index modulo 256`;
- `block_length` precisa ser `min(negotiated_block_size, bytes_restantes)`;
- `frame_sequence * 5` precisa apontar para dentro do bloco;
- cada payload precisa ter exatamente `min(5, bytes_restantes_no_bloco)`;
- sessão e sequência do bloco precisam coincidir.

Uma DATA já recebida é ignorada se o payload for idêntico ao buffer; se for
diferente, gera `INVALID_SEQUENCE`. Um novo BEGIN do bloco ativo reinicia o
buffer daquele bloco. Se o bloco já foi gravado e seu ACK se perdeu, repetir o
mesmo BEGIN retorna a contagem no fim do bloco, sem reprogramar a Flash. Esse ACK
permite ao host reconhecer o commit e avançar.

## Sessão, timeout e retry

O firmware mantém uma única sessão de update. `START_UPDATE` exige um byte de
sessão não zero; o cliente Python escolhe aleatoriamente um valor em `1..255` por
instância. Comandos e DATA de outra sessão são rejeitados enquanto a transferência
está ativa. DISCOVER, cada INFO e cada seletor PROBE usam sessões correlacionadoras distintas da
sessão de update, pois essas operações são stateless no firmware.

| Temporizador | Default | Comportamento |
|---|---:|---|
| boot window do firmware | 1500 ms | sem controle recebido, executa imagem válida |
| boot window solicitada pela aplicação | 10000 ms | sem controle recebido, volta à imagem válida |
| inatividade de transferência no firmware | 5000 ms | em `PREPARE_UPDATE`/`RECEIVE`, encerra sessão e emite `NACK(ABORT, TIMEOUT)` |
| ACK normal do host | 0,5 s | retransmite ao expirar |
| ACK de ERASE do host | 4 s | menor que os 5 s do dispositivo para permitir retry da sessão |
| retries do host | 3 | três retransmissões depois da tentativa inicial, quatro tentativas no total |
| atraso mínimo de RUN | 50 ms | caminho normal: ACK aceito para transmissão e mailbox TX livre antes do jump |
| limite absoluto de RUN | 250 ms | contado desde o pedido; força o jump mesmo com ACK pendente ou CAN ocupado |

O host retransmite controles idempotentes quando não recebe ACK. Em um bloco,
timeout de resposta ou NACK retryable reinicia a tentativa pelo BEGIN; a contagem
de bytes do ACK decide entre reenviar DATA e considerar o bloco já gravado. Um
erro fatal antes da verificação provoca um `ABORT` best-effort.

O NACK assíncrono `command=ABORT`, `value=TIMEOUT` é reconhecido pelo host mesmo
enquanto ele aguarda a resposta de outro comando e encerra a operação imediatamente.

O limite absoluto de RUN impede bloqueio indefinido no handoff. Se o CAN não
aceitar o ACK dentro de 250 ms, o dispositivo executa a aplicação mesmo assim e
o host pode observar timeout em vez da confirmação.

## CRC-32/ISO-HDLC

O CRC da imagem e o CRC do cabeçalho de metadata usam a mesma variante:

| Parâmetro | Valor |
|---|---:|
| Nome | CRC-32/ISO-HDLC |
| Largura | 32 bits |
| Polinômio normal | `0x04C11DB7` |
| Polinômio refletido usado no C | `0xEDB88320` |
| `init` | `0xFFFFFFFF` |
| `refin` / `refout` | verdadeiro / verdadeiro |
| `xorout` | `0xFFFFFFFF` |
| Vetor `123456789` | `0xCBF43926` |

O CRC da imagem cobre exatamente os `image_size` bytes do arquivo `.bin`, na
ordem transmitida. O valor de 32 bits aparece nos frames e na metadata em
little-endian. No host, `zlib.crc32(data) & 0xFFFFFFFF` produz o mesmo resultado.

O `header_crc32` da metadata cobre somente os bytes `0..19`; ele não cobre seu
próprio campo nem os markers `update`, `valid` e `invalid`.

CRC detecta corrupção acidental, mas não autentica o emissor nem a imagem. Não há
assinatura, MAC, criptografia ou proteção contra downgrade nesta versão.
