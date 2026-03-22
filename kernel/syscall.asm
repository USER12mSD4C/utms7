; kernel/syscall.asm
global syscall_handler

extern syscall_handler_c

section .text
bits 64

syscall_handler:
    swapgs
    
    ; Сохраняем пользовательские регистры
    mov [rsp-8], rcx      ; user RIP
    mov [rsp-16], r11     ; user RFLAGS
    
    ; Переключаемся на стек ядра
    mov rcx, rsp
    mov rsp, [gs:8]       ; kernel rsp (должен быть установлен в gs)
    
    push rcx              ; user RSP
    push r11              ; user RFLAGS
    push rcx              ; user RIP
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Вызываем C обработчик
    mov r9, r8
    mov r8, rcx
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_handler_c
    
    ; Восстанавливаем регистры
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx               ; user RIP
    pop r11               ; user RFLAGS
    pop rsp               ; user RSP
    
    swapgs
    o64 sysret
