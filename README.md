# UTMS7

**Copyright (c) 2026 The UOPL Authors**

x86-64 operating system written in C and NASM assembly.

i dont recommend use it, still untested
( finally repaired UFS, yay :D )

## Project structure

- `kernel/` – core kernel code
- `drivers/` – hardware drivers source (keyboard, mouse, VGA, VESA, disk, etc.)
- `fs/` – file system implementations (UFS, FAT, EXT4)
- `apps/` – user applications (UWR text editor)
- `shell/` – command shell (USS interpreter)
- `include/` – public headers
- `lib/` – standard library replacements
- `usb/` – USB support (XHCI)
- `commands/` – shell command implementations
- `modules/` - compiled drivers into .ko

## last huge update

- working UWR (utms write-reader)
- working mbr/gpt
- udisk instead of straight gpt
- livecd install (in work)

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
make kom #kernel objects/tools
make livecd #kernel with installing on disk with grub programm
make iso
```

## What is that?

thats ultra modular operating system, where you can change or add absolutely anything you want

## driver/.ko modules

to make your own driver you need to add at the end of the file:
```
static const char __vesa_name[] __attribute__((section(".module_name"))) = "vesa";
static int (*__vesa_entry)(void) __attribute__((section(".module_entry"))) = vesa_init;
```
