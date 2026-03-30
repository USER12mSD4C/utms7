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

global isr_wrapper32
global isr_wrapper33
global isr_wrapper34
global isr_wrapper35
global isr_wrapper36
global isr_wrapper37
global isr_wrapper38
global isr_wrapper39
global isr_wrapper40
global isr_wrapper41
global isr_wrapper42
global isr_wrapper43
global isr_wrapper44
global isr_wrapper45
global isr_wrapper46
global isr_wrapper47

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

%macro IRQ 2
isr_wrapper%1:
    push 0
    push %2
    jmp irq_common
%endmacro

; ============================================================
; Обработчик исключений (сохраняет все регистры)
; ============================================================
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
    
    mov rdi, [rsp + 136]
    mov rsi, [rsp + 128]
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

; ============================================================
; Обработчик IRQ (сохраняет все регистры, отправляет EOI)
; ============================================================
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
    
    mov rdi, [rsp + 136]
    push rdi
    
    cmp rdi, 32
    je .irq0
    cmp rdi, 33
    je .irq1
    cmp rdi, 43
    je .irq11
    cmp rdi, 44
    je .irq12
    jmp .eoi
    
.irq0:
    call irq0_handler_c
    jmp .eoi
.irq1:
    call irq1_handler_c
    jmp .eoi
.irq11:
    call irq11_handler_c
    jmp .eoi
.irq12:
    call irq12_handler_c
    jmp .eoi

.eoi:
    pop rdi
    mov al, 0x20
    out 0x20, al
    cmp rdi, 40
    jl .no_slave
    out 0xA0, al
.no_slave:
    
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
IRQ 34, 34
IRQ 35, 35
IRQ 36, 36
IRQ 37, 37
IRQ 38, 38
IRQ 39, 39
IRQ 40, 40
IRQ 41, 41
IRQ 42, 42
IRQ 43, 43
IRQ 44, 44
IRQ 45, 45
IRQ 46, 46
IRQ 47, 47
