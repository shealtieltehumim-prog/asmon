%include "io64.inc"
section .text
global main
extern square
main:
    mov rbp, rsp
    mov rdi, 7
    call square
    PRINT_DEC 8, rax
    NEWLINE
    xor rax, rax
    ret
