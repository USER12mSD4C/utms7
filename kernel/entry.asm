[bits 32]
global _start
global keyboard_handler
global timer_handler
global mouse_handler
global double_fault_handler
global context_switch
extern kernel_main
extern keyboard_handler_c
extern timer_handler_c
extern mouse_handler_c
extern double_fault_handler_c
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
bits 32
_start:
    cli
    mov [multiboot_info], ebx
    
    ; Очистка BSS
    mov ecx, __bss_end
    mov eax, __bss_start
    sub ecx, eax
    mov edi, __bss_start
    xor eax, eax
    rep stosb
    
    ; === СТРАНИЧНАЯ АДРЕСАЦИЯ ===
    ; Очищаем PML4 (0x1000)
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    
    ; PML4[0] -> PDPT (0x2000)
    mov edi, 0x1000
    mov eax, 0x2000
    or eax, 3
    mov [edi], eax
    
    ; PML4[510] -> PDPT2 (0x4000) для верхней памяти
    mov eax, 0x4000
    or eax, 3
    mov [edi + 510*8], eax
    
    ; PDPT[0] -> PD (0x3000)
    mov edi, 0x2000
    mov eax, 0x3000
    or eax, 3
    mov [edi], eax
    
    ; PDPT2[0] -> PD2 (0x5000)
    mov edi, 0x4000
    mov eax, 0x5000
    or eax, 3
    mov [edi], eax
    
    ; Заполняем PD (первые 1GB)
    mov edi, 0x3000
    mov eax, 0x83          ; 2MB page, present, writable
    mov ecx, 512
.map_pd:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map_pd
    
    ; Заполняем PD2 (для framebuffer)
    mov edi, 0x5000
    xor eax, eax
    mov ecx, 4096          ; 4096 байт = весь PD2
    rep stosb
    
    ; Добавляем запись для framebuffer (0xFD000000)
    mov edi, 0x5000
    mov eax, 0xFD000000
    or eax, 0x83           ; present + writable + huge
    mov [edi + 0x3F40], eax ; 0x7E8 * 8 = 0x3F40
    mov dword [edi + 0x3F44], 0 ; старшие 32 бита
    
    ; Включаем PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; Включаем long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    ; Включаем paging
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    
    ; Загружаем GDT и прыгаем в 64 бита
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

; === ОБРАБОТЧИКИ ПРЕРЫВАНИЙ ===
align 16
keyboard_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    cld
    call keyboard_handler_c
    
    mov al, 0x20
    out 0x20, al
    out 0xa0, al
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

align 16
timer_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    cld
    call timer_handler_c
    
    mov al, 0x20
    out 0x20, al
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

align 16
mouse_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    cld
    call mouse_handler_c
    
    mov al, 0x20
    out 0xa0, al
    out 0x20, al
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

align 16
double_fault_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    cld
    call double_fault_handler_c
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

align 16
context_switch:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rsi
    push rdi
    push rdx
    push rcx
    push rbx
    push rax
    pushfq
    
    mov [rdi], rsp
    mov rsp, rsi
    
    popfq
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rdi
    pop rsi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    ret

section .data
align 16
gdt64:
    dq 0x0000000000000000
    dq 0x0020980000000000
    dq 0x0000920000000000
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64
