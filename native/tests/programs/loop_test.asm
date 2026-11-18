; Never ends (native debug engine test: pause and kill).
default rel
global main
section .text
main:
    xor eax, eax
spin:
    inc rax
    jmp spin
