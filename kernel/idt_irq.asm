global irq_wrapper_0
global irq_wrapper_1
global irq_wrapper_2
global irq_wrapper_3
global irq_wrapper_4
global irq_wrapper_5
global irq_wrapper_6
global irq_wrapper_7
global irq_wrapper_8
global irq_wrapper_9
global irq_wrapper_10
global irq_wrapper_11
global irq_wrapper_12
global irq_wrapper_13
global irq_wrapper_14
global irq_wrapper_15

extern irq_handler

%macro IRQ_WRAPPER 1
irq_wrapper_%1:
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
    
    mov rdi, %1
    call irq_handler
    
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
    iretq
%endmacro

IRQ_WRAPPER 0
IRQ_WRAPPER 1
IRQ_WRAPPER 2
IRQ_WRAPPER 3
IRQ_WRAPPER 4
IRQ_WRAPPER 5
IRQ_WRAPPER 6
IRQ_WRAPPER 7
IRQ_WRAPPER 8
IRQ_WRAPPER 9
IRQ_WRAPPER 10
IRQ_WRAPPER 11
IRQ_WRAPPER 12
IRQ_WRAPPER 13
IRQ_WRAPPER 14
IRQ_WRAPPER 15
