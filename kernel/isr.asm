bits 64
section .text

; Макрос для сохранения всех регистров общего назначения
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

; Макрос для восстановления всех регистров общего назначения
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

; Макрос для определения обработчика исключения без кода ошибки
%macro ISR_NOERRCODE 1
global isr_wrapper%1
isr_wrapper%1:
    cli
    push 0                 ; заглушка для error_code
    push %1                ; номер прерывания
    jmp isr_common
%endmacro

; Макрос для определения обработчика исключения с кодом ошибки
%macro ISR_ERRCODE 1
global isr_wrapper%1
isr_wrapper%1:
    cli
    push %1                ; номер прерывания
    jmp isr_common
%endmacro

; Макрос для определения обработчика IRQ
%macro IRQ 2
global irq%1
irq%1:
    cli
    SAVE_REGS
    mov rdi, %2            ; аргумент 1: номер IRQ
    call irq_handler_dispatch
    RESTORE_REGS
    sti
    iretq
%endmacro

extern exception_handler_c
extern irq_handler_dispatch

; Общий обработчик исключений
isr_common:
    SAVE_REGS
    mov rdi, [rsp + 136]    ; номер прерывания (лежит выше сохранённых регистров)
    mov rsi, [rsp + 128]    ; код ошибки
    call exception_handler_c
    RESTORE_REGS
    add rsp, 16             ; убираем push 0 и номер прерывания
    sti
    iretq

; Генерация обработчиков исключений (0-31)
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

; Генерация обработчиков IRQ (0-15)
IRQ 0, 0
IRQ 1, 1
IRQ 2, 2
IRQ 3, 3
IRQ 4, 4
IRQ 5, 5
IRQ 6, 6
IRQ 7, 7
IRQ 8, 8
IRQ 9, 9
IRQ 10, 10
IRQ 11, 11
IRQ 12, 12
IRQ 13, 13
IRQ 14, 14
IRQ 15, 15
