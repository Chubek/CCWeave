# DOING-SYSCALLS-LINUX.md

# Making Linux System Calls in Assembly: x86-64, AArch64, and RISC-V 64

This document explains how to invoke Linux kernel system calls directly from
assembly code on three architectures. Each section covers the calling
convention, register usage, a complete "Hello, world" example, and how to
assemble and link it.

---

## 1. General Concepts

A system call (syscall) is a request from user space to the kernel. On every
architecture the pattern is the same:

1. Place the **syscall number** in a designated register.
2. Place the **arguments** in designated registers (up to 6).
3. Execute the architecture's **trap instruction**.
4. Read the **return value** from a designated register. On error, the kernel
   returns a negative errno value in the range $[-4095, -1]$, i.e. a return
   value $r$ indicates an error when $-4095 \le r \le -1$.

Syscall numbers are **architecture-specific**. Authoritative sources in the
kernel tree:

| Architecture | Syscall table location |
|---|---|
| x86-64 | `arch/x86/entry/syscalls/syscall_64.tbl` |
| AArch64 | `include/uapi/asm-generic/unistd.h` |
| RISC-V 64 | `include/uapi/asm-generic/unistd.h` |

On an installed system, `/usr/include/asm/unistd_64.h` (x86-64) or
`/usr/include/asm-generic/unistd.h` (AArch64/RISC-V) list the numbers.
Note that AArch64 and RISC-V share the modern *generic* table, so legacy
calls like `open` are absent — use `openat` instead.

---

## 2. x86-64

### 2.1 Convention

| Item | Register |
|---|---|
| Syscall number | `rax` |
| Argument 1 | `rdi` |
| Argument 2 | `rsi` |
| Argument 3 | `rdx` |
| Argument 4 | `r10` (**not** `rcx`!) |
| Argument 5 | `r8` |
| Argument 6 | `r9` |
| Trap instruction | `syscall` |
| Return value | `rax` |
| Clobbered | `rcx` (saved RIP), `r11` (saved RFLAGS) |

Key differences from the userspace C ABI: the 4th argument goes in `r10`
instead of `rcx`, because the `syscall` instruction itself destroys `rcx`
and `r11`.

Common numbers: `read`=0, `write`=1, `open`=2, `close`=3, `mmap`=9,
`exit`=60, `openat`=257.

### 2.2 Example (GNU as / AT&T syntax)

```asm
    .section .rodata
msg:
    .ascii "Hello, x86-64!\n"
    .set msglen, . - msg

    .section .text
    .globl _start
_start:
    # write(1, msg, msglen)
    mov $1, %rax            # syscall number: write
    mov $1, %rdi            # fd = stdout
    lea msg(%rip), %rsi     # buffer
    mov $msglen, %rdx       # length
    syscall

    # exit(0)
    mov $60, %rax           # syscall number: exit
    xor %rdi, %rdi          # status = 0
    syscall
```
Intel-syntax equivalent (NASM):

```asm
section .rodata
msg:    db "Hello, x86-64!", 10
msglen  equ $ - msg

section .text
global _start
_start:
    mov rax, 1              ; write
    mov rdi, 1              ; stdout
    lea rsi, [rel msg]
    mov rdx, msglen
    syscall

    mov rax, 60             ; exit
    xor rdi, rdi
    syscall
```

### 2.3 Build and run

```bash
# GNU as
as -o hello.o hello.s
ld -o hello hello.o
./hello

# NASM
nasm -f elf64 -o hello.o hello.asm
ld -o hello hello.o
./hello
```

### 2.4 Legacy note

The old 32-bit mechanism `int $0x80` still exists but uses 32-bit syscall
numbers and truncates pointers; never use it in 64-bit code. Always use the
`syscall` instruction on x86-64.

---

## 3. AArch64 (ARM64)

### 3.1 Convention

| Item | Register |
|---|---|
| Syscall number | `x8` |
| Argument 1 | `x0` |
| Argument 2 | `x1` |
| Argument 3 | `x2` |
| Argument 4 | `x3` |
| Argument 5 | `x4` |
| Argument 6 | `x5` |
| Trap instruction | `svc #0` |
| Return value | `x0` |

