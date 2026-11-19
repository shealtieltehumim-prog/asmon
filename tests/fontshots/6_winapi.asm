extern ExitProcess
extern MessageBoxA

section .data
    title db 'Asmon', 0
    text  db 'Hello from the Windows API', 0

section .text
global main
main:
    sub rsp, 40                 ; shadow space + alignment
    xor ecx, ecx                ; hWnd = NULL
    lea rdx, [rel text]         ; lpText
    lea r8,  [rel title]        ; lpCaption
    xor r9d, r9d                ; MB_OK
    call MessageBoxA
    xor ecx, ecx
    call ExitProcess
