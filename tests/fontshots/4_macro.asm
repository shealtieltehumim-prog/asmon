%macro SWAP 2
    push %1
    push %2
    pop  %1
    pop  %2
%endmacro

section .text
global max2
; rcx = a, rdx = b  ->  rax = max(a, b)
max2:
    mov rax, rcx
    cmp rcx, rdx
    cmovl rax, rdx
    ret

global main
main:
    mov rcx, 5
    mov rdx, 9
    call max2
    SWAP rax, rdx
    ret
