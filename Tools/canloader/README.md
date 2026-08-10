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
python -m canloader info --interface socketcan --channel can0 --node 1
python -m canloader update application.bin --channel can0
python -m canloader update application.bin --channel can0 --run
python -m canloader verify --channel can0
python -m canloader run --channel can0
```

Every command accepts `--interface`, `--channel`, `--node`, `--timeout`,
`--erase-timeout`, `--retries`, and `--verbose`. `--retries` is the number of
retransmissions after the initial attempt. Node IDs are in the range 1..127.
Firmware files must contain at least the initial stack pointer and reset vector
(8 bytes) and must fit the `APP_MAX_SIZE` reported by the target.
The erase timeout defaults to 4 seconds, below the target's 5-second inactive
transfer timeout so a lost ERASE ACK can be retried while the session is valid.

The update flow is `DISCOVER`, `INFO(APP_MAX_SIZE)`, `START_UPDATE`,
`SET_IMAGE_CRC`, `ERASE`, block transfer, `FINISH`, and `VERIFY`, followed by
`RUN_APPLICATION` when `--run` is supplied. CRC values use CRC-32/ISO-HDLC, as
implemented by `zlib.crc32`.

## Tests

The tests use an injected in-memory bus and require neither `python-can` nor CAN
hardware when run directly from the repository:

```bash
python -m pip install pytest
python -m pytest Tests/python
```
