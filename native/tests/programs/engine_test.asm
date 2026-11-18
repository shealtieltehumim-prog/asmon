; Test program of the native debug engine (native/tests/debuggee_harness.cpp).
; Plain NASM + the C runtime: nasm -f win64 -l engine_test.lst engine_test.asm; gcc engine_test.o
default rel
global main
extern puts

section .data
msg     db 'native engine test', 0
align 8
counter dq 0

section .text
main:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rax, 5
    mov rbx, 7
    add rax, rbx
    pushfq
    popfq
    call helper
    lea rcx, [msg]
    call puts
    mov qword [counter], 1
    mov qword [counter], 2
    mov rcx, 3
loop1:
    dec rcx
    jnz loop1
    mov eax, 7
    leave
    ret

helper:
    mov rdx, 42
    ret
