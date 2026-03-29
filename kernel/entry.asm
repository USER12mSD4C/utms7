; kernel/entry.asm
[bits 32]
global _start
extern kernel_main
extern __bss_start
extern __bss_end

section .multiboot2
align 8
header_start:
    dd 0xe85250d6
    dd 0
    dd header_end - header_start
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))
    
    align 8
    dw 2
    dw 0
    dd 12
    dd 0
    
    align 8
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

global multiboot_info
multiboot_info:
    resb 8192

section .text
_start:
    cli
    
    mov dword [multiboot_info], ebx
    mov dword [multiboot_info + 4], 0
    
    mov ebx, __bss_start
    mov ecx, __bss_end
    sub ecx, ebx
    mov edi, ebx
    xor eax, eax
    rep stosb
    
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    
    mov edi, 0x1000
    mov eax, 0x2000
    or eax, 3
    mov [edi], eax
    
    mov eax, 0x4000
    or eax, 3
    mov [edi + 510*8], eax
    
    mov edi, 0x2000
    mov eax, 0x3000
    or eax, 3
    mov [edi], eax
    
    mov edi, 0x4000
    mov eax, 0x5000
    or eax, 3
    mov [edi], eax
    
    mov edi, 0x3000
    mov eax, 0x83
    mov ecx, 512
.map_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map_pd
    
    mov edi, 0x5000
    xor eax, eax
    mov ecx, 4096
    rep stosd
    
    mov edi, 0x5000
    mov eax, 0xFD000000
    or eax, 0x83
    mov [edi + 0x3F40], eax
    
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    
    lgdt [gdt64_ptr]
    jmp 0x08:start64

bits 64
start64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    mov rsp, stack_top
    mov rbp, rsp
    
    mov rdi, multiboot_info
    call kernel_main
    
    cli
    hlt
    jmp $

section .data
align 16
gdt64:
    dq 0x0000000000000000
    dq 0x00af9a000000ffff
    dq 0x00cf92000000ffff
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64
