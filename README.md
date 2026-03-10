# UTMS7

**Copyright (c) 2026 The UOPL Authors**

x86-64 operating system written in C and NASM assembly.

## Project structure

- `kernel/` – core kernel code
- `drivers/` – hardware drivers (keyboard, mouse, VGA, VESA, disk, etc.)
- `fs/` – file system implementations (UFS, FAT, EXT4)
- `apps/` – user applications (UWR text editor)
- `shell/` – command shell (USS interpreter)
- `include/` – public headers
- `lib/` – standard library replacements
- `usb/` – USB support (XHCI)
- `commands/` – shell command implementations

## Requirements

- x86-64 machine or emulator (QEMU)
- NASM assembler
- GCC cross-compiler (i686-elf or x86_64-elf) or standard GCC with appropriate flags
- GNU Make
- GRUB (for ISO creation) or `xorriso` (for ISO generation)
- linux-based distro (if you want to make it from source)

## Building

```
make
make run #with qemu
make runD #with logs
make iso
```

## What is that?

thats ultra modular operating system, where you can change or add absolutely anything
