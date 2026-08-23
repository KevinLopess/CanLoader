# Mapa de memória

## STM32F103C8Tx

O target atual possui 64 KiB de Flash em `0x08000000..0x0800FFFF` e 20 KiB de
SRAM em `0x20000000..0x20004FFF`. Todos os intervalos abaixo usam limites
inclusivos; o limite exclusivo é mostrado separadamente para eliminar
ambiguidades.

| Região | Início | Fim inclusivo | Fim exclusivo | Tamanho |
|---|---:|---:|---:|---:|
| Bootloader code | `0x08000000` | `0x08003BFF` | `0x08003C00` | `0x3C00` = 15 KiB |
| Device config | `0x08003C00` | `0x08003FFF` | `0x08004000` | `0x400` = 1 KiB |
| Aplicação | `0x08004000` | `0x0800FBFF` | `0x0800FC00` | `0xBC00` = 47 KiB = 48128 bytes |
| Metadata | `0x0800FC00` | `0x0800FFFF` | `0x08010000` | `0x400` = 1 KiB |
| SRAM | `0x20000000` | `0x20004FFF` | `0x20005000` | `0x5000` = 20 KiB |

```text
Flash
0x08000000  +----------------------------------+
            | Bootloader code                  | 15 KiB
0x08003C00  +----------------------------------+
            | Device config / node ID          | 1 KiB
0x08004000  +----------------------------------+
            | Aplicacao relocada               | 47 KiB
            | vector table em 0x08004000        |
0x0800FC00  +----------------------------------+
            | Metadata                         | 1 KiB / 1 pagina
0x08010000  +----------------------------------+
```

`STM32F103C8TX_FLASH.ld` limita o código do bootloader a 15 KiB, reserva a
página seguinte para configuração e possui asserções para que a partição total
termine exatamente em `0x08004000`. `TestApplication/linker.ld`
origina a aplicação em `0x08004000`, limita-a a `0xBC00` e verifica que seu fim
seja `0x0800FC00`.

## Geometria e proteção da Flash

Para o STM32F103C8Tx configurado:

- erase mínimo: uma página de 1024 bytes;
- programação mínima: halfword de 2 bytes;
- origem da aplicação e metadata alinhadas por página;
- no target F1, o tamanho de bloco anunciado por `START_UPDATE` é 100 bytes,
  múltiplo da unidade de programação; o host usa esse valor negociado;
- um último bloco ímpar é completado com `0xFF` somente para a gravação do
  halfword; o byte de preenchimento não integra `image_size` nem o CRC;
- o erase da aplicação é arredondado para cima até a próxima página, mas
  nunca ultrapassa a região de aplicação.
- a página `DEVICE_CONFIG` pertence à partição do bootloader, não é apagada por
  updates da aplicação e não aparece no `.bin` normal do bootloader.

O port recebe uma região (`APPLICATION` ou `METADATA`) e um offset relativo.
Ele valida overflow, limite, alinhamento e comprimento antes de calcular um
endereço físico. Não existe comando no protocolo que permita ao host escolher um
endereço absoluto. O bootloader e a página de metadata não pertencem à região
gravável como aplicação.

## Configuração persistente do dispositivo

A página em `0x08003C00` contém um registro little-endian de 12 bytes. Quando a
página está apagada, incompleta ou corrompida, o firmware usa o node default 1.

| Offset | Tamanho | Campo | Regra |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | `0x46434C43`, bytes ASCII `CLCF` |
| `4..5` | 2 | `format_version` | `1` |
| `6` | 1 | `node_id` | `1..127` |
| `7` | 1 | `node_id_inv` | complemento bit a bit de `node_id` |
| `8..11` | 4 | `crc32` | CRC-32/ISO-HDLC dos bytes `0..7` |

`SET_NODE_ID` apaga somente essa página, grava e relê o registro antes do ACK.
Uma perda de energia durante a operação pode fazer o target voltar ao node 1;
ela não afeta bootloader, aplicação ou metadata. Um erase total por SWD também
remove a configuração.

