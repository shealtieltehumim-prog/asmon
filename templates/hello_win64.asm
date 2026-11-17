; Hello world — x64 with io64.inc (works on Windows and on Linux).
; F9 = build and run, F5 = debug.
%include "io64.inc"

section .data
    msg db 'Hello, world!', 0

section .text
global main
main:
    PRINT_STRING msg
    NEWLINE
    xor rax, rax            ; return 0
    ret
