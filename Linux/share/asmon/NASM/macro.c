#include <stdio.h>

#ifdef __linux__
#include <sys/prctl.h>
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif
#ifndef PR_SET_PTRACER_ANY
#define PR_SET_PTRACER_ANY ((unsigned long)-1)
#endif
// Asmon: "Debug > Attach to process" can attach to this program also where
// Yama's ptrace_scope is 1 (Ubuntu's default: only a parent may debug).
// Only programs built in Asmon get this.
__attribute__((constructor)) static void asmon_allow_attach(void)
{
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
}
#endif

FILE *get_stdin(void) { return stdin; }
FILE *get_stdout(void) { return stdout; }

// I/O functions easily callable from asm
// - Accept under-aligned stack
// - Return value (if any) in EAX
// - Up to three arguments in EAX, EDX, ECX
// - Preserve EBX, EBP, ESP, EDI, ESI
//
// Extends macro.c from SASM (usually /usr/share/sasm/NASM/macro.c)

#define IO_ATTR __attribute__((regparm(3),force_align_arg_pointer))

// Input facilities

// Read a 32-bit number using %d/%u/%x format, return value in EAX
IO_ATTR int      io_get_dec(void);
IO_ATTR unsigned io_get_udec(void);
IO_ATTR unsigned io_get_hex(void);

// Read a character via getchar(), return value in EAX
IO_ATTR int io_get_char(void);

// Read a string, 'buf' in EAX, 'size' in EDX
IO_ATTR void io_get_string(char *buf, int size);

// Output facilities

// Print a 32-bit number in EAX using %d/%u/%x format
IO_ATTR void io_print_dec(int v);
IO_ATTR void io_print_udec(unsigned v);
IO_ATTR void io_print_hex(unsigned v);

// Print a character in AL
IO_ATTR void io_print_char(char c);

// Print a string addressed by EAX
IO_ATTR void io_print_string(const char *s);

// Start a new line
IO_ATTR void io_newline(void);

IO_ATTR
int io_get_dec(void)
{
	int r;
	scanf("%d", &r);
	return r;
}

IO_ATTR
unsigned io_get_udec(void)
{
	unsigned r;
	scanf("%u", &r);
	return r;
}

IO_ATTR
unsigned io_get_hex(void)
{
	unsigned r;
	scanf("%x", &r);
	return r;
}

IO_ATTR
int io_get_char(void)
{
	return getchar();
}

IO_ATTR
void io_get_string(char *buf, int size)
{
	fgets(buf, size, stdin);
}

IO_ATTR
void io_print_dec(int v)
{
	printf("%d", v);
}

IO_ATTR
void io_print_udec(unsigned v)
{
	printf("%u", v);
}

IO_ATTR
void io_print_hex(unsigned v)
{
	printf("%x", v);
}

IO_ATTR
void io_print_char(char c)
{
	putchar(c);
}

IO_ATTR
void io_print_string(const char *s)
{
	fputs(s, stdout);
}

IO_ATTR
void io_newline(void)
{
	putchar('\n');
}

__attribute__((constructor))
static void unbuffer_stdout(void)
{
	setbuf(stdout, 0);
}
