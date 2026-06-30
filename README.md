# UTMS7

**Copyright (c) 2026 The UOPL Authors**

x86-64 operating system written in C and NASM assembly.

i dont recommend use it, cause it isn't working

## last huge update

- ski init system
- multitask (in work)

## Requirements

- x86-64 machine or emulator (QEMU)
- NASM assembler
- GCC cross-compiler (i686-elf or x86_64-elf) or standard GCC with appropriate flags
- GNU Make
- GRUB (for ISO creation) or `xorriso` (for ISO generation)
- linux-based distro (if you want to make it from source)

## Building

```
umk build
umk build --clean //with pre clean
umk build --iso //create iso after build
umk build --disk //create 5gb disk after build
```

## What is that?

operating system with hybrid kernel that im doing because why not

## driver/.ko modules

Work In Progress

## how it works?

idk

## useful
- how to prepare disk?

```
udisk mbr /dev/sdX
udisk create /dev/sdX {amount of MB in partition}
mkfs.ufs /dev/sdX1
mount /dev/sdX1 /
```
