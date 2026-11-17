/*
 * Asmon — helper object linked into every program built on Windows.
 * Replaces SASM's prebuilt macro.o / macro64.o (built with GCC 4.8.1 against
 * msvcrt; they referenced __iob_func, which does not exist in UCRT).
 *
 * io.inc / io64.inc call get_stdin() / get_stdout() to obtain the C runtime's
 * FILE pointers, because stdin/stdout are macros in C, not linkable symbols.
 *
 * Built by CI with the bundled MinGW-w64 GCC:
 *   gcc -c -O2 -g macro_win.c -o macro64.o        (x64)
 *   gcc -m32 -c -O2 -g macro_win.c -o macro.o     (x86, if the 32-bit toolchain is shipped)
 *
 * License: GPL-3.0 (same as Asmon / SASM).
 */
#include <stdio.h>
#include <fcntl.h>
#include <io.h>

FILE *get_stdin(void) { return stdin; }
FILE *get_stdout(void) { return stdout; }

/* Kept for compatibility with SASM: reopen stdin from "input.txt" in the
 * current directory. Returns 0 on success, -1 on failure. */
int sasm_replace_stdin(void)
{
    int fd = _open("input.txt", _O_RDONLY);
    if (fd < 0)
        return -1;
    int r = _dup2(fd, 0);
    _close(fd);
    return r;
}

/* Used by the debugger: reopen stdin from an arbitrary file. */
int asmon_redirect_stdin(const char *path)
{
    return freopen(path, "r", stdin) ? 0 : -1;
}
