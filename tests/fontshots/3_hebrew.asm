; תוכנית ראשונה באסמבלי: מדפיסה ברכה
%include "io64.inc"

section .data
    greeting db 'Shalom, olam!', 0   ; שלום עולם

section .text
global main
main:
    PRINT_STRING greeting            ; הדפסת המחרוזת
    NEWLINE
    mov eax, 0x7FFFFFFF              ; המספר הגדול ביותר
    ret
