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

help=$("$cephyr" --help)
printf '%s\n' "$help" | grep -q '^  -o <file>      Output file'
printf '%s\n' "$help" | grep -q "^  -c             Compile only (don't link)$"
if printf '%s\n' "$help" | grep -q '^  -o <file>      Write output and stop before linking'; then
  echo "cephyr: compile-only help entry must use -c" >&2
  exit 1
fi
