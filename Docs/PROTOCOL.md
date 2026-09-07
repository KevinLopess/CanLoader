# Protocolo do CanLoader sobre CAN/J1939

## 1. Escopo e declaração de conformidade

Este documento é o contrato on-wire do CanLoader. O valor `0x02` no campo de
versão identifica internamente o formato atual das mensagens; ele não designa
uma segunda versão lançada do produto. Esse é o único formato aceito pelo
firmware e pela ferramenta host. Não existe fallback para o protótipo anterior
baseado em IDs de 11 bits.

O perfil combina três categorias que precisam permanecer distintas:

| Categoria | Conteúdo usado pelo CanLoader | Como deve ser descrito |
|---|---|---|
| J1939-21/J1939-81 | CAN clássico com ID estendido de 29 bits, PDU1/PDU2, PGN, NAME, Request, Address Claimed, Commanded Address e TP clássico | comportamento J1939 implementado, sujeito à validação com a revisão SAE licenciada |
| subconjunto de diagnóstico | formatos e sequência de DM14, DM15 e DM16 para ERASE/WRITE | perfil baseado em semântica pública histórica de J1939-73; não se afirma conformidade com J1939-73 atual |
| CanLoader Proprietary A | identificação, manifest, sessão, CRC total, finalização, execução e entrada no bootloader | extensão proprietária do projeto, PGN `0xEF00` |

A edição vigente identificada durante a implementação é SAE
J1939/21_202205 para enlace, J1939/81_202504 para gestão de rede,
J1939/73_202412 para diagnóstico e J1939DA_202607 para atribuições. O conteúdo
integral dessas publicações não acompanha o repositório. Portanto, o texto
correto é:

> CanLoader implementa framing J1939 de 29 bits, gestão de endereço e TP
> clássico, além de um perfil de escrita baseado em DM14/DM15/DM16 históricos.
> O ciclo de vida da imagem usa Proprietary A. O projeto não foi certificado
> contra as publicações SAE atuais.

## 2. Convenções

- todos os CAN-IDs do v2 são estendidos, com 29 bits;
- todos os frames são CAN clássico, data frame, nunca RTR ou CAN FD;
- `SA` é Source Address e `DA` é Destination Address;
- `H` representa o SA da ferramenta host e `T` o SA do target;
- o host usa `H = 0xF9` por padrão e o target tenta `T = 0x80` por padrão;
- bytes multibyte explicitamente definidos como LE têm o byte menos
  significativo primeiro;
- bits em tabelas seguem a numeração J1939 de 1 a 8 quando indicada; em código,
  o bit menos significativo é bit 0;
- bytes reservados de frames de oito bytes são preenchidos com `0xFF`, salvo
  definição contrária;
- prioridade numérica menor tem precedência maior no barramento;
- o perfil usa prioridade 6 para todos os PGNs aqui definidos;
- valores hexadecimais representam a ordem lógica, enquanto sequências entre
  crases representam bytes na ordem transmitida.

Um adaptador SLCAN deve transmitir frames Lawicel `T`, e não `t`. Com
`python-can`, todo frame v2 exige `is_extended_id=True`.

## 3. Identificador CAN de 29 bits

O identificador é composto assim:

| Bits | Campo | Largura |
|---:|---|---:|
| 28..26 | Priority | 3 |
| 25 | Reserved, `R` | 1 |
| 24 | Data Page, `DP` | 1 |
| 23..16 | PDU Format, `PF` | 8 |
| 15..8 | PDU Specific, `PS` | 8 |
| 7..0 | Source Address, `SA` | 8 |

O PGN normalizado possui `R`, `DP`, `PF` e, somente em PDU2, `PS`:

```text
PGN = (R << 17) | (DP << 16) | (PF << 8) |
      (PF >= 0xF0 ? PS : 0)
```

### 3.1 PDU1: PF menor que `0xF0`

Em PDU1, `PS` é o endereço de destino e não faz parte do PGN. O byte menos
significativo do PGN deve ser normalizado para zero.

```text
CAN_ID = (priority << 26) | (PGN << 8) | (DA << 8) | SA
```

DM14 `0xD900`, por exemplo, enviado de `H=0xF9` para `T=0x80` usa
`0x18D980F9`.

### 3.2 PDU2: PF maior ou igual a `0xF0`

Em PDU2, `PS` é Group Extension e integra o PGN. Não existe DA individual no
identificador; a mensagem é global.

```text
CAN_ID = (priority << 26) | (PGN << 8) | SA
```

Commanded Address `0xFED8`, depois de remontado pelo TP, é um PGN PDU2.

### 3.3 Validação de recepção

Antes de interpretar payload, o receptor confere:

1. frame estendido e não RTR;
2. ID limitado a 29 bits;
3. PGN normalizado de acordo com PF;
4. DA igual ao SA local ou global, quando PDU1;
5. SA permitido para a operação e diferente de `0xFF`;
6. DLC exato do tipo de mensagem;
7. estado de Address Claim concluído para tráfego operacional;
8. origem/sessão iguais às que abriram o update para mensagens mutáveis.

## 4. PGNs usados

