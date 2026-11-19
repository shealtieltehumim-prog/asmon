; Sum of 1..10, printed as a decimal number
%include "io64.inc"

section .text
global main
main:
    xor rax, rax            ; sum = 0
    mov rcx, 10             ; counter
.next:
    add rax, rcx
    dec rcx
    jnz .next               ; loop while rcx != 0
    PRINT_DEC 8, rax
    NEWLINE
    xor rax, rax
    ret
