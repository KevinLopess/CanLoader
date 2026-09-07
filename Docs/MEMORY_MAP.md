# Mapa de memória e formatos persistentes

## 1. Premissas dos targets

Esta revisão contém dois mapas explícitos:

| Propriedade | STM32F103C8Tx | STM32G0B1RE |
|---|---:|---:|
| Flash interna | 64 KiB | 512 KiB |
| SRAM | 20 KiB | 144 KiB |
| unidade de erase | 1024 B | 2048 B |
| unidade de programação | 2 B | 8 B |
| base da aplicação | `0x08005000` | `0x08008000` |
| tamanho máximo da aplicação | 44032 B | 489472 B |

Ambos usam:

- aplicação *single-slot*;
- uma página separada para configuração do dispositivo;
- uma página final separada para metadata da imagem.

Os intervalos abaixo são parte do ABI entre linker, bootloader, aplicação,
host e testes. Alterá-los exige rebuild coordenado de todos esses componentes.

## 2. Particionamento da Flash do STM32F103C8Tx

| Região | Início | Fim inclusivo | Fim exclusivo | Tamanho |
|---|---:|---:|---:|---:|
| código do bootloader | `0x08000000` | `0x08004BFF` | `0x08004C00` | `0x4C00` = 19 KiB = 19456 B |
| configuração do dispositivo | `0x08004C00` | `0x08004FFF` | `0x08005000` | `0x400` = 1 KiB |
| aplicação | `0x08005000` | `0x0800FBFF` | `0x0800FC00` | `0xAC00` = 43 KiB = 44032 B |
| metadata | `0x0800FC00` | `0x0800FFFF` | `0x08010000` | `0x400` = 1 KiB |

```text
0x08000000  +------------------------------------+
            | bootloader: vetores, código, const | 19 KiB
0x08004C00  +------------------------------------+
            | journal NAME/preferred SA          | 1 KiB
0x08005000  +------------------------------------+
            | aplicação relocada                 | 43 KiB
            | vector table em 0x08005000          |
0x0800FC00  +------------------------------------+
            | metadata da imagem                 | 1 KiB
0x08010000  +------------------------------------+
```

`BOOTLOADER_FLASH_SIZE=0x5000` descreve a partição total reservada ao
bootloader; ela é a soma de `BOOTLOADER_CODE_SIZE=0x4C00` e uma página de
configuração. O protocolo nunca oferece essa área como memória gravável.

`Targets/stm32f103c8/STM32F103C8TX_BOOT_FLASH.ld` expõe 19 KiB como `FLASH` e reserva
`DEVICE_CONFIG` em `0x08004C00`. `TestApplication/Targets/stm32f103c8/linker.ld` origina a
aplicação em `0x08005000`, limita-a a `0xAC00` e afirma que seu limite exclusivo
é `0x0800FC00`.

### 2.1 Por que o bootloader passou a ocupar 20 KiB

O v1 reservava 16 KiB no total, com aplicação em `0x08004000`. A integração v2
adicionou framing de 29 bits, Address Claim, NAME, TP, DM14/15/16, journal de
configuração e filtros estendidos. O binário deixou de caber com margem segura
nos 15 KiB de código anteriores. A partição foi então redimensionada para
20 KiB: 19 KiB de código e 1 KiB de configuração.

A medição integrada atual é:

| Seção | Bytes |
|---|---:|
| `.text` e dados constantes reportados como text | 16456 |
| `.data` em Flash/RAM | 12 |
| `.bss` em RAM | 1648 |
| ocupação Flash considerada (`text+data`) | 16468 |
| capacidade da região de código | 19456 |
| margem Flash | 2988 |

Essa medição é informativa, não um limite congelado. O relatório de tamanho e
o `.map` precisam ser arquivados novamente em cada build de liberação.

### 2.2 Particionamento do STM32G0B1RE

| Região | Início | Fim inclusivo | Fim exclusivo | Tamanho |
|---|---:|---:|---:|---:|
| código do bootloader | `0x08000000` | `0x080077FF` | `0x08007800` | `0x7800` = 30 KiB |
| configuração do dispositivo | `0x08007800` | `0x08007FFF` | `0x08008000` | `0x800` = 2 KiB |
| aplicação | `0x08008000` | `0x0807F7FF` | `0x0807F800` | `0x77800` = 478 KiB = 489472 B |
| metadata | `0x0807F800` | `0x0807FFFF` | `0x08080000` | `0x800` = 2 KiB |

`Targets/stm32g0b1re/STM32G0B1RETX_BOOT_FLASH.ld` limita o código a
30 KiB. `TestApplication/Targets/stm32g0b1re/linker.ld` origina a aplicação em
`0x08008000` e termina antes da última página. O port exige o mapeamento padrão
dos bancos (`nSWAP_BANK=1`) e divide apagamentos que cruzem `0x08040000`.