Na inicialização, o port F1 lê o registrador de tamanho da Flash e recusa operar
se detectar menos de 64 KiB. Erase e programação são conferidos por readback.

## Vetores da aplicação

Os primeiros oito bytes da imagem são interpretados em little-endian:

| Offset | Campo | Regra |
|---:|---|---|
| `0x00..0x03` | Initial Stack Pointer | `> 0x20000000`, `<= 0x20005000` e alinhado em 8 bytes |
| `0x04..0x07` | Reset Handler | bit Thumb igual a 1; endereço sem o bit Thumb em `[0x08004000, 0x08004000 + image_size)` |

O tamanho mínimo aceito é, por isso, 8 bytes. A validação do Reset Handler usa
o tamanho declarado da imagem, não toda a partição de 47 KiB.

## Formato persistente da metadata

A página começa com um registro little-endian de 30 bytes. O restante permanece
apagado nesta versão.

| Offset | Tamanho | Campo | Valor/regra atual |
|---:|---:|---|---|
| `0..3` | 4 | `magic` | `0x444D4C43`, bytes ASCII `CLMD` na Flash |
| `4..5` | 2 | `format_version` | `1` |
| `6..7` | 2 | `protocol_version` | `1` |
| `8..11` | 4 | `image_size` | `8..0xBC00` |
| `12..15` | 4 | `image_crc32` | CRC-32/ISO-HDLC dos bytes da imagem |
| `16..19` | 4 | `image_version` | `0` nesta versão |
| `20..23` | 4 | `header_crc32` | CRC-32/ISO-HDLC **somente dos bytes `0..19`** |
| `24..25` | 2 | unidade física `update` | marker independente |
| `26..27` | 2 | unidade física `valid` | marker independente |
| `28..29` | 2 | unidade física `invalid` | marker independente |
| `30..1023` | 994 | reservado | permanece `0xFF` |

Cada marker ocupa uma unidade física de programação independente. Uma
unidade apagada contém `FF FF`; uma unidade marcada contém `00 00`. Os valores
lógicos `BOOT_METADATA_STATE_*` do código não são uma palavra de estado gravada
diretamente na página.

| Estado interpretado | `update` | `valid` | `invalid` | Executável? |
|---|---|---|---|---|
| página apagada / `EMPTY` conceitual | `FF FF` | `FF FF` | `FF FF` | não; não há cabeçalho legível |
| `UPDATING` | `00 00` | `FF FF` | `FF FF` | não |
| `VALID` | `00 00` | `00 00` | `FF FF` | somente após nova validação de CRC e vetores |
| `INVALID` | `00 00` | qualquer | `00 00` | não; `invalid` tem precedência |

Combinações parciais que não correspondem a essas regras, magic/versão
incorreta ou erro no CRC do cabeçalho tornam a metadata ilegível e, portanto, a
aplicação inválida.

## Ordem de gravação e falha de energia

O início de uma atualização segue esta ordem:

1. apagar apenas a página de metadata;
2. gravar cabeçalho e marker `update`;
3. reler e confirmar estado `UPDATING`, tamanho e CRC esperados;
4. somente então apagar as páginas cobertas pela nova aplicação;
5. gravar todos os blocos;
6. conferir CRC e vetores;
7. programar a unidade `valid` e reler o estado;
8. executar novamente a validação completa da imagem.

Timeout, `ABORT` ou falha fatal após o erase tentam programar a unidade
`invalid`. Uma perda de energia antes disso ainda deixa a página em `UPDATING`,
parcial ou ilegível; nenhuma dessas condições permite o jump. No próximo reset o
dispositivo permanece em `WAIT_HOST`.

Essa ordem evita executar uma imagem parcial, mas não conserva a imagem antiga:
o layout é single-slot, não A/B. A distinção entre recuperação e rollback é
detalhada em [LIMITATIONS.md](LIMITATIONS.md).
