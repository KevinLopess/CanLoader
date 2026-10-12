# Ferramenta host CanLoader

Cliente Python 3.10+ exclusivo do protocolo atual do CanLoader sobre J1939. A CLI
usa `python-can`; as camadas de protocolo aceitam um barramento injetado e são
testadas sem hardware CAN.

## Instalação

```bash
python -m pip install -e Tools/canloader
```

A ferramenta não cria interfaces SocketCAN, não executa `sudo` e não configura
o bitrate de `can0`. Nos backends seriais SLCAN e SeeedStudio, ela envia o
bitrate explicitamente ao adaptador; para SeeedStudio também força
`frame_type=EXT`.

## Descoberta e identidade

O host reivindica seu próprio endereço J1939, envia um Request global do PGN
Address Claimed e coleta pares `NAME`/endereço. Depois tenta `IDENTIFY` de forma
dirigida. Só marca um dispositivo como CanLoader quando endereço, discriminador
interno de formato `0x02`, target code suportado e flags J1939/Address Claim/TP
RX são coerentes:

```bash
python -m canloader ls --interface socketcan --channel can0
```

Todo comando dirigido usa `--address`; `--name` é uma trava de identidade
opcional. Mesmo quando `--name` é omitido, o cliente resolve o endereço por
Address Claimed e trava o NAME antes de qualquer operação. A atualização é
interrompida se outro NAME assumir o endereço. Antes de uma operação pública,
o cliente também exige `IDENTIFY` compatível e reutiliza essa validação enquanto
NAME/SA permanecem bloqueados; mudança de rota invalida o cache.

Os target codes aceitos são `0xF1` (STM32F103C8) e `0xB1` (STM32G0B1RE).

Se o Tool já operacional perder seu próprio SA para um NAME numericamente
menor, ele invalida rota/perfil, escolhe um fallback AAC livre, repete Address
Claim e IDENTIFY antes do próximo comando. Uma atualização aberta pelo SA
anterior deve ser reiniciada, pois o owner embarcado não muda no meio da sessão.

```bash
python -m canloader info --channel can0 --address 0x80
python -m canloader info --channel can0 \
  --address 0x80 --name 0x8000000000123456
python -m canloader update application.bin --channel can0 \
  --address 0x80 --name 0x8000000000123456 --run
python -m canloader verify --channel can0 --address 0x80
python -m canloader run --channel can0 --address 0x80
python -m canloader reboot --channel can0 --address 0x80
python -m canloader set-address 0x91 --channel can0 \
  --address 0x80 --name 0x8000000000123456
```

`set-address` usa o PGN Commanded Address, transportado por BAM com o payload
`NAME_LE64 + novo_endereço`; o cliente confirma claim estável, ocupante único e
IDENTIFY na nova rota. Os endereços válidos são `0x00..0xFD`, mas o novo SA
deve ser diferente de `--source-address`; `0xFE` é Null/Cannot Claim e `0xFF`
é global.

O host usa por padrão SA `0xF9` e um NAME de laboratório. Em uma implantação,
configure uma identidade única e válida com `--source-address` e
`--source-name`.

## macOS e SLCAN

Localize a porta serial comparando `ls /dev/cu.*` antes e depois de conectar o
adaptador. Para um CANable/Lawicel com suporte a quadros estendidos:

```bash
python -m canloader ls \
  --interface slcan \
  --channel /dev/cu.usbmodemXXXX \
  --bitrate 500000
```

Se necessário, informe também `--tty-baudrate 115200`. O backend deve suportar
quadros Lawicel `T` (CAN 29 bits); o protocolo atual não possui fallback para IDs
CAN de 11 bits.

Para um adaptador suportado pelo backend `seeedstudio`, troque a interface e
informe a baudrate serial quando necessário, por exemplo
`--interface seeedstudio --tty-baudrate 2000000`. A CLI mantém
`--bitrate 500000` como bitrate CAN e seleciona quadros estendidos.

## Atualização

O fluxo é:

1. Address Claim do host e resolução/travamento do NAME alvo.
2. `IDENTIFY`; se a aplicação estiver ativa, `ENTER_BOOTLOADER` e nova
   confirmação de Address Claimed para o mesmo NAME.
3. Manifesto em Proprietary A (`START_UPDATE` com tamanho e `SET_IMAGE_CRC`).
4. consulta da unidade de apagamento por INFO, seguida de `DM14 ERASE`,
   `DM15 PROCEED/COMPLETED` e fechamento DM14;
5. para cada bloco anunciado pelo target (100 B no F103 e 96 B no G0),
   `DM14 WRITE`, DM16 e confirmação DM15.
   DM16 com mais de 7 bytes usa TP.RTS/CTS; firmware nunca usa BAM.
6. `FINALIZE`, `VERIFY` e, com `--run`, `RUN_APPLICATION`.

O `EOM_ACK` do transporte confirma apenas a remontagem do payload. O cliente só
avança depois do `DM15 OPERATION_COMPLETED`, que confirma o commit em Flash. O
CRC é CRC-32/ISO-HDLC, compatível com `zlib.crc32`.

As opções principais são `--timeout`, `--transport-timeout`,
`--erase-timeout`, `--scan-timeout`, `--claim-timeout`, `--retries` e
`--verbose`. `--retries` também cobre a retransmissão integral do BAM de
Commanded Address quando claim/IDENTIFY não confirmam a mudança. O cliente
aguarda `transport_timeout` antes de sobrepor um BAM parcial. O firmware deve
conter ao menos SP inicial e vetor de reset
(8 bytes) e caber em `APP_MAX_SIZE`. A ferramenta não presume o mapa do MCU:
ela lê `APP_BASE`, `APP_MAX_SIZE` e `FLASH_ERASE_UNIT_SIZE` via `INFO`.

## Testes

```bash
python -m pip install -e 'Tools/canloader[test]'
python -m pytest -q Tests/python
```

A suíte cobre vetores de ID/NAME, Address Claim e arbitragem, TP/BAM/RTS-CTS,
DM14/DM15/DM16 byte a byte, identidade do alvo, duas ECUs simultâneas, CLI e
adaptação `python-can`/SLCAN.

## Atividade CAN da aplicação de teste

O comando `canloader activity` consulta os contadores opcionais da aplicação de
teste atualizada. Aceita as mesmas opções de interface, canal, SA e NAME de
`info`. Por exemplo:

```sh
canloader activity --interface socketcan --channel can0 \
  --address 0x80 --name 0x8000000000123456
```

Mostra mensagens enfileiradas, recepções válidas de outros dispositivos,
tentativas adiadas, última origem e indicadores de erro do controlador CAN.
Enfileiramento não confirma entrega. Os contadores reiniciam com a aplicação
e são consultados sequencialmente. O bootloader e aplicações antigas não
implementam essa extensão. Consulte o [roteiro da aplicação de teste](../../TestApplication/README.md#comunicação-periódica-e-diagnóstico)
para gravar as imagens, interpretar os LEDs e ensaiar atualização com tráfego.
