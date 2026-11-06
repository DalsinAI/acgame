#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/acgame-ref-test-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/include" \
  "$ROOT/src/c2p_ref.c" \
  "$ROOT/src/ham8.c" \
  "$ROOT/src/platform_aros_aga.c" \
  "$ROOT/tests/test_acgame.c" \
  -o "$OUT"
"$OUT"
