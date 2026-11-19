section .text
global _start
_start:
mov rdx,5
mov rbx,1
loop _start:
imuul rbx,rdx
dec rdx
jnz loop _start
mov rax,60
mov rdi,rbx
syscall

