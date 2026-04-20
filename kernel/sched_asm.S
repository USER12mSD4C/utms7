bits 64
section .text

global switch_to_context
switch_to_context:
    ; Сохраняем контекст текущего процесса
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Сохраняем RSP в old_rsp
    mov [rdi], rsp

    ; Переключаемся на новый стек и CR3
    mov rsp, rsi
    mov rax, cr3
    cmp rax, rdx
    je .skip_cr3
    mov cr3, rdx
.skip_cr3:

    ; Восстанавливаем контекст нового процесса
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