| Nome | PGN | PF | Formato | Direção principal | Tamanho lógico |
|---|---:|---:|---|---|---:|
| DM16 Binary Data Transfer | `0xD700` | `D7` | PDU1 | Tool → target em WRITE | 2..1785 |
| DM15 Memory Access Response | `0xD800` | `D8` | PDU1 | target → Tool | 8 |
| DM14 Memory Access Request | `0xD900` | `D9` | PDU1 | Tool → target | 8 |
| Request | `0xEA00` | `EA` | PDU1 | qualquer → específico/global | 3 ou 8 |
| TP.DT | `0xEB00` | `EB` | PDU1 | conforme sessão TP | 8 |
| TP.CM | `0xEC00` | `EC` | PDU1 | conforme sessão TP | 8 |
| Address Claimed/Cannot Claim | `0xEE00` | `EE` | PDU1/global | ECU → global | 8 |
| CanLoader Proprietary A | `0xEF00` | `EF` | PDU1 | Tool ↔ target | 8 |
| Commanded Address | `0xFED8` | `FE` | PDU2/global | commanding ECU → global | 9 via BAM |

Com `H=0xF9` e `T=0x80`, os IDs mais frequentes são:

| Mensagem | CAN-ID |
|---|---:|
| Request global do host | `0x18EAFFF9` |
| Address Claimed do target | `0x18EEFF80` |
| Proprietary A request | `0x18EF80F9` |
| Proprietary A response | `0x18EFF980` |
| DM14 | `0x18D980F9` |
| DM15 | `0x18D8F980` |
| DM16 single-frame | `0x18D780F9` |
| TP.CM Tool → target | `0x18EC80F9` |
| TP.CM target → Tool | `0x18ECF980` |
| TP.DT Tool → target | `0x18EB80F9` |

## 5. NAME e endereços

### 5.1 Faixas de endereço

| Valor | Significado |
|---:|---|
| `0x00..0xFD` | Source Address unicast válido |
| `0xFE` | NULL/idle; também usado como SA em Cannot Claim |
| `0xFF` | global/no address; nunca SA operacional de um update |

O preferred address do target é `0x80`. O host/service tool usa `0xF9` por
padrão. Ambos são configuráveis, mas uma integração de produção precisa validar
essas escolhas no J1939 Digital Annex e no conjunto completo de ECUs da rede.

### 5.2 Layout do NAME

O NAME é um inteiro sem sinal de 64 bits:

| Bits | Campo | Largura |
|---:|---|---:|
| 0..20 | Identity Number | 21 |
| 21..31 | Manufacturer Code | 11 |
| 32..34 | ECU Instance | 3 |
| 35..39 | Function Instance | 5 |
| 40..47 | Function | 8 |
| 48 | Reserved | 1 |
| 49..55 | Vehicle System | 7 |
| 56..59 | Vehicle System Instance | 4 |
| 60..62 | Industry Group | 3 |
| 63 | Arbitrary Address Capable, AAC | 1 |

No payload de Address Claimed e nos oito primeiros bytes de Commanded Address,
o inteiro é transmitido little-endian: `name & 0xFF` primeiro.

Cada dispositivo físico precisa de NAME estável e único. O default de
desenvolvimento do target deriva deterministicamente o Identity Number do UID96
do STM32 e usa `Manufacturer Code = 0`, com `AAC = 1`. Esse código de fabricante
não é uma atribuição SAE e não oferece unicidade global; ele só é aceitável em
bancada. O registro persistente permite provisionar um NAME válido.

Bootloader e aplicação usam o mesmo NAME, preferred address e regra de claim.
O host não deve tratar uma mudança aplicação → bootloader como uma identidade
nova.

## 6. Address Claim e descoberta

### 6.1 Address Claimed

O PGN `0xEE00` é enviado globalmente, DLC 8:

```text
Byte 0..7 = NAME little-endian
```

Ao iniciar, o target:

1. carrega NAME e preferred address persistentes;
2. escolhe inicialmente `0x80`, salvo configuração válida;
3. envia Address Claimed de seu SA candidato;
4. aguarda 250 ms sem transmitir tráfego operacional;
5. torna o endereço operacional se não perder uma disputa.

A exceção é a resposta a uma solicitação de Address Claimed: ela pode ser
transmitida durante a janela de claim. O timer de 250 ms não é reiniciado por
uma repetição idêntica do mesmo NAME/SA.

Quando dois NAMEs reivindicam o mesmo SA, o menor valor numérico de NAME tem
prioridade. O perdedor com `AAC=1` tenta outro endereço da faixa permitida e
repete o claim. Se não houver endereço, envia Cannot Claim com SA `0xFE` e seu
NAME; nesse estado, não inicia tráfego dirigido de atualização.

Dois controladores com o mesmo NAME são uma falha de provisionamento. O perfil
não consegue selecionar com segurança um deles e o host deve abortar.

### 6.2 Request for Address Claimed

Request usa PGN `0xEA00`. Para solicitar os claims de toda a rede:

```text
CAN-ID: 0x18EAFFHH
Data:   00 EE 00 FF FF FF FF FF
```

