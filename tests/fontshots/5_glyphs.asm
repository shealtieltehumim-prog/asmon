; Hard-to-tell characters:  0O  1lI|  5S  8B  rn m  {}[]()<>  ;:,.  `'"  ~^_-=+*/\%
section .data
    table dq 0x0123456789ABCDEF, 0xFEDCBA9876543210
    text  db "Il1| O0o  {[()]} <=>!=  ->  ::", 10, 0
    mask  dd 0b1010_1100, 0o755, 1234567890
section .text
global main
main:
    lea  rsi, [rel table + rax*8 + 16]
    movzx eax, byte [rsi]
    shl  rax, 4 | 1
    ret
