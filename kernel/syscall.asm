global syscall_handler

extern syscall_handler_c

section .text
bits 64

syscall_handler:
    ; Сохраняем все регистры
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
    
    ; Вызываем C обработчик
    ; rdi = syscall_num (уже в rdi из пользователя)
    mov rsi, rbx    ; arg1
    mov rdx, rcx    ; arg2
    mov rcx, rdx    ; arg3
    call syscall_handler_c
    
    ; Восстанавливаем регистры, но сохраняем результат
    mov rbx, rax    ; сохраняем результат
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
    
    ; Возвращаем результат в rax
    mov rax, rbx
    iretq
