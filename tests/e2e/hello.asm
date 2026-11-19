; End-to-end test used by CI: Hello World, a breakpoint on line 13, then the built exe in RE mode.
; Do not move lines: the test sets the breakpoint by going down 12 lines from the top.
%include "io64.inc"

section .data
    msg db 'Hello, world!', 0

section .text
global main
main:
    PRINT_STRING msg
    NEWLINE
    xor rax, rax            ; line 13: the breakpoint
    ret
