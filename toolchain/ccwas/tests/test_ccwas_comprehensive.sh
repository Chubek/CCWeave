#!/bin/sh
# Comprehensive test suite for ccwas
set -eu
CCWAS="${1:-./build/ccwas}"
TMP="${TMPDIR:-/tmp}/ccwas-test-$$"
PASS=0
FAIL=0

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
mkdir "$TMP"

# Helper: run a test
check() {
  local name="$1" expected="$2" actual="$3"
  if [ "$expected" = "$actual" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: $name"
  else
    FAIL=$((FAIL + 1))
    echo "  FAIL: $name"
    echo "    expected: $expected"
    echo "    actual:   $actual"
  fi
}

check_contains() {
  local name="$1" pattern="$2" file="$3"
  if grep -q "$pattern" "$file" 2>/dev/null; then
    PASS=$((PASS + 1))
    echo "  PASS: $name"
  else
    FAIL=$((FAIL + 1))
    echo "  FAIL: $name (pattern '$pattern' not found in $file)"
  fi
}

check_command_contains() {
  name="$1"
  pattern="$2"
  shift 2
  if "$@" 2>/dev/null | grep -q "$pattern"; then
    PASS=$((PASS + 1))
    echo "  PASS: $name"
  else
    FAIL=$((FAIL + 1))
    echo "  FAIL: $name (pattern '$pattern' not found)"
  fi
}

check_command_not_contains() {
  name="$1"
  pattern="$2"
  shift 2
  if "$@" 2>/dev/null | grep -q "$pattern"; then
    FAIL=$((FAIL + 1))
    echo "  FAIL: $name (unexpected pattern '$pattern')"
  else
    PASS=$((PASS + 1))
    echo "  PASS: $name"
  fi
}

assemble() {
  local src="$1" out="$2"
  "$CCWAS" --target=x86-64 "$src" -o "$out" 2>/dev/null
}

assemble_fail() {
  local src="$1"
  if "$CCWAS" --target=x86-64 "$src" -o /dev/null 2>/dev/null; then
    return 1
  fi
  return 0
}

echo "=== ccwas comprehensive tests ==="

# --- Test 1: Basic mov/add/ret ---
cat > "$TMP/t1.s" << 'EOF'
.text
.global f
f:
  mov rax, rbx
  add rax, rcx
  ret
EOF
assemble "$TMP/t1.s" "$TMP/t1.o"
check "basic mov/add/ret" "4889d84801c8c3" "$(od -An -tx1 -j64 -N7 "$TMP/t1.o" | tr -d ' \n')"

# --- Test 2: push/pop ---
cat > "$TMP/t2.s" << 'EOF'
.text
.global g
g:
  push rax
  push rbx
  pop rbx
  pop rax
  ret
EOF
assemble "$TMP/t2.s" "$TMP/t2.o"
check "push/pop sequence" "5053" "$(od -An -tx1 -j64 -N2 "$TMP/t2.o" | tr -d ' \n')"

# --- Test 3: nop ---
cat > "$TMP/t3.s" << 'EOF'
.text
.global h
h:
  nop
  nop
  ret
EOF
assemble "$TMP/t3.s" "$TMP/t3.o"
check "nop encoding" "9090" "$(od -An -tx1 -j64 -N2 "$TMP/t3.o" | tr -d ' \n')"

# --- Test 4: xor ---
cat > "$TMP/t4.s" << 'EOF'
.text
.global xorfunc
xorfunc:
  xor rax, rax
  ret
EOF
assemble "$TMP/t4.s" "$TMP/t4.o"
check "xor rax,rax" "4831c0" "$(od -An -tx1 -j64 -N3 "$TMP/t4.o" | tr -d ' \n')"

# --- Test 5: cmp ---
cat > "$TMP/t5.s" << 'EOF'
.text
.global cmpfunc
cmpfunc:
  cmp rax, rbx
  ret
EOF
assemble "$TMP/t5.s" "$TMP/t5.o"
check "cmp rax,rbx" "4839d8" "$(od -An -tx1 -j64 -N3 "$TMP/t5.o" | tr -d ' \n')"

# --- Test 6: mov r64, imm32 ---
cat > "$TMP/t6.s" << 'EOF'
.text
.global constfunc
constfunc:
  mov rax, 42
  ret
EOF
assemble "$TMP/t6.s" "$TMP/t6.o"
check "mov rax,42" "48b82a00000000000000" "$(od -An -tx1 -j64 -N10 "$TMP/t6.o" | tr -d ' \n')"

# --- Test 7: and/or ---
cat > "$TMP/t7.s" << 'EOF'
.text
.global bitfunc
bitfunc:
  and rax, rbx
  or rax, rcx
  ret
EOF
assemble "$TMP/t7.s" "$TMP/t7.o"
check "and/or sequence" "4821d84809c8" "$(od -An -tx1 -j64 -N6 "$TMP/t7.o" | tr -d ' \n')"

# --- Test 8: sub ---
cat > "$TMP/t8.s" << 'EOF'
.text
.global subfunc
subfunc:
  sub rax, rbx
  ret
EOF
assemble "$TMP/t8.s" "$TMP/t8.o"
check "sub rax,rbx" "4829d8" "$(od -An -tx1 -j64 -N3 "$TMP/t8.o" | tr -d ' \n')"

# --- Test 9: .byte directive ---
cat > "$TMP/t9.s" << 'EOF'
.text
.global dat
dat:
  .byte 0xde, 0xad, 0xbe, 0xef
  ret
EOF
assemble "$TMP/t9.s" "$TMP/t9.o"
check "byte directive" "deadbeefc3" "$(od -An -tx1 -j64 -N5 "$TMP/t9.o" | tr -d ' \n')"

# --- Test 10: .4byte directive ---
cat > "$TMP/t10.s" << 'EOF'
.text
.global four
four:
  .4byte 0x12345678
  ret
EOF
assemble "$TMP/t10.s" "$TMP/t10.o"
check "4byte directive" "78563412" "$(od -An -tx1 -j64 -N4 "$TMP/t10.o" | tr -d ' \n')"

# --- Test 11: ELF header ---
check_contains "ELF header magic" "ELF" "$TMP/t1.o"
check_command_contains "ELF relocatable" "REL (Relocatable file)" readelf -h "$TMP/t1.o"

# --- Test 12: Template pass ---
cat > "$TMP/t12.s" << 'EOF'
.text
<?lua= "nop\n" ?>ret
EOF
assemble "$TMP/t12.s" "$TMP/t12.o"
check "template nop+ret" "90c3" "$(od -An -tx1 -j64 -N2 "$TMP/t12.o" | tr -d ' \n')"

# --- Test 13: Template with variables ---
cat > "$TMP/t13.s" << 'EOF'
.text
.global f
f:
<?lua
  for i = 1, 3 do
    ccwas.emitln("nop")
  end
?>ret
EOF
assemble "$TMP/t13.s" "$TMP/t13.o"
check "template loop" "909090c3" "$(od -An -tx1 -j64 -N4 "$TMP/t13.o" | tr -d ' \n')"

# --- Test 14: .global directive ---
cat > "$TMP/t14.s" << 'EOF'
.text
.global exported
exported:
  mov rax, 1
  ret
EOF
assemble "$TMP/t14.s" "$TMP/t14.o"
check_contains "global symbol" "exported" "$TMP/t14.o"

# --- Test 15: Unknown instruction (negative) ---
cat > "$TMP/t15.s" << 'EOF'
.text
.global bad
bad:
  foobar rax, rbx
  ret
EOF
if assemble_fail "$TMP/t15.s"; then
  PASS=$((PASS + 1))
  echo "  PASS: unknown instruction rejected"
else
  FAIL=$((FAIL + 1))
  echo "  FAIL: unknown instruction should be rejected"
fi

# --- Test 16: lea instruction ---
cat > "$TMP/t16.s" << 'EOF'
.text
.global leafunc
leafunc:
  lea rax, [rbx + 8]
  ret
EOF
assemble "$TMP/t16.s" "$TMP/t16.o"
# lea rax, [rbx+8] = REX.W 8D 43 08
check "lea [rbx+8]" "488d4308" "$(od -An -tx1 -j64 -N4 "$TMP/t16.o" | tr -d ' \n')"

# --- Test 17: test instruction ---
cat > "$TMP/t17.s" << 'EOF'
.text
.global testfunc
testfunc:
  test rax, rax
  ret
EOF
assemble "$TMP/t17.s" "$TMP/t17.o"
check "test rax,rax" "4885c0" "$(od -An -tx1 -j64 -N3 "$TMP/t17.o" | tr -d ' \n')"

# --- Test 18: imul r,r ---
cat > "$TMP/t18.s" << 'EOF'
.text
.global mulfunc
mulfunc:
  imul rax, rbx
  ret
EOF
assemble "$TMP/t18.s" "$TMP/t18.o"
check "imul rax,rbx" "480fafc3" "$(od -An -tx1 -j64 -N4 "$TMP/t18.o" | tr -d ' \n')"

# --- Test 19: int3 ---
cat > "$TMP/t19.s" << 'EOF'
.text
.global bkpt
bkpt:
  int3
  ret
EOF
assemble "$TMP/t19.s" "$TMP/t19.o"
check "int3" "ccc3" "$(od -An -tx1 -j64 -N2 "$TMP/t19.o" | tr -d ' \n')"

# --- Test 20: inc/dec ---
cat > "$TMP/t20.s" << 'EOF'
.text
.global incfunc
incfunc:
  inc rax
  dec rbx
  ret
EOF
assemble "$TMP/t20.s" "$TMP/t20.o"
check "inc/dec" "48ffc048ffcb" "$(od -An -tx1 -j64 -N6 "$TMP/t20.o" | tr -d ' \n')"

# --- Test 21: .align ---
cat > "$TMP/t21.s" << 'EOF'
.text
.global alfunc
alfunc:
  nop
  .align 16
  ret
EOF
assemble "$TMP/t21.s" "$TMP/t21.o"
check "align 16 uses code NOPs" "90909090909090909090909090909090c3" \
  "$(od -An -tx1 -j64 -N17 "$TMP/t21.o" | tr -d ' \n')"

# --- Test 22: .text/.data switching ---
cat > "$TMP/t22.s" << 'EOF'
.text
.global mixed
mixed:
  nop
.data
  .byte 0x42
.text
  ret
EOF
assemble "$TMP/t22.s" "$TMP/t22.o"
check_contains "data section" ".data" "$TMP/t22.o"

# --- Test 23: Multiple sections ---
cat > "$TMP/t23.s" << 'EOF'
.text
.global start
start:
  nop
  ret
.data
  .byte 0x01, 0x02, 0x03, 0x04
EOF
assemble "$TMP/t23.s" "$TMP/t23.o"
check_command_contains "has data section" "\\.data" readelf -S "$TMP/t23.o"

# --- Test 24: r8-r15 registers ---
cat > "$TMP/t24.s" << 'EOF'
.text
.global hi
hi:
  mov r8, r9
  ret
EOF
assemble "$TMP/t24.s" "$TMP/t24.o"
check "r8/r9 mov" "4d89c8" "$(od -An -tx1 -j64 -N3 "$TMP/t24.o" | tr -d ' \n')"

# --- Test 25: shl by immediate ---
cat > "$TMP/t25.s" << 'EOF'
.text
.global shlfunc
shlfunc:
  shl rax, 3
  ret
EOF
assemble "$TMP/t25.s" "$TMP/t25.o"
check "shl rax,3" "48c1e003" "$(od -An -tx1 -j64 -N4 "$TMP/t25.o" | tr -d ' \n')"

# --- Test 26: shr by immediate ---
cat > "$TMP/t26.s" << 'EOF'
.text
.global shrfunc
shrfunc:
  shr rax, 1
  ret
EOF
assemble "$TMP/t26.s" "$TMP/t26.o"
check "shr rax,1" "48d1e8" "$(od -An -tx1 -j64 -N3 "$TMP/t26.o" | tr -d ' \n')"

# --- Test 27: not/neg ---
cat > "$TMP/t27.s" << 'EOF'
.text
.global notneg
notneg:
  not rax
  neg rbx
  ret
EOF
assemble "$TMP/t27.s" "$TMP/t27.o"
check "not/neg" "48f7d048f7db" "$(od -An -tx1 -j64 -N6 "$TMP/t27.o" | tr -d ' \n')"

# --- Test 28: -D flag ---
cat > "$TMP/t28.s" << 'EOF'
.text
.global f
f:
<?lua if ccwas.env.FOO == "bar" then ccwas.emit("nop\n") end ?>ret
EOF
"$CCWAS" --target=x86-64 -DFOO=bar "$TMP/t28.s" -o "$TMP/t28.o" 2>/dev/null
check "env -D flag" "90c3" "$(od -An -tx1 -j64 -N2 "$TMP/t28.o" | tr -d ' \n')"

# --- Test 29: .equ directive ---
cat > "$TMP/t29.s" << 'EOF'
.text
.equ ANSWER, 42
.global f
f:
  mov rax, 42
  ret
EOF
assemble "$TMP/t29.s" "$TMP/t29.o"
check "equ directive" "48b82a00000000000000" "$(od -An -tx1 -j64 -N10 "$TMP/t29.o" | tr -d ' \n')"

# --- Test 30: idiv ---
cat > "$TMP/t30.s" << 'EOF'
.text
.global divfunc
divfunc:
  idiv rbx
  ret
EOF
assemble "$TMP/t30.s" "$TMP/t30.o"
check "idiv rbx" "48f7fb" "$(od -An -tx1 -j64 -N3 "$TMP/t30.o" | tr -d ' \n')"

# --- Test 31: invalid alignment rejected ---
cat > "$TMP/t31.s" << 'EOF'
.text
nop
.align 3
ret
EOF
if "$CCWAS" --target=x86-64 "$TMP/t31.s" -o "$TMP/t31.o" 2>"$TMP/t31.err"; then
  FAIL=$((FAIL + 1))
  echo "  FAIL: invalid alignment should be rejected"
else
  check_contains "invalid alignment diagnostic" "positive power of two" "$TMP/t31.err"
fi

# --- Test 32: GNU directive aliases rejected ---
cat > "$TMP/t32.s" << 'EOF'
.text
.short 1
EOF
if "$CCWAS" --target=x86-64 "$TMP/t32.s" -o "$TMP/t32.o" 2>"$TMP/t32.err"; then
  FAIL=$((FAIL + 1))
  echo "  FAIL: GNU directive alias should be rejected"
else
  check_contains "directive alias diagnostic" "canonical CCWAS name" "$TMP/t32.err"
fi

# --- Test 33: symbol redefinition rejected ---
cat > "$TMP/t33.s" << 'EOF'
.text
duplicate:
duplicate:
ret
EOF
if "$CCWAS" --target=x86-64 "$TMP/t33.s" -o "$TMP/t33.o" 2>"$TMP/t33.err"; then
  FAIL=$((FAIL + 1))
  echo "  FAIL: duplicate symbol should be rejected"
else
  check_contains "duplicate symbol diagnostic" "redefined" "$TMP/t33.err"
fi

# --- Test 34: warnings promoted by -W error ---
cat > "$TMP/t34.s" << 'EOF'
.text
.warning "careful"
ret
EOF
if "$CCWAS" --target=x86-64 -W error "$TMP/t34.s" -o "$TMP/t34.o" 2>"$TMP/t34.err"; then
  FAIL=$((FAIL + 1))
  echo "  FAIL: -W error should reject warnings"
else
  check_contains "warning promotion diagnostic" "treated as errors" "$TMP/t34.err"
fi

# --- Test 35: deterministic object bytes ---
"$CCWAS" --target=x86-64 "$TMP/t1.s" -o "$TMP/t35a.o"
"$CCWAS" --target=x86-64 "$TMP/t1.s" -o "$TMP/t35b.o"
if cmp -s "$TMP/t35a.o" "$TMP/t35b.o"; then
  PASS=$((PASS + 1))
  echo "  PASS: deterministic object bytes"
else
  FAIL=$((FAIL + 1))
  echo "  FAIL: deterministic object bytes"
fi

# --- Test 36: ELF names and global binding ---
check_command_not_contains "ELF section names are valid" "<corrupt>" readelf -S "$TMP/t1.o"
check_command_contains "global symbol binding" "GLOBAL.* f$" readelf -s "$TMP/t1.o"

# --- Summary ---
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
if [ "$FAIL" -gt 0 ]; then
  exit 1
fi
