#!/bin/sh
set -eu
ccwas="$1"
tmp="${TMPDIR:-/tmp}/ccwas-test-$$"
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp"
cat >"$tmp/in.s" <<'EOF'
.text
.global f
f:
  mov rax, rbx
  add rax, rcx
  ret
EOF
"$ccwas" --target=x86-64 --keep-expanded="$tmp/expanded.s" "$tmp/in.s" -o "$tmp/a.o"
test "$(od -An -tx1 -j64 -N7 "$tmp/a.o" | tr -d ' \n')" = "4889d84801c8c3"
test "$(readelf -h "$tmp/a.o" | grep -c 'Type:[[:space:]]*REL')" -eq 1
cat >"$tmp/template.s" <<'EOF'
.text
<?lua= "nop\n" ?>ret
EOF
"$ccwas" --target=x86-64 "$tmp/template.s" -o "$tmp/t.o"
test "$(od -An -tx1 -j64 -N2 "$tmp/t.o" | tr -d ' \n')" = "90c3"
