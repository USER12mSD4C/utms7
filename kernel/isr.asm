; kernel/isr.asm
bits 64
section .text

extern exception_handler_c
extern irq0_handler_c
extern irq1_handler_c
extern irq11_handler_c
extern irq12_handler_c

%macro ISR_NOERRCODE 1
global isr_wrapper%1
isr_wrapper%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr_wrapper%1
isr_wrapper%1:
    push %1
    jmp isr_common
%endmacro

%macro IRQ 2
global isr_wrapper%1
isr_wrapper%1:
    push 0
    push %2
    jmp irq_common
%endmacro

isr_common:
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
    
    mov rdi, [rsp + 8*16]
    mov rsi, [rsp + 8*16 + 8]
    call exception_handler_c
    
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
    
    add rsp, 16
    iretq

irq_common:
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
    
    mov rdi, [rsp + 8*16 + 8]
    cmp rdi, 32
    je .irq0
    cmp rdi, 33
    je .irq1
    cmp rdi, 43
    je .irq11
    cmp rdi, 44
    je .irq12
    jmp .done
    
.irq0:
    call irq0_handler_c
    jmp .done
.irq1:
    call irq1_handler_c
    jmp .done
.irq11:
    call irq11_handler_c
    jmp .done
.irq12:
    call irq12_handler_c
.done:
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
    
    add rsp, 16
    iretq

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

IRQ 32, 32
IRQ 33, 33
IRQ 43, 43
IRQ 44, 44
