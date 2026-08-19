#!/bin/sh
set -eu

cephyr=$1
ccwas=$2
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/cephyr-assembly.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cat >"$tmpdir/plain.s" <<'EOF'
.text
.global _main
_main:
  ret
EOF

cat >"$tmpdir/preprocessed.S" <<'EOF'
#define UNUSED_ASSEMBLY_MACRO 1
.text
.global _main
_main:
  ret
EOF

cat >"$tmpdir/custom.asmx" <<'EOF'
.text
.global _main
_main:
  ret
EOF

"$cephyr" -c -o "$tmpdir/plain.o" "$tmpdir/plain.s"
"$cephyr" -E -o "$tmpdir/preprocessed.s" "$tmpdir/preprocessed.S"
grep -q '^\.text$' "$tmpdir/preprocessed.s"
"$cephyr" -c -o "$tmpdir/preprocessed.o" "$tmpdir/preprocessed.S"

CEPHYR_AS_EXTENSIONS=.asmx \
CEPHYR_AS="$ccwas --target=x86-64" \
"$cephyr" -c -o "$tmpdir/custom.o" "$tmpdir/custom.asmx"

"$cephyr" -o "$tmpdir/linked" "$tmpdir/plain.s"
grep -a -q '_main' "$tmpdir/linked"