Os três primeiros bytes são o PGN solicitado, little-endian. O frame pode ter
DLC 3 em uma API J1939; o perfil raw/SLCAN transmite DLC 8 e completa com
`0xFF` para ter um vetor único.

Cada ECU com endereço/NAME conhecido responde retransmitindo Address Claimed.
O comando `canloader ls` coleta por NAME, não por ordem de chegada.

Address Claim apenas encontra ECUs J1939. Para distinguir um CanLoader, o host
envia `IDENTIFY` Proprietary A a cada SA não ambíguo. Só classifica o peer como
CanLoader quando a resposta correlacionada anuncia o endereço esperado, versão
2, um target code suportado (`0xF1` para STM32F103C8 ou `0xB1` para
STM32G0B1RE) e os flags J1939, Address Claim e TP RX.

## 7. Commanded Address

Commanded Address usa PGN global `0xFED8` e payload lógico de nove bytes:

| Byte | Campo |
|---:|---|
| 0..7 | NAME alvo, LE |
| 8 | novo preferred Source Address |

Por exceder oito bytes e ser global, ele é transportado exclusivamente por BAM:

```text
TP.CM BAM: 20 09 00 02 FF D8 FE 00
TP.DT #1:  01 name0 name1 name2 name3 name4 name5 name6
TP.DT #2:  02 name7 newSA FF FF FF FF FF
```

O target age somente quando os oito bytes de NAME coincidem exatamente. O novo
SA precisa estar em `0x00..0xFD`. Após validar, o target grava e relê a
configuração, abandona o endereço anterior e executa Address Claim no novo
endereço. O PGN não define ACK próprio. O host só confirma a mudança depois de
observar o mesmo NAME no novo SA por uma janela completa de claim, verificar
que ele é o único ocupante conhecido do endereço e concluir um `IDENTIFY`
CanLoader dirigido na nova rota. Se o target perder a arbitragem e reclamar
outro SA, a operação falha explicitamente. Se a confirmação não chegar, cada
retry retransmite o BAM completo; gravar novamente o mesmo preferred SA é
idempotente no target. Antes de sobrepor um BAM possivelmente incompleto, o host
aguarda ao menos seu `transport_timeout`, maior que o timeout TP do target na
configuração padrão.

Uma falha de persistência não deve deixar um endereço anunciado como permanente
sem registro coerente. Como Commanded Address não possui ACK, o host detecta a
falha pela ausência do novo Address Claimed e encerra por timeout.

## 8. Transport Protocol clássico

### 8.1 Escopo implementado

O v2 implementa TP clássico para mensagens lógicas de 9 a 1785 bytes. ETP,
mensagens maiores que 1785 bytes, CAN FD e fast-packet não são suportados.

- DM16 de WRITE usa RTS/CTS unicast;
- Commanded Address usa BAM global;
- firmware nunca é enviado por BAM, pois BAM não tem flow control nem ACK;
- existe uma única sessão TP RX total no target; SA, DA e PGN fazem parte da
  chave validada dessa sessão, não criam contextos paralelos;
- a janela CTS do target é de no máximo 8 pacotes;
- o timeout de progresso TP do target é 1250 ms;
- o host usa intervalo configurado de 50 ms após o BAM e antes de cada
  pacote TP.DT, inclusive o primeiro.

### 8.2 TP.CM

TP.CM usa PGN `0xEC00`, DLC 8. O PGN transportado aparece sempre em bytes 5 a
7, little-endian.

#### RTS — `0x10`

```text
Byte 0     10
Byte 1     total_size LSB
Byte 2     total_size MSB
Byte 3     packet_count = ceil(total_size / 7)
Byte 4     max_packets_per_CTS
Byte 5..7  transported_PGN LE24
```

#### CTS — `0x11`

```text
Byte 0     11
Byte 1     packets_allowed
Byte 2     next_sequence
Byte 3..4  FF FF
Byte 5..7  transported_PGN LE24
```

`packets_allowed=0` pausa a transmissão, mas não encerra a sessão. O target
concede `min(8, sender_max_do_RTS, pacotes_restantes)`.

#### End Of Message Acknowledgment — `0x13`

```text
Byte 0     13
Byte 1     total_size LSB
Byte 2     total_size MSB
Byte 3     packet_count
Byte 4     FF
Byte 5..7  transported_PGN LE24
```

EOM_ACK confirma remontagem do payload TP. Para DM16, ele não comprova que a
Flash foi programada; o resultado de memória é um DM15 posterior.

#### BAM — `0x20`

```text
Byte 0     20
Byte 1     total_size LSB
Byte 2     total_size MSB
Byte 3     packet_count
Byte 4     FF
Byte 5..7  transported_PGN LE24
```

BAM não usa CTS nem EOM_ACK.

#### Connection Abort — `0xFF`

```text
Byte 0     FF
Byte 1     reason
Byte 2..4  FF FF FF
Byte 5..7  transported_PGN LE24
```

O host conhece a taxonomia abaixo para interpretar peers e gerar um aborto
genérico. O receptor embarcado desta revisão emite apenas `1`, `2` e `3`;
sequência inválida é encerrada com o motivo genérico `2`:

