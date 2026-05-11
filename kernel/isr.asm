; kernel/isr.asm
bits 64
section .text

extern exception_handler_c
extern irq_handler_dispatch
extern sched_need_resched
extern sched_do_switch
extern current

; === МАКРОСЫ ===

%macro SAVE_REGS 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro RESTORE_REGS 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
%endmacro

; === ОБРАБОТЧИКИ ИСКЛЮЧЕНИЙ ===

%macro ISR_NOERRCODE 1
global isr_wrapper%1
isr_wrapper%1:
    cli
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr_wrapper%1
isr_wrapper%1:
    cli
    push %1
    jmp isr_common
%endmacro

isr_common:
    SAVE_REGS
    mov rdi, [rsp + 120]  ; error_code (первый аргумент)
    mov rsi, [rsp + 128]  ; vector (второй аргумент)
    call exception_handler_c
    RESTORE_REGS
    add rsp, 16
    iretq

; === ОБРАБОТЧИКИ IRQ ===

; Макрос для IRQ 0-7 (мастер PIC)
%macro IRQ_MASTER 2
global irq%1
irq%1:
    cli
    push 0
    push %2
    SAVE_REGS
    mov rdi, %1
    call irq_handler_dispatch

    ; Отправляем EOI мастеру
    mov al, 0x20
    out 0x20, al

    ; Проверяем переключение контекста
    cmp byte [rel sched_need_resched], 0
    je %%no_switch
    mov rdi, rsp
    call sched_do_switch
    mov rsp, rax

%%no_switch:
    RESTORE_REGS
    add rsp, 16
    iretq
%endmacro

; Макрос для IRQ 8-15 (слейв PIC)
%macro IRQ_SLAVE 2
global irq%1
irq%1:
    cli
    push 0
    push %2
    SAVE_REGS
    mov rdi, %1
    call irq_handler_dispatch

    ; Отправляем EOI слейву и мастеру
    mov al, 0x20
    out 0xA0, al
    out 0x20, al

    ; Проверяем переключение контекста
    cmp byte [rel sched_need_resched], 0
    je %%no_switch_slave
    mov rdi, rsp
    call sched_do_switch
    mov rsp, rax

%%no_switch_slave:
    RESTORE_REGS
    add rsp, 16
    iretq
%endmacro

; Генерируем IRQ 0-7 (мастер PIC)
IRQ_MASTER 0, 32
IRQ_MASTER 1, 33
IRQ_MASTER 2, 34
IRQ_MASTER 3, 35
IRQ_MASTER 4, 36
IRQ_MASTER 5, 37
IRQ_MASTER 6, 38
IRQ_MASTER 7, 39

; Генерируем IRQ 8-15 (слейв PIC)
IRQ_SLAVE 8, 40
IRQ_SLAVE 9, 41
IRQ_SLAVE 10, 42
IRQ_SLAVE 11, 43
IRQ_SLAVE 12, 44
IRQ_SLAVE 13, 45
IRQ_SLAVE 14, 46
IRQ_SLAVE 15, 47

; === ПЕРЕКЛЮЧЕНИЕ КОНТЕКСТА (int 0x80) ===
global isr_wrapper_128
isr_wrapper_128:
    cli
    push 0
    push 128
    SAVE_REGS
    mov rdi, rsp
    call sched_do_switch
    mov rsp, rax
    RESTORE_REGS
    add rsp, 16
    iretq

; === ИСКЛЮЧЕНИЯ ===
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
ISR_ERRCODE   21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31
