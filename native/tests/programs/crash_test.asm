; Crashes with a write to address 0 (native debug engine test).
default rel
global main
section .text
main:
    xor ecx, ecx
    mov qword [rcx], 1
    ret
