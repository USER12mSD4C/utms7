; kernel/sched_asm.asm
global switch_to_process
global enter_userspace
global restore_regs_and_iret

section .text
bits 64

; void switch_to_process(process_t* prev, process_t* next)
; rdi = prev, rsi = next
switch_to_process:
    ; Сохраняем регистры текущего процесса
    mov [rdi + 0], r15
    mov [rdi + 8], r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], r11
    mov [rdi + 40], r10
    mov [rdi + 48], r9
    mov [rdi + 56], r8
    mov [rdi + 64], rbp
    mov [rdi + 72], rbx
    mov [rdi + 80], rcx      ; RIP (через ret)
    mov [rdi + 88], rdx      ; CS (не используется)
    mov [rdi + 96], rsi      ; RFLAGS
    mov [rdi + 104], rsp     ; RSP
    mov [rdi + 112], rdi     ; SS (не используется)
    
    ; Сохраняем RIP через адрес возврата
    mov rax, [rsp]
    mov [rdi + 80], rax
    
    ; Загружаем регистры нового процесса
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov r11, [rsi + 32]
    mov r10, [rsi + 40]
    mov r9,  [rsi + 48]
    mov r8,  [rsi + 56]
    mov rbp, [rsi + 64]
    mov rbx, [rsi + 72]
    
    ; Переключаем CR3 (адресное пространство)
    mov rax, [rsi + 120]     ; cr3
    test rax, rax
    jz .skip_cr3
    mov cr3, rax
.skip_cr3:
    
    ; Переключаем стек и возвращаемся
    mov rsp, [rsi + 104]     ; rsp
    mov rax, [rsi + 80]      ; rip
    mov [rsp], rax           ; кладём rip на стек
    
    ; Восстанавливаем остальное
    pop rax                  ; rip
    pop rcx                  ; rflags
    pop rdi                  ; ss
    pop rsi                  ; rsp
    
    ; Уходим в процесс
    push rdi                 ; ss
    push rsi                 ; rsp
    push rcx                 ; rflags
    push rax                 ; cs
    push qword [rsi + 80]    ; rip
    
    iretq

; void enter_userspace(u64 rip, u64 rsp, u64 cr3)
; rdi = rip, rsi = rsp, rdx = cr3
enter_userspace:
    cli
    
    ; Переключаем CR3
    mov cr3, rdx
    
    ; Настраиваем стек для iretq
    push 0x23                ; SS (user data)
    push rsi                 ; RSP
    push 0x202               ; RFLAGS
    push 0x1B                ; CS (user code)
    push rdi                 ; RIP
    
    ; Переключаемся в user mode
    iretq
