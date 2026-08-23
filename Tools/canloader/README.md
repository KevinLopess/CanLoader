# CanLoader host tool

Python 3.10+ updater for the CanLoader protocol. The command-line interface
uses `python-can`; its protocol/client layers accept an injected bus and can be
tested without CAN hardware.

## Install

```bash
python -m pip install -e Tools/canloader
```

The tool never creates or configures a Linux CAN interface and never invokes
`sudo`. Configure `can0` separately before using the default SocketCAN backend.

## Use

```bash
python -m canloader ls --interface socketcan --channel can0
python -m canloader info --interface socketcan --channel can0 --node 1
python -m canloader update application.bin --channel can0
python -m canloader update application.bin --channel can0 --run
python -m canloader verify --channel can0
python -m canloader run --channel can0
python -m canloader reboot --channel can0 --node 1
python -m canloader set-node 7 --channel can0 --node 1
```

`ls` envia quatro pedidos broadcast `PROBE` não invasivos e mostra node,
target, protocolo, modo de execução, validade da aplicação e UID STM32 de 96
bits. `reboot` depende do contrato de controle no lado da aplicação; a aplicação
de teste incluída o implementa. `set-node NEW_NODE` endereça o `--node` atual,
persiste o novo endereço, reinicia e confirma o node depois do reset.

Every node command accepts `--interface`, `--channel`, `--node`, `--timeout`,
`--erase-timeout`, `--retries`, and `--verbose`. `--retries` is the number of
retransmissions after the initial attempt. Node IDs are in the range 1..127.
Firmware files must contain at least the initial stack pointer and reset vector
(8 bytes) and must fit the `APP_MAX_SIZE` reported by the target.

`ls` não recebe `--node`; use `--scan-timeout` para definir a janela de coleta
de cada seletor. Em macOS com um CANable usando firmware SLCAN:

```bash
CAN_BITRATE=500000 python -m canloader ls \
  --interface slcan --channel /dev/cu.usbmodemXXXX
```
The erase timeout defaults to 4 seconds, below the target's 5-second inactive
transfer timeout so a lost ERASE ACK can be retried while the session is valid.

The update flow is `DISCOVER`, `INFO(APP_MAX_SIZE)`, `START_UPDATE`,
`SET_IMAGE_CRC`, `ERASE`, block transfer, `FINISH`, and `VERIFY`, followed by
`RUN_APPLICATION` when `--run` is supplied. CRC values use CRC-32/ISO-HDLC, as
implemented by `zlib.crc32`. The client uses the block size returned by
`START_UPDATE` (100 bytes on the STM32F1 target) and gives each stateless query
a distinct correlation session so a delayed INFO ACK cannot satisfy the next
selector.

## Tests

The tests use an injected in-memory bus and require neither `python-can` nor CAN
hardware when run directly from the repository:

```bash
python -m pip install pytest
python -m pytest Tests/python
```
