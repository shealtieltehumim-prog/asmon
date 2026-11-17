; Hello world — Linux x64, system calls only (no C library).
; On Windows: choose the "Linux (ELF, WSL)" target, then build and run.
; On Linux: just build and run.

section .data
    msg     db "Hello, world!", 10
    msg_len equ $ - msg

section .text
global main
main:
    mov rax, 1              ; sys_write
    mov rdi, 1              ; fd 1 = stdout
    lea rsi, [rel msg]      ; buffer
    mov rdx, msg_len        ; length
    syscall

    mov rax, 60             ; sys_exit
    xor rdi, rdi            ; exit code 0
    syscall
