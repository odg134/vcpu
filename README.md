# vcpu

A lightweight virtual CPU (emulator). It defines its own small
64-bit instruction set, ships a two-pass assembler and a disassembler, and
executes programs against a flat `VirtualAlloc`-backed address space. Guest
I/O, timing and sleeping are based off of the Win32 API, support for other architectures may come in the eventual future.

## Building

Requires CMake and a Windows C++ toolchain (MSVC or clang-cl).

```
cmake -S . -B build
cmake --build build --config Release
```

The binary lands at `build\Release\vcpu.exe` (or `build\vcpu.exe` with a
single-config generator such as Ninja).

## Usage

```
vcpu run    <program.vasm|.vbin> [--mem <bytes>] [--trace] [--stats]
vcpu asm    <program.vasm> -o <program.vbin>
vcpu disasm <program.vasm|.vbin>
```

- `run` assembles a `.vasm` listing (or loads a raw `.vbin` image) and
  executes it. `--trace` prints each instruction to stderr as it retires;
  `--stats` reports the retired instruction count; `--mem` sets the guest
  memory size (default 1 MiB).
- `asm` assembles a listing into a flat `.vbin` image.
- `disasm` linearly disassembles an image.

```
vcpu run examples\Hello.vasm
vcpu run examples\Fib.vasm
vcpu run examples\Squares.vasm
vcpu run examples\Countdown.vasm
```

## Architecture

- **Registers**: `r0`–`r15`, all 64-bit and general purpose. The stack
  pointer `sp` is register index 16 and is addressable like any other
  register.
- **Program counter / flags**: maintained by the core, not directly
  addressable.
- **Flags**: zero, sign, carry and overflow, set only by `cmp` / `cmpi`
  and consumed by the conditional jumps (x86-style signed semantics).
- **Memory**: a single flat, zero-filled image. The program loads at
  address 0 and execution starts there; the stack grows down from the top
  of the image.
- **Instruction word**: a fixed 12 bytes : opcode, two register slots, a
  reserved byte, then a little-endian 64-bit immediate.

## Assembly language

- One instruction or directive per line; `;` starts a comment.
- Labels are `name:` and resolve to a byte address.
- Immediates may be decimal (`42`, `-7`), hex (`0x2a`), a character
  literal (`'A'`, `'\n'`), or a label.

### Instructions

| Form | Mnemonics |
|------|-----------|
| none | `nop`, `halt`, `ret` |
| `rd` | `neg`, `inc`, `dec`, `not`, `push`, `pop` |
| `rd, rs` | `mov`, `add`, `sub`, `mul`, `div`, `mod`, `and`, `or`, `xor`, `shl`, `shr`, `cmp` |
| `rd, imm` | `ldi`, `addi`, `subi`, `shli`, `shri`, `cmpi` |
| `rd, rs, imm` | `ld`, `st`, `ldb`, `stb` |
| `imm` | `jmp`, `je`, `jne`, `jg`, `jge`, `jl`, `jle`, `call`, `sys` |

- `ld rd, rs, imm` loads a 64-bit word from `[rs + imm]`; `st rd, rs, imm`
  stores `rs` into `[rd + imm]`. `ldb` / `stb` are the byte-wide variants.
  The immediate may be omitted (defaults to 0).
- `div` / `mod` are signed; dividing by zero stops the core.
- `call` pushes the return address and jumps; `ret` pops it.

### Directives

- `.string "..."` / `.asciz "..."` : bytes plus a null terminator.
- `.ascii "..."` : bytes without a terminator.
- `.byte` / `.word` / `.dword` / `.qword` : little-endian numeric data.
- `.space N` : `N` zero bytes.

### Syscalls (`sys imm`)

`r0` carries the argument; the input calls return their result in `r0`.

| imm | Name | Behaviour |
|-----|------|-----------|
| 0 | Exit | stop, exit code = `r0` |
| 1 | PutChar | write `r0` as a byte |
| 2 | PutInt | write `r0` as signed decimal |
| 3 | PutString | write the null-terminated string at `[r0]` |
| 4 | GetChar | read one byte into `r0` (`-1` on EOF) |
| 5 | GetInt | read a decimal integer into `r0` |
| 6 | Sleep | sleep `r0` milliseconds (`Sleep`) |
| 7 | Ticks | milliseconds since start into `r0` (`QueryPerformanceCounter`) |