| Código | Uso |
|---:|---|
| `1` | sessão incompatível já ativa |
| `2` | recurso/buffer indisponível |
| `3` | timeout |
| `4` | controle CTS inesperado durante dados |
| `5` | falha irrecuperável/retries excedidos |
| `6` | TP.DT inesperado |
| `7` | sequence number inválido |
| `8` | sequence number duplicado sem recuperação |
| `9` | tamanho acima de 1785 em TP clássico |
| `250` | outro motivo identificado somente como genérico |

Após abortar TP por uma falha durante DM16, o target também encerra a operação
de memória com DM15 `Operation Failed` quando ainda consegue responder.

### 8.3 TP.DT

TP.DT usa PGN `0xEB00`, DLC 8:

```text
Byte 0     sequence_number, 1..255
Byte 1..7  sete bytes consecutivos do payload lógico
```

O último pacote é completado com `0xFF`. `total_size`, e não o padding, define
o final. O receptor valida sequência, origem, destino e PGN da sessão antes de
copiar dados e limita cada nova concessão ao tamanho anunciado no CTS. O target
pressupõe que o sender aguarde essa concessão; a limitação física descrita em
`LIMITATIONS.md` impede afirmar que um burst adiantado é sempre descartado.

### 8.4 Exemplo de DM16 com bloco de 100 bytes no F103

O payload lógico tem 101 bytes: `FF` seguido dos 100 bytes crus. São necessários
15 TP.DT:

```text
18EC80F9#1065000F0F00D700    RTS, 101 bytes, 15 pacotes, sender-max 15
18ECF980#110801FFFF00D700    CTS, envie 8 a partir do pacote 1
18EB80F9#01FF<data0..5>      DT 1: header DM16 + seis bytes
...                         DT 2..8
18ECF980#110709FFFF00D700    CTS, envie 7 a partir do pacote 9
...                         DT 9..15
18ECF980#1365000FFF00D700    EOM_ACK
```

Os marcadores `<...>` representam bytes, não texto ASCII. Ao remontar o último
TP.DT, o target enfileira EOM_ACK, programa e relê a Flash e então enfileira
DM15. O bxCAN usa prioridade FIFO por ordem de solicitação (`TXFP=1`) para que
DM15 não ultrapasse o EOM_ACK por ter CAN-ID numericamente menor. O host também
preserva um DM15 eventualmente observado enquanto aguarda TP.CM.

No G0, o bloco anunciado é 96 bytes. O payload lógico tem 97 bytes e ocupa 14
TP.DT. O formato não muda; somente o tamanho retornado por `START_UPDATE` muda.

## 9. DM14 — Memory Access Request

DM14 usa PGN `0xD900`, prioridade 6, PDU1 e DLC 8.

| Byte | Campo |
|---:|---|
| 0 | Length/Number Requested bits 7..0 |
| 1 bits 7..5 | Length/Number Requested bits 10..8 |
| 1 bit 4 | Pointer Type |
| 1 bits 3..1 | Command |
| 1 bit 0 | reservado, transmitido como 1 |
| 2..4 | Pointer bits 23..0, LE |
| 5 | Pointer Extension |
| 6..7 | Key/User_Level, LE |

Codificação do byte 1:

```text
b1 = (((length >> 8) & 0x07) << 5) |
     ((pointer_type & 0x01) << 4)   |
     ((command & 0x07) << 1)       |
     0x01
```

Embora o campo tenha 11 bits físicos, o perfil histórico usado define a faixa
válida de Length/Number como `0..1784`. Valores `1785..2047` são rejeitados.

### 9.1 Pointer Type

O CanLoader aceita apenas `Pointer Type = 0`, Direct Memory Addressing. Pointer
Extension é concatenado como byte alto:

```text
address = byte2 | byte3<<8 | byte4<<16 | byte5<<24
```

Directed Spatial Addressing (`Pointer Type = 1`) não é implementado.

### 9.2 Commands

| Valor | Command histórico | Estado no perfil v2 |
|---:|---|---|
| `0` | Erase | suportado para páginas da aplicação |
| `1` | Read | não suportado |
| `2` | Write | suportado |
| `3` | Status Request | suportado para a operação do mesmo Tool |
| `4` | Operation Completed | suportado como fechamento pelo Tool |
| `5` | Operation Failed | aceito como cancelamento best-effort da operação DM corrente; regra de perfil |
| `6` | Boot Load | não suportado; execução é Proprietary A |
| `7` | EDCP Generation | não suportado; CRC total é Proprietary A |

### 9.3 ERASE

Para ERASE, `length` é a quantidade de unidades de apagamento definidas pelo
device, e não a quantidade de bytes. A ferramenta obtém essa unidade pelo
seletor INFO 11: 1024 bytes no F103 e 2048 bytes no G0.

```text
command       = 0
pointer_type  = 0
pointer       = APP_BASE, alinhado a erase_unit
length        = ceil(image_size / erase_unit)
key           = FFFF
```

