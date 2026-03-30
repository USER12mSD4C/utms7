; kernel/isr.asm
bits 64
section .text

global isr_wrapper0
global isr_wrapper1
global isr_wrapper2
global isr_wrapper3
global isr_wrapper4
global isr_wrapper5
global isr_wrapper6
global isr_wrapper7
global isr_wrapper8
global isr_wrapper9
global isr_wrapper10
global isr_wrapper11
global isr_wrapper12
global isr_wrapper13
global isr_wrapper14
global isr_wrapper15
global isr_wrapper16
global isr_wrapper17
global isr_wrapper18
global isr_wrapper19
global isr_wrapper20
global isr_wrapper21
global isr_wrapper22
global isr_wrapper23
global isr_wrapper24
global isr_wrapper25
global isr_wrapper26
global isr_wrapper27
global isr_wrapper28
global isr_wrapper29
global isr_wrapper30
global isr_wrapper31

global irq0_handler_asm
global irq1_handler_asm
global irq2_handler_asm
global irq3_handler_asm
global irq4_handler_asm
global irq5_handler_asm
global irq6_handler_asm
global irq7_handler_asm
global irq8_handler_asm
global irq9_handler_asm
global irq10_handler_asm
global irq11_handler_asm
global irq12_handler_asm
global irq13_handler_asm
global irq14_handler_asm
global irq15_handler_asm

extern exception_handler_c
extern irq0_handler_c
extern irq1_handler_c
extern irq11_handler_c
extern irq12_handler_c

%macro ISR_NOERRCODE 1
isr_wrapper%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
isr_wrapper%1:
    push %1
    jmp isr_common
%endmacro

%macro SAVE_REGS 0
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
%endmacro

%macro RESTORE_REGS 0
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
%endmacro

isr_common:
    SAVE_REGS
    mov rdi, [rsp + 136]
    mov rsi, [rsp + 128]
    call exception_handler_c
    RESTORE_REGS
    add rsp, 16
    iretq

; IRQ обработчики
irq0_handler_asm:
    cli
    SAVE_REGS
    call irq0_handler_c
    RESTORE_REGS
    mov al, 0x20
    out 0x20, al
    sti
    iretq

irq1_handler_asm:
    cli
    SAVE_REGS
    call irq1_handler_c
    RESTORE_REGS
    mov al, 0x20
    out 0x20, al
    sti
    iretq

irq2_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq3_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq4_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq5_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq6_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq7_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq8_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq9_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq10_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq11_handler_asm:
    cli
    SAVE_REGS
    call irq11_handler_c
    RESTORE_REGS
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    sti
    iretq

irq12_handler_asm:
    cli
    SAVE_REGS
    call irq12_handler_c
    RESTORE_REGS
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    sti
    iretq

irq13_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq14_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop rax
    RESTORE_REGS
    sti
    iretq

irq15_handler_asm:
    cli
    SAVE_REGS
    push rax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop rax
    RESTORE_REGS
    sti
    iretq

; Объявления исключений
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31
