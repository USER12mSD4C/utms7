# UTMS7

Author: UTMS Innovative Technologies
This documentation uses Simplified Technical English (ASD-STE100).

## 1. Overview

UTMS7 is a hybrid operating system for x86-64 PCs.
The kernel keeps the core services: memory, scheduler, system calls, drivers.
User programs run in ring 3.
Each user process has its own address space.
Kernel modules use the UMOK format and run in ring 0.

## 2. Components

### 2.1 Kernel core

- Multiboot2 boot path (GRUB).
- Four-level paging, 4 KB and 2 MB pages.
- Block allocator for physical memory.
- Preemptive scheduler: kernel threads and user processes.
- syscall/sysret entry, table of 64 system calls.
- ELF64 and raw binary loaders.
- Exception and IRQ handling with timer context switch.

### 2.2 Drivers

- Framebuffer console with DRM-like UAPI (VMware VGA, Bochs VGA, EFI GOP fallback).
- PCI enumeration.
- Disk driver, GPT partitions, partition manager.
- PS/2 keyboard and mouse.
- RTL8139 and Intel e1000 network adapters.

### 2.3 File systems

- UFS: native file system, read and write.
- FAT: read support.

### 2.4 Network stack

Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP client, DNS client, HTTP client.

### 2.5 Userspace

- init.bin: first user process, restarts the shell.
- sh.bin: shell with pipes, redirection and history.
- Both are ELF64 executables linked at 0x40000000.

## 3. Build and run

Requirements: gcc, nasm, binutils, grub2-mkrescue, xorriso, qemu-system-x86_64.

- umk build: build kernel.bin, init.bin, sh.bin.
- umk run: create utms.iso and start QEMU.
- umk runD: start QEMU with interrupt log in qemu.log.
- umk clean: delete build output.

## 4. Source tree

- kernel/: scheduler, paging, system calls, ELF loader, module loader (kinit).
- drivers/: hardware drivers.
- net/: network stack.
- fs/: file systems.
- lib/: shared C library, kernel side and user side.
- commands/: shell builtin commands.
- apps/: user programs (init, sh).
- adders/: early boot init (ski).
- tools/: mkmod, the UMOK module packer.

## 5. User ABI

- System call number in rax, arguments in rdi, rsi, rdx, r10, r8, r9, return in rax.
- Numbers: kernel/syscall.h and include/syscall.h.
- User heap base: 0x40000000 (sys_brk, sys_mmap).
- User stack top: 0x7FFFFFFFF000.

## 6. Kernel module ABI

- Modules are relocatable objects packed by tools/mkmod into UMOK.
- The loader resolves undefined symbols against the kernel symbol table.
- Entry point: .module_entry section.