O target recusa zero páginas, desalinhamento, overflow e qualquer intervalo que
saia da partição de aplicação ou alcance bootloader/configuração/metadata. Para
uma imagem máxima, o resultado é 43 páginas no F103 e 239 no G0.

O ERASE de toda a área necessária é uma única operação DM14. Enviar
`length=2048` significaria 2048 unidades, não 2048 bytes, e é inválido para os
targets atuais.

### 9.4 WRITE

Cada WRITE abre exatamente um bloco da imagem:

```text
command       = 2
pointer_type  = 0
pointer       = APP_BASE + committed_offset
length        = min(block_size, image_size - committed_offset)
key           = FFFF
```

O limite geral de DM16 é 1784 bytes crus, mas o perfil CanLoader limita cada
WRITE a no máximo 100 bytes. O valor efetivo é informado no ACK de
`START_UPDATE`: 100 no F103 e 96 no G0. Somente o último bloco pode ser menor
que o valor anunciado.

O target valida estado de manifest, Tool proprietário da sessão, endereço
esperado, comprimento exato, faixa de Flash e ausência de outro DM/TP ativo.

### 9.5 STATUS REQUEST e fechamento

A forma canônica enviada pelo host para Status Request usa `length=0`, Direct
Memory Addressing, `command=3`, pointer zero e `Key/User_Level=FFFF`. Na recepção,
o target exige Direct/`FFFF`, mas não usa length nem pointer para selecionar o
estado. Somente o Tool que abriu a sessão pode consultar. O target responde BUSY
durante erase, PROCEED enquanto aguarda DM16, COMPLETED enquanto o close está
pendente e COMPLETED com length zero quando não há operação DM.

Depois de DM15 COMPLETED em ERASE/WRITE, o Tool envia DM14 Operation Completed
(`command=4`) para completar o close. Por regra específica do CanLoader, esse
frame repete exatamente `pointer` e `length` do ERASE/WRITE encerrado. Um close
com `length=0` é rejeitado. Não existe timer DM separado de 100 ms na
implementação: a operação pendente permanece sob o timeout global de inatividade
do update de 5000 ms.

DM14 Operation Failed (`command=5`) é aceito do owner como cancelamento
best-effort da operação DM corrente: cancela a expectativa TP e limpa flags de
operação/close, sem resposta DM15. Essa semântica é uma extensão do perfil; no
material histórico consultado, o comando é descrito no fechamento de leitura,
e READ não é implementado aqui. O cliente normal não o envia no caminho de
sucesso e pode usar TP.ABORT ao falhar uma transferência.

### 9.6 Key/User_Level

O v2 usa `0xFFFF`, No Key Available/sem novo key requerido, e não implementa
seed-key, password, User_Level, DM18 ou chave longa. O campo:

- não é o byte de sessão CanLoader;
- não autentica o host;
- precisa permanecer constante quando uma requisição é repetida;
- não deve receber token proprietário.

A sessão de update existe somente no PGN Proprietary A e na FSM local associada
ao NAME/SA do Tool.

## 10. DM15 — Memory Access Response

DM15 usa PGN `0xD800`, prioridade 6, PDU1 e DLC 8.

| Byte | Campo |
|---:|---|
| 0 | Length/Number Allowed bits 7..0 |
| 1 bits 7..5 | Length/Number Allowed bits 10..8 |
| 1 bit 4 | reservado |
| 1 bits 3..1 | Status |
| 1 bit 0 | reservado |
| 2..4 | Error Indicator/EDC Parameter, LE24 |
| 5 | EDCP Extension |
| 6..7 | Seed, LE16 |

O encoder do target transmite os dois bits reservados do byte 1 como 1:

```text
b1 = (((allowed >> 8) & 0x07) << 5) |
     0x10                              |
     ((status & 0x07) << 1)           |
     0x01
```

O host exige os dois bits em 1 antes de mascarar os campos de length/status. Um
peer que os envie como 0 não é compatível com este perfil estrito.

### 10.1 Status

| Valor | Status | Uso no CanLoader |
|---:|---|---|
| `0` | Proceed | operação aceita; `allowed` é o tamanho autorizado |
| `1` | Busy | operação não pode prosseguir agora; o target pode ecoar o length corrente |
| `2`, `3` | Reserved | rejeitados pelo host |
| `4` | Operation Completed | erase/write concluído; o target ecoa o length da operação |
| `5` | Operation Failed | operação rejeitada/encerrada; o target ecoa o length conhecido |
| `6`, `7` | Reserved | rejeitados pelo host |

Em PROCEED de WRITE, `allowed` precisa ser exatamente o length do DM14. O host
não envia DM16 se o target reduzir, ampliar ou zerar esse valor.

### 10.2 Campo diagnóstico proprietário, byte 5 e Seed

O target não negocia EDCP nem seed-key. Para tornar o erro de implementação
observável sem inventar uma atribuição SAE, o perfil define explicitamente:

```text
bytes 2..4 = boot_error CanLoader, uint24 little-endian
byte 5      = 00
bytes 6..7  = FF FF
```

