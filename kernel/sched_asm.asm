global context_switch

section .text
bits 64

; void context_switch(u64 *old_rsp, u64 new_rsp, u64 new_cr3)
; rdi = old_rsp (указатель на переменную, куда сохранить текущий RSP)
; rsi = new_rsp
; rdx = new_cr3
context_switch:
    ; Сохраняем регистры на текущем стеке
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    
    ; Сохраняем текущий RSP
    mov [rdi], rsp
    
    ; Переключаем стек
    mov rsp, rsi
    
    ; Переключаем адресное пространство, если нужно
    test rdx, rdx
    jz .skip_cr3
    mov cr3, rdx
.skip_cr3:
    
    ; Восстанавливаем регистры
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    
    ret
