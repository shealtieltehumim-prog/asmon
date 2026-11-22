; =============================================
; demo.asm - הדגמה של אסמון
; =============================================
%macro PRINT 2
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [%1]
    mov     rdx, %2
    syscall
%endmacro

section .data
    msg     db  "Hello, Asmon!", 10
    len     equ $ - msg
    nums    dq  1, 2, 3, 4, 5

section .bss
    buffer  resb 64

section .text
global _start

_start:
    PRINT   msg, len
    xor     rcx, rcx
    lea     rbx, [nums]
.sum:
    add     rax, [rbx + rcx*8]
    inc     rcx
    cmp     rcx, 5
    jl      .sum

    call    helper
    mov     rax, 60         ; exit
    xor     rdi, rdi
    syscall

; helper: returns 42 in rax
helper:
    push    rbp
    mov     rbp, rsp
    mov     rax, 42
    pop     rbp
    ret
