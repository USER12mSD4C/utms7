bits 64
section .text

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

%macro IRQ 2
global irq%1
irq%1:
    cli
    ; Загружаем сегментные регистры данных (на всякий случай)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    SAVE_REGS
    mov rdi, %2
    call irq_handler_dispatch
    ; Проверяем, нужно ли переключение контекста
    cmp dword [sched_need_resched], 0
    je .no_switch
    ; Сохраняем указатель стека в current->kstack_top
    mov rax, [current]
    lea rbx, [rsp + 8]      ; rsp указывает на адрес возврата, сохранённые регистры начинаются с rsp+8
    mov [rax + 40], rbx     ; offsetof(process_t, kstack_top) = 40
    call sched_pick_next
    ; Результат в rax – следующий процесс
    mov rbx, [rax + 40]     ; next->kstack_top
    mov rdx, [rax + 32]     ; next->cr3 (offset 32)
    mov rsp, rbx
    mov cr3, rdx
    ; Продолжаем как при обычном выходе
    RESTORE_REGS
    add rsp, 16             ; убираем номер IRQ и код ошибки
    sti
    iretq
.no_switch:
    RESTORE_REGS
    add rsp, 16
    sti
    iretq
%endmacro

extern exception_handler_c
extern irq_handler_dispatch
extern sched_need_resched
extern current
extern sched_pick_next

isr_common:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    SAVE_REGS
    mov rdi, [rsp + 136]    ; номер прерывания (15 regs * 8 = 120, + 8 ret addr, + 8 err code)
    mov rsi, [rsp + 128]    ; код ошибки
    call exception_handler_c
    RESTORE_REGS
    add rsp, 16
    sti
    iretq

; Исключения
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

; IRQ
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