AArch64 uses the generic syscall table. Common numbers: `openat`=56,
`close`=57, `read`=63, `write`=64, `exit`=93, `exit_group`=94, `mmap`=222.
There is no `open`; use `openat(AT_FDCWD, path, flags, mode)` with
`AT_FDCWD` $= -100$.

### 3.2 Example (GNU as)

```asm
    .section .rodata
msg:
    .ascii "Hello, AArch64!\n"
    .set msglen, . - msg

    .section .text
    .globl _start
_start:
    // write(1, msg, msglen)
    mov x8, #64             // syscall number: write
    mov x0, #1              // fd = stdout
    adr x1, msg             // buffer (adrp/add for far symbols)
    mov x2, #msglen         // length
    svc #0

    // exit(0)
    mov x8, #93             // syscall number: exit
    mov x0, #0              // status = 0
    svc #0
```
For symbols that may be out of `adr` range, use the standard pair:

```asm
    adrp x1, msg
    add  x1, x1, :lo12:msg
```
### 3.3 Build and run

```bash
# Native
as -o hello.o hello.s
ld -o hello hello.o
./hello

# Cross-compiling from another host
aarch64-linux-gnu-as -o hello.o hello.s
aarch64-linux-gnu-ld -o hello hello.o
qemu-aarch64 ./hello
```
---

## 4. RISC-V 64 (RV64)

### 4.1 Convention

| Item | Register |
|---|---|
| Syscall number | `a7` (x17) |
| Argument 1 | `a0` (x10) |
| Argument 2 | `a1` (x11) |
| Argument 3 | `a2` (x12) |
| Argument 4 | `a3` (x13) |
| Argument 5 | `a4` (x14) |
| Argument 6 | `a5` (x15) |
| Trap instruction | `ecall` |
| Return value | `a0` |

RISC-V also uses the generic syscall table, so the numbers match AArch64:
`openat`=56, `close`=57, `read`=63, `write`=64, `exit`=93, `mmap`=222.
Like AArch64, there is no legacy `open` — use `openat`.

### 4.2 Example (GNU as)

```asm
    .section .rodata
msg:
    .ascii "Hello, RISC-V!\n"
    .set msglen, . - msg

    .section .text
    .globl _start
_start:
    # write(1, msg, msglen)
    li a7, 64               # syscall number: write
    li a0, 1                # fd = stdout
    la a1, msg              # buffer
    li a2, msglen           # length
    ecall

    # exit(0)
    li a7, 93               # syscall number: exit
    li a0, 0                # status = 0
    ecall
```
### 4.3 Build and run

```bash
# Native
as -o hello.o hello.s
ld -o hello hello.o
./hello

# Cross-compiling from another host
riscv64-linux-gnu-as -o hello.o hello.s
riscv64-linux-gnu-ld -o hello hello.o
qemu-riscv64 ./hello
```
---

## 5. Side-by-Side Summary

| | x86-64 | AArch64 | RISC-V 64 |
|---|---|---|---|
| Trap instruction | `syscall` | `svc #0` | `ecall` |
| Number register | `rax` | `x8` | `a7` |
| Arg registers | `rdi rsi rdx r10 r8 r9` | `x0`–`x5` | `a0`–`a5` |
| Return register | `rax` | `x0` | `a0` |
| Clobbers | `rcx`, `r11` | none extra | none extra |
| `write` number | 1 | 64 | 64 |
| `exit` number | 60 | 93 | 93 |
| Syscall table | x86-specific | generic | generic |

## 6. Error Handling

On all three architectures the kernel returns $-\text{errno}$ directly in the
return register. A robust check:

```asm
    # after the syscall (x86-64 example)
    cmp $-4095, %rax        # unsigned compare against 0xFFFFFFFFFFFFF001
    jae syscall_failed      # rax in [-4095, -1] means error
```
The actual errno is then $\text{errno} = -r$ where $r$ is the returned value.
This differs from the C library, which stores errno in a thread-local
variable and returns $-1$.

## 7. Practical Tips

- Prefer `exit_group` over `exit` in multithreaded programs; in a
  single-threaded `_start` either works.
- Registers not listed as clobbered are preserved across a syscall.
- Use `strace ./hello` to verify which syscalls your binary actually makes.
- When writing inline assembly in C instead, use the `syscall(2)` libc
  wrapper unless you have a reason not to.


