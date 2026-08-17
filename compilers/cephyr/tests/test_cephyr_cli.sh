#!/bin/sh
set -eu

cephyr=$1
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/cephyr-cli.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cat >"$tmpdir/main.c" <<'EOF'
int main(void) { return CLI_VALUE; }
EOF

"$cephyr" -E \
  -Wp,-DCLI_VALUE=31 \
  -Wa,--32 \
  -Wl,--as-needed \
  -Xpreprocessor -undef \
  -Xassembler --64 \
  -Xlinker --eh-frame-hdr \
  -L"$tmpdir" -lfixture -PIC -PIE -shared \
  -o "$tmpdir/preprocessed.c" "$tmpdir/main.c"
grep -q '31' "$tmpdir/preprocessed.c"

"$cephyr" -s -o "$tmpdir/assembler.ir" "$tmpdir/main.c"
grep -q '(module ' "$tmpdir/assembler.ir"
