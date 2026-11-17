; Hello world — Windows x64, calling the Windows API directly (no io64.inc).
; Microsoft x64 calling convention: arguments in rcx, rdx, r8, r9, then the
; stack; the caller reserves 32 bytes of "shadow space" and keeps rsp
; 16-byte aligned at every call.
default rel

extern GetStdHandle
extern WriteFile

section .data
    msg     db "Hello, world!", 13, 10
    msg_len equ $ - msg

section .bss
    written resd 1

section .text
global main
main:
    sub rsp, 40                 ; 32 shadow space + 8 to realign the stack

    mov ecx, -11                ; STD_OUTPUT_HANDLE
    call GetStdHandle           ; rax = handle

    mov rcx, rax                ; hFile
    lea rdx, [msg]              ; lpBuffer
    mov r8d, msg_len            ; nNumberOfBytesToWrite
    lea r9, [written]           ; lpNumberOfBytesWritten
    mov qword [rsp + 32], 0     ; lpOverlapped = NULL (5th argument)
    call WriteFile

    add rsp, 40
    xor eax, eax                ; return 0
    ret