Em PROCEED/BUSY/COMPLETED sem erro, `boot_error=0`. Em FAILED, o código pertence
ao mesmo enum de NACK Proprietary A (`0..17`). O host expõe a composição como
`error_code = uint24(bytes2..4) | byte5<<24`, mas byte 5 é zero neste target.

Essa codificação é uma extensão diagnóstica CanLoader dentro da estrutura de
DM15. Ela **não** é um Error Indicator ou EDCP normativo de J1939-73. Um tester
J1939 genérico não deve interpretar byte 5 como uma atribuição EDCP SAE nem os
valores `0..17` como códigos SAE. `Seed=0xFFFF` confirma que o subset não oferece
seed-key. EDCP concatenado e DM18 não são suportados.

## 11. DM16 — Binary Data Transfer

DM16 usa PGN `0xD700`, prioridade 6 e PDU1. Seu conteúdo é apenas o dado cru da
operação aberta por DM14.

### 11.1 Um a sete bytes crus

Para `raw_length` entre 1 e 7, DM16 é um único frame:

```text
Byte 0       raw_length, 01..07
Byte 1..N    raw bytes
restante     FF no encoder CanLoader
DLC          8 no host de referência
```

O receptor usa o count, não o padding, como comprimento. Para interoperar com
peers históricos ele aceita DLC entre `raw_length+1` e 8; o host de referência
sempre envia DLC 8 preenchido com `0xFF`. Um count diferente de `DM14.length` é
rejeitado.

### 11.2 Oito a 1784 bytes crus

Para mais de sete bytes, a mensagem lógica entregue ao TP é:

```text
FF <raw byte 0> <raw byte 1> ... <raw byte length-1>
```

Assim:

```text
tp_total_size = raw_length + 1
packet_count  = ceil(tp_total_size / 7)
```

O TP é obrigatoriamente RTS/CTS dirigido para o PGN `0xD700`. O primeiro TP.DT
contém sequence number, `0xFF` e os seis primeiros bytes crus. O máximo teórico
é 1784 bytes crus; o update atual envia no máximo 100.

Valores count `0` ou `8..254` e tamanho direto incompatível geram DM15
`Operation Failed`, mas deixam a expectativa de WRITE disponível para retry.
Um TP dirigido do owner cujo tamanho não corresponde a `DM14.length+1` é
rejeitado e encerra aquela expectativa. DM16 de outra origem, BAM para DM16 e
DM16 sem WRITE autorizado são ignorados ou rejeitados sem tomar posse da
operação corrente. Assim, somente tráfego que casa owner, destino, PGN e a
expectativa autorizada pode avançar ou cancelar o WRITE.

## 12. CanLoader Proprietary A

Proprietary A usa PGN `0xEF00`, prioridade 6 e sempre é dirigido. Ele não é
enviado em broadcast e não usa TP.

### 12.1 Request

```text
Byte 0     protocol_version = 02
Byte 1     command
Byte 2     session
Byte 3     parameter
Byte 4..7  value, uint32 LE
```

### 12.2 Response

```text
Byte 0     protocol_version = 02
Byte 1     kind: ACK=80, NACK=81
Byte 2     echoed_command
Byte 3     echoed_session
Byte 4..7  result ou NACK code, uint32 LE
```

O host valida versão, PGN, SA, DA, command e session antes de aceitar a resposta.
Todo request v2 exige `session=1..255`, inclusive comandos stateless; zero é
rejeitado com `BOOT_ERROR_SESSION`.

### 12.3 Commands

| Nome | Código | Request `parameter/value` | ACK `value` |
|---|---:|---|---|
| `IDENTIFY` | `0x01` | `0 / 0`; stateless | capabilities |
| `INFO` | `0x02` | selector / `0`; stateless | valor do selector |
| `START_UPDATE` | `0x10` | `0 / image_size`; session não zero | block size: 100 no F103, 96 no G0 |
| `SET_IMAGE_CRC` | `0x11` | `0 / expected_crc32`; owner/session | CRC ecoado |
| `FINALIZE` | `0x30` | `0 / expected_crc32`; todos os bytes e nenhum close | CRC calculado |
| `VERIFY` | `0x31` | `0 / 0`; stateless | CRC persistido |
| `RUN_APPLICATION` | `0x32` | `0 / 0`; imagem válida | CRC persistido |
| `ENTER_BOOTLOADER` | `0x33` | `0 / 0` | zero; aplicação registra pedido e reinicia |
| `ABORT` | `0x3F` | `0 / 0`; owner se update ativo | zero |

Não existem `SET_NODE_ID`, `ERASE`, `BEGIN_BLOCK` ou `DATA` proprietários no
v2. Reendereçamento usa Commanded Address; erase e write usam DM14/15/16.

Os seletores INFO, capabilities e códigos NACK são definidos pelos enums C e
Python e devem permanecer idênticos. O `value` de NACK nunca é interpretado como
um Error Indicator SAE; são namespaces diferentes.

Seletores INFO: `0` capabilities, `1` APP_BASE, `2` APP_MAX_SIZE, `3` tamanho
armazenado, `4` CRC armazenado, `5..7` words UID96, `8..9` NAME low/high, `10`
SA atual e `11` tamanho em bytes da unidade de apagamento da Flash. Capabilities
usa flags nos bits `0..5`, SA em `15..8`, versão em `23..16` e target code em
`31..24`. Os códigos NACK `0..17` estão
enumerados na documentação de implementação e nos headers C/Python.

