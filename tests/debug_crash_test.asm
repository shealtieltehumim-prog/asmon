; Debugger test used by CI: step, call/ret, macro, loop, then a crash on line 22.
%include "io64.inc"

section .data
    msg db 'before crash', 0
    val dq 0x1122334455667788

section .text
global main
main:
    mov rax, 5
    mov rbx, 7
    add rax, rbx
    call helper
    PRINT_STRING msg
    NEWLINE
    mov rcx, 3
loop1:
    dec rcx
    jnz loop1
    xor rcx, rcx
    mov qword [rcx], 1   ; line 21: SIGSEGV
    ret

helper:
    mov rdx, 42
    ret
