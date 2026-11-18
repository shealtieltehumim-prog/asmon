; Runs until stopped: used to test Debug > Attach to process.
%include "io64.inc"
section .text
global main
main:
    mov rbp, rsp
    xor rbx, rbx
outer:
    mov rcx, 100000000
inner:
    dec rcx
    jnz inner
    inc rbx
    jmp outer