Durante um update, `RUN_APPLICATION`, `VERIFY` e `ENTER_BOOTLOADER` são
rejeitados: o owner recebe `BUSY` e outro SA/session recebe `SESSION`.
`IDENTIFY` e `INFO` continuam disponíveis, mas tráfego stateless ou de outra ECU
não renova o timeout do update. `ABORT` fora de uma sessão ativa é ACK/no-op e
jamais invalida uma imagem já finalizada.

### 12.4 Sessão proprietária

`START_UPDATE` exige session `1..255`. No target, a sessão é associada ao Source
Address do Tool, ao target e ao manifest. O cliente host acrescenta o bloqueio
por NAME e monitora Address Claimed; o firmware embarcado não armazena o NAME
do Tool no contexto. Mensagens de outro SA ou session não podem apagar,
escrever, finalizar ou abortar a sessão ativa.

IDENTIFY e INFO são stateless e usam session apenas para correlação. O host
escolhe valores novos para evitar aceitar respostas atrasadas. A sessão expira
após 5000 ms sem progresso. DM14 válido e cada TP.RTS/TP.DT DM16 consumido do
owner renovam essa atividade; quadros de outros peers não a renovam. O timeout
TP de 1250 ms encerra a operação DM16 corrente e produz ABORT/DM15 de falha; o
host pode reiniciar o bloco enquanto o manifest ainda estiver ativo.

## 13. Fluxo completo de update

```text
Tool                                              Target
 |--- Request(Address Claimed) -------------------->|
 |<-- Address Claimed(NAME, T) ---------------------|
 |--- PropA IDENTIFY ------------------------------>|
 |<-- PropA ACK ------------------------------------|
 |--- PropA START_UPDATE(size, session) ----------->|
 |<-- PropA ACK(block_size do target) --------------|
 |--- PropA INFO(FLASH_ERASE_UNIT_SIZE) ----------->|
 |<-- PropA ACK(erase_unit) ------------------------|
 |--- PropA SET_IMAGE_CRC(crc32, session) -------->|
 |<-- PropA ACK ------------------------------------|
 |--- DM14 ERASE(APP_BASE, pages, FFFF) ---------->|
 |<-- DM15 PROCEED(pages) --------------------------|
 |<-- DM15 COMPLETED ou FAILED ---------------------|
 |--- DM14 OPERATION COMPLETED(APP_BASE,pages) ---->|
 |                                                  |
 |    para cada bloco do tamanho anunciado:         |
 |--- DM14 WRITE(address, length, FFFF) ---------->|
 |<-- DM15 PROCEED(length, Seed=FFFF) --------------|
 |--- DM16 direto, se length <= 7 ----------------->|
 |    ou, se length >= 8:                           |
 |--- TP.RTS(DM16, length+1) ---------------------->|
 |<-- TP.CTS(window<=8) ----------------------------|
 |--- TP.DT... ------------------------------------>|
 |<-- TP.CTS... / EOM_ACK --------------------------|
 |<-- DM15 COMPLETED ou FAILED ---------------------|
 |--- DM14 OPERATION COMPLETED(address,length) ---->|
 |                                                  |
 |--- PropA FINALIZE ------------------------------>|
 |<-- PropA ACK(crc calculado) ---------------------|
 |--- PropA VERIFY -------------------------------->|
 |<-- PropA ACK(crc persistido) --------------------|
 |--- PropA RUN_APPLICATION opcional -------------->|
 |<-- PropA ACK best-effort ------------------------|
```

Invariantes:

- nenhuma página de aplicação é apagada antes de manifest e CRC serem aceitos e
  metadata `UPDATING` ser persistida;
- ERASE cobre somente as páginas necessárias à imagem;
- WRITE começa em `APP_BASE` e avança sem lacunas pelo bloco anunciado;
- DM16 nunca precede DM15 PROCEED;
- EOM_ACK é enfileirado ao concluir o TP; DM15 vem depois da tentativa de
  programação e reflete o resultado real da Flash;
- falha fatal de erase/write/finalização invalida a metadata quando possível e
  impede FINALIZE/RUN; uma falha de sessão TP pode permitir retry da operação;
- FINALIZE só promove `VALID` depois de tamanho, CRC e vetores corretos;
- somente imagem `VALID` é executada.

## 14. Timeout, retry e idempotência

| Timer | Valor do perfil | Origem |
|---|---:|---|
| Address Claim | 250 ms | regra de gestão de rede usada pelo perfil |
| progresso TP no target | 1250 ms | timeout do receptor implementado |
| espera TP no host | 1500 ms | configuração do cliente CanLoader |
| intervalo BAM → TP.DT e entre TP.DT no host | 50 ms | configuração conservadora do perfil |
| inatividade de update | 5000 ms | regra proprietária CanLoader |
| boot window normal | 1500 ms | regra proprietária CanLoader |
| boot window solicitada pela aplicação | 10000 ms | regra proprietária CanLoader |