## 3. Proteções de acesso

A interface `boot_flash_*` recebe uma enumeração de região e offset relativo,
não um endereço físico fornecido pelo barramento. Cada port resolve
`APPLICATION`, `METADATA` e `DEVICE_CONFIG` para os intervalos do seu target.
Em ambos:

- offset, tamanho e subtrações são validados antes de calcular o endereço;
- erase exige offset e tamanho múltiplos da unidade do target;
- write exige offset e tamanho múltiplos da unidade de programação;
- erase e programação são verificados por leitura posterior.

DM14 usa endereço direto de 32 bits, mas `bootloader.c` só aceita o próximo
endereço esperado da aplicação. O host não consegue usar DM14 para escrever o
bootloader, a configuração ou a metadata.

O MCU também é conferido em runtime: cada port lê o registrador de tamanho da
Flash e recusa uma capacidade menor que a configurada. O G0 recusa também o
bank swap, pois seu cálculo de banco pressupõe o mapa padrão.

## 4. ERASE e WRITE

No perfil v2, `DM14.length` muda de unidade conforme a operação:

- em ERASE, significa quantidade de unidades de apagamento;
- em WRITE, significa quantidade de bytes crus do bloco DM16.

Para uma imagem de `image_size` bytes:

```text
erase_unit = INFO(FLASH_ERASE_UNIT_SIZE)
page_count = ceil(image_size / erase_unit)
erase_range = [APP_BASE, APP_BASE + page_count*erase_unit)
```

No F103, a unidade vale 1024 B e o máximo ocupa 43 páginas. No G0, ela vale
2048 B e o máximo ocupa 239 páginas. Um ERASE `length=2048` pediria 2048
unidades, não 2048 bytes, e deve ser rejeitado.

WRITE avança sequencialmente pelo bloco anunciado em `START_UPDATE`: 100 B no
F103 e 96 B no G0. O último bloco pode ser menor. O bootloader acrescenta
`0xFF` apenas no buffer físico para completar a unidade de 2 ou 8 B. Esse
padding não integra `image_size`, DM16 nem CRC.

## 5. Vetores da aplicação

Os primeiros oito bytes do binário são little-endian:

| Offset | Campo | Validação |
|---:|---|---|
| `0x00..0x03` | MSP inicial | dentro da SRAM do target e alinhado em 8 bytes |
| `0x04..0x07` | Reset Handler | bit Thumb igual a 1 e endereço físico dentro de `[APP_BASE, APP_BASE+image_size)` |

O tamanho mínimo da imagem é, portanto, 8 bytes. A vector table deve estar no
`APP_BASE` do target (`0x08005000` ou `0x08008000`). No handoff, o bootloader
grava esse endereço em `SCB->VTOR`, carrega MSP e salta para o Reset Handler
após limpar o estado de interrupções.

## 6. Journal persistente de NAME e preferred SA

A página de configuração é um journal append-only de registros de 32 bytes. O
F103 dispõe de 32 slots em `0x08004C00`; o G0 dispõe de 64 em `0x08007800`.
Quando todos os slots são usados, a página é apagada e reinicia no slot zero.

### 6.1 Registro v2

| Offset | Tamanho | Campo | Regra |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | `0x46434C43`, bytes `43 4C 43 46` (`CLCF`) |
| `4..5` | 2 | `format_version` | `2` |
| `6..7` | 2 | `record_size` | `32` |
| `8..11` | 4 | `generation` | contador monotônico modular, zero reservado ao default em RAM |
| `12..19` | 8 | `name` | NAME J1939 little-endian; diferente de 0 e `UINT64_MAX` |
| `20` | 1 | `preferred_address` | `0x00..0xFD` |
| `21` | 1 | `preferred_address_inv` | complemento bit a bit do byte anterior |
| `22..23` | 2 | `flags` | bit 0: NAME provisionado; demais reservados |
| `24..27` | 4 | `crc32` | CRC-32/ISO-HDLC dos bytes `0..23` |
| `28..29` | 2 | `commit` | `00 00`, gravado por último |
| `30..31` | 2 | reservado | permanece `FF FF` |

Um registro só é válido se magic, versão, tamanho, endereço/complemento, NAME,
CRC e commit forem válidos. O scanner escolhe a geração válida mais nova usando
comparação modular assinada. Slots parciais são ignorados.

### 6.2 Commit e falha de energia

A gravação segue:

1. localizar o primeiro slot apagado e a geração válida mais nova;
2. se a página estiver cheia, apagá-la;
3. montar o registro com `commit=FFFF`;
4. gravar as unidades anteriores àquela que contém o commit;
5. programar por último a unidade que contém `commit=0000`;
6. reler o journal e conferir todos os campos.

