#!/bin/sh

set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT_ROOT"

COMMON_PATHS="Application Protocol Portable/Inc Config"
FORBIDDEN_PATTERN='#include[[:space:]]+[<"](stm32|main\.h|core_cm)|(^|[^[:alnum:]_])(HAL_|LL_|USE_HAL_DRIVER)'

violations=$(grep -R -n -E \
    --include='*.c' \
    --include='*.h' \
    "$FORBIDDEN_PATTERN" \
    $COMMON_PATHS 2>/dev/null || true)

if [ -n "$violations" ]; then
    echo "Portability boundary violation:" >&2
    echo "$violations" >&2
    echo "HAL/CMSIS device access belongs in target initialization or Portable/<target>." >&2
    exit 1
fi

echo "check_portability: OK (common core has no STM32 HAL/LL/device dependency)"
