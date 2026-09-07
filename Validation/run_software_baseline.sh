#!/bin/sh
set -eu

usage() {
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
}

if [ "$#" -ne 1 ]; then
    usage
    exit 2
fi

validation_output=$1
case "$validation_output" in
    ""|/|.|..)
        echo "refusing unsafe output directory: $validation_output" >&2
        exit 2
        ;;
esac

validation_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$validation_output" in
    /*) validation_session=$validation_output ;;
    *) validation_session=$validation_repo/$validation_output ;;
esac

if [ -e "$validation_session" ]; then
    echo "output directory already exists: $validation_session" >&2
    exit 2
fi

mkdir -p "$validation_session"
cd "$validation_repo"

validation_python=${PYTHON:-$validation_repo/.venv/bin/python}
if [ ! -x "$validation_python" ]; then
    echo "Python interpreter is not executable: $validation_python" >&2
    exit 2
fi

validation_started=$(date '+%Y-%m-%dT%H:%M:%S%z')
validation_commit=$(git rev-parse HEAD)

{
    echo "started_at=$validation_started"
    echo "repository=$validation_repo"
    echo "commit=$validation_commit"
    echo "commit_subject=$(git log -1 --format=%s)"
    echo "python=$validation_python"
    echo
    uname -a
    sw_vers 2>/dev/null || true
    echo
    "$validation_python" --version
    "$validation_python" -m pip show canloader python-can pyserial pytest
    echo
    make --version | sed -n '1,2p'
} > "$validation_session/environment.txt"

{
    echo "Commit auditado: $validation_commit"
    echo
    echo "Alterações rastreadas nos diretórios de implementação e testes:"
    git diff --name-status HEAD -- \
        Application Config Core Drivers Portable Protocol TestApplication Tests Tools \
        Targets Makefile
    echo
    echo "Arquivos não rastreados nesses diretórios:"
    git ls-files --others --exclude-standard -- \
        Application Config Core Drivers Portable Protocol TestApplication Tests Tools
} > "$validation_session/source-tree.txt"

{
    echo '$ make -B bootloader test-application'
    make -B bootloader test-application
} > "$validation_session/build.log" 2>&1 || {
    cat "$validation_session/build.log"
    echo "build failed; evidence kept at $validation_session" >&2
    exit 1
}

{
    echo '$ make test PYTHON=<selected interpreter>'
    make test PYTHON="$validation_python"
} > "$validation_session/tests.log" 2>&1 || {
    cat "$validation_session/tests.log"
    echo "tests failed; evidence kept at $validation_session" >&2
    exit 1
}

validation_size_tool=$(awk '/arm-none-eabi-size / { print $1; exit }' "$validation_session/build.log")
if [ -n "$validation_size_tool" ] && [ -x "$validation_size_tool" ]; then
    validation_tool_dir=$(dirname -- "$validation_size_tool")
    {
        "$validation_size_tool" --version | sed -n '1,2p'
        echo
        "$validation_size_tool" build/stm32f103c8/bootloader/CanLoader.elf
        "$validation_size_tool" build/stm32f103c8/test-application/canloader_test_app.elf
    } > "$validation_session/size.txt"
    "$validation_tool_dir/arm-none-eabi-objdump" -h \
        build/stm32f103c8/bootloader/CanLoader.elf > "$validation_session/bootloader-sections.txt"
    "$validation_tool_dir/arm-none-eabi-objdump" -h \
        build/stm32f103c8/test-application/canloader_test_app.elf \
        > "$validation_session/application-sections.txt"
    "$validation_tool_dir/arm-none-eabi-gcc" --version \
        > "$validation_session/toolchain.txt"
else
    echo "arm-none-eabi-size path not found in build log" \
        > "$validation_session/size.txt"
    echo "arm-none-eabi-gcc path not found in build log" \
        > "$validation_session/toolchain.txt"
fi

mkdir -p "$validation_session/artifacts" "$validation_session/maps"
cp build/stm32f103c8/bootloader/CanLoader.bin \
    "$validation_session/artifacts/bootloader.raw"
cp build/stm32f103c8/test-application/canloader_test_app.bin \
    "$validation_session/artifacts/reference-application.raw"
cp build/stm32f103c8/bootloader/CanLoader.map \
    "$validation_session/maps/bootloader.map.txt"
cp build/stm32f103c8/test-application/canloader_test_app.map \
    "$validation_session/maps/test-application.map.txt"

"$validation_python" - "$validation_session/artifacts.json" <<'PY'
from __future__ import annotations

import hashlib
import json
import sys
import zlib
from pathlib import Path

artifacts = [
    (Path("build/stm32f103c8/bootloader/CanLoader.bin"), "artifacts/bootloader.raw"),
    (
        Path("build/stm32f103c8/test-application/canloader_test_app.bin"),
        "artifacts/reference-application.raw",
    ),
]
records = []
for artifact, evidence_path in artifacts:
    data = artifact.read_bytes()
    records.append(
        {
            "path": str(artifact),
            "evidence_path": evidence_path,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "crc32_iso_hdlc": f"0x{zlib.crc32(data) & 0xFFFFFFFF:08X}",
        }
    )
Path(sys.argv[1]).write_text(
    json.dumps({"schema_version": 1, "artifacts": records}, indent=2) + "\n",
    encoding="utf-8",
)
PY

validation_ended=$(date '+%Y-%m-%dT%H:%M:%S%z')
cat > "$validation_session/summary.md" <<EOF
# Baseline de software

| Campo | Valor |
|---|---|
| Início | \`$validation_started\` |
| Fim | \`$validation_ended\` |
| Commit | \`$validation_commit\` |
| Build do bootloader | APROVADO |
| Build da aplicação de teste | APROVADO |
| Seis executáveis C | APROVADO |
| Suíte Python | APROVADO |
| Validação física | NÃO EXECUTADA |

Os logs integrais, os mapas de ligação e cópias dos binários estão nos demais
arquivos desta pasta. Esta sessão comprova somente o baseline de software; não
comprova CAN, Flash ou execução no STM32 físico.
EOF

"$validation_python" Validation/finalize_session.py \
    --session "$validation_session"

echo "software baseline completed: $validation_session"