No F103, o passo 4 grava `0..27` e o passo 5 grava os 2 bytes do commit. No G0,
o passo 4 grava `0..23` e o passo 5 grava a unidade `24..31`, que contém CRC,
commit e reserva. Uma queda antes do passo 5 não promove o registro parcial.
Uma queda durante o erase da página cheia pode eliminar a preferência anterior; no boot seguinte o
firmware recupera o default derivado do UID e `0x80`. Portanto o journal reduz
desgaste e rejeita registros incompletos, mas não é uma redundância de duas
páginas.

### 6.3 Defaults de desenvolvimento

Sem registro válido:

- `preferred_address = 0x80`;
- `Identity Number = CRC32(UID96) & 0x1FFFFF`, substituído por 1 se resultar 0;
- `Manufacturer Code = 0`;
- demais campos de função/sistema = 0;
- `AAC = 1`.

O resultado é estável por MCU, mas não é um NAME de produção globalmente
registrado. `Manufacturer Code=0` é deliberadamente uma configuração de
laboratório. Produção precisa provisionar campos coerentes com as atribuições
SAE e garantir NAME único.

## 7. Metadata da imagem

A página de metadata começa em `0x0800FC00` no F103 e `0x0807F800` no G0. O
cabeçalho comum ocupa 24 bytes; os markers seguintes ocupam uma unidade de
programação independente.

| Offset | Tamanho | Campo | Valor/regra |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | `0x444D4C43`, bytes `43 4C 4D 44` (`CLMD`) |
| `4..5` | 2 | `format_version` | `1` |
| `6..7` | 2 | `protocol_version` | `2` |
| `8..11` | 4 | `image_size` | `8..BOOT_APPLICATION_MAX_SIZE` |
| `12..15` | 4 | `image_crc32` | CRC-32/ISO-HDLC da imagem |
| `16..19` | 4 | `image_version` | `0`, reservado para evolução |
| `20..23` | 4 | `header_crc32` | CRC-32/ISO-HDLC dos bytes `0..19` |
| a partir de `24` | 2 ou 8 | marker `update` | todos `00` quando iniciado |
| unidade seguinte | 2 ou 8 | marker `valid` | todos `00` quando promovido |
| unidade seguinte | 2 ou 8 | marker `invalid` | todos `00` quando invalidado |

O registro serializado ocupa 30 bytes no F103 e 48 bytes no G0. Estado apagado
é a unidade inteira em `FF`; estado marcado é a unidade inteira em `00`.

| Estado interpretado | update | valid | invalid | Executável? |
|---|---|---|---|---|
| vazio/ilegível | qualquer combinação inválida | | | não |
| `UPDATING` | marcado | apagado | apagado | não |
| `VALID` | marcado | marcado | apagado | somente após revalidar imagem |
| `INVALID` | marcado | qualquer | marcado | não |

`invalid` tem precedência. Magic, versão, protocolo, tamanho ou CRC de cabeçalho
inválidos também tornam a imagem não executável.

## 8. Ordem transacional do update

1. `START_UPDATE` aceita tamanho e sessão somente em RAM;
2. `SET_IMAGE_CRC` aceita o CRC esperado somente em RAM;
3. ao executar ERASE, o bootloader apaga a página de metadata;
4. grava cabeçalho e marker `update` e relê `UPDATING`;
5. somente depois apaga as páginas da aplicação;
6. grava e confere os blocos sequenciais;
7. `FINALIZE` recalcula CRC e valida vetores;
8. programa o marker `valid` e relê `VALID`;
9. `VERIFY` repete a validação completa antes de liberar o handoff.

Uma queda entre os passos 3 e 8 deixa metadata `UPDATING`, parcial ou ilegível;
nenhuma dessas condições permite jump. Esse comportamento garante recuperação
via CAN, mas não rollback: a imagem antiga foi perdida assim que o erase da
aplicação começou.

## 9. Checklist ao alterar o mapa

Qualquer mudança precisa manter coerentes:

- constantes e asserts de `Config/boot_config.h`;
- linker do bootloader e header em `Config/targets/`;
- linker e vector table da aplicação;
- limites do port de Flash;
- seletores INFO `APP_BASE`, `APP_MAX_SIZE` e `FLASH_ERASE_UNIT_SIZE`;
- page count de DM14 ERASE;
- testes C e Python de boundary/overflow;
- metadata `protocol_version` e estratégia de migração;
- documentação, comandos de gravação e evidência de tamanho.

Nunca aumente a região de código apenas no linker: isso pode sobrescrever a
configuração ou deslocar a aplicação sem que o host e a validação saibam.