O host pode repetir um request Proprietary A idempotente quando perde a
resposta. Em DM/TP:

1. timeout antes de PROCEED permite repetir o mesmo DM14; STATUS permanece
   disponível para diagnóstico;
2. timeout após RTS faz o host enviar TP.ABORT best-effort antes de reiniciar o
   bloco;
3. perda de EOM_ACK só é tratada como commit quando um DM15 COMPLETED dirigido,
   com o mesmo length, já foi preservado pelo host; sem ele, o DM14 é repetido;
4. perda de DM15 final faz o host repetir o mesmo DM14, ao qual o target responde
   COMPLETED se o bloco já foi gravado;
5. após DM15 final, o Tool envia o close com pointer/length originais; como não
   há ACK positivo, ele repete o frame conforme o orçamento de retries e o
   target trata duplicatas exatas como idempotentes e silenciosas;
6. retry de bloco começa novamente em DM14 WRITE; o target não aceita TP.DT
   órfão;
7. expiração de 5 s encerra o manifest e exige novo START_UPDATE.

Retries não podem alterar length, pointer, CRC, tamanho ou session de uma
operação já aberta. Uma duplicata divergente é erro de sequência, não um novo
pedido.

## 15. Segurança e proteção de memória

O protocolo garante limites e integridade acidental, não autenticidade:

- somente a região `APPLICATION` definida pelo target é apagável/gravável;
- bootloader, configuração persistente e metadata são excluídos do pointer
  aceito em DM14;
- soma `pointer+length`, arredondamento de páginas e conversões são verificadas
  contra overflow;
- Flash é relida depois de erase/programação;
- CRC-32/ISO-HDLC cobre exatamente `image_size` bytes;
- MSP e Reset Handler são validados antes do jump;
- `Key/User_Level=FFFF` explicitamente desativa o fluxo de segurança DM;
- Address Claim, Commanded Address e Proprietary A não autenticam remetente;
- não há assinatura, MAC, criptografia, secure boot ou anti-rollback.

Em uma rede não confiável, um participante pode reendereçar o target ou instalar
uma imagem com CRC válido. Produção exige autenticação e política de serviço
fora deste perfil ou uma evolução incompatível do protocolo.

## 16. Critérios mínimos de interoperabilidade

Uma implementação v2 é compatível com este repositório somente se:

- transmite exclusivamente IDs estendidos de 29 bits;
- normaliza PGN PDU1 e separa DA de PS;
- mantém NAME estável e resolve disputa pelo menor NAME;
- respeita 250 ms antes do tráfego operacional;
- entende Request/Address Claimed e Commanded Address por BAM;
- implementa RTS/CTS, janelas, sequência, EOM_ACK e ABORT para DM16;
- limita TP clássico a 1785 bytes e DM16 cru a 1784;
- mantém DM14/15/16 livres de cabeçalhos proprietários;
- usa `Key/User_Level=FFFF` e `Seed=FFFF` sem tratá-los como sessão;
- fecha ERASE/WRITE conforme DM15 + DM14 Operation Completed;
- usa Proprietary A `0xEF00` para manifest/sessão/CRC/handoff;
- seleciona update por NAME e rota atual, nunca por ordem de resposta do scan;
- rejeita write fora da partição ou originado por outro Tool.

## 17. Referências

Fontes atuais/primárias usadas para enlace, API e gestão:

- [SAE J1939/21_202205 — Data Link Layer](https://saemobilus.sae.org/standards/j193921_202205-data-link-layer)
- [SAE J1939/81_202504 — Network Management](https://saemobilus.sae.org/standards/j193981_202504-network-management)
- [SAE J1939/73_202412 — Application Layer Diagnostics](https://saemobilus.sae.org/standards/j193973_202412-application-layer-diagnostics)
- [SAE J1939DA_202607 — Digital Annex](https://saemobilus.sae.org/standards/j1939da_202607-j1939-digital-annex)
- [Documentação J1939 do kernel Linux](https://docs.kernel.org/networking/j1939.html)
- [Header UAPI J1939 do kernel Linux](https://github.com/torvalds/linux/blob/master/include/uapi/linux/can/j1939.h)
- [Implementação TP do kernel Linux](https://github.com/torvalds/linux/blob/master/net/can/j1939/transport.c)
- [Implementação Address Claim do kernel Linux](https://github.com/torvalds/linux/blob/master/net/can/j1939/address-claim.c)
- [can-utils: exemplos TP e address-claim daemon](https://github.com/linux-can/can-utils)

Fonte histórica pública usada apenas para conferir o perfil DM14/DM15/DM16:

- [UNECE, cópia arquivada de SAE J1939-73 Revised SEP2006](https://unece.org/fileadmin/DAM/trans/doc/2006/wp29/WP29-140-06e.pdf)

Esse material histórico não substitui a revisão SAE licenciada atual. Antes de
qualquer alegação formal de conformidade, o integrador deve adquirir e revisar
J1939/21, J1939/73, J1939/81 e o Digital Annex vigentes, além da camada física
J1939 escolhida.
