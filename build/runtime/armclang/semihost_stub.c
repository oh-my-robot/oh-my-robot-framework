/*
 * Armclang no-semihosting syscall stubs.
 *
 * Purpose:
 * - Avoid semihosting BKPT(0xAB) traps during startup/debug.
 * - Keep behavior deterministic when host semihost channel is unavailable.
 *
 * Notes:
 * - These stubs intentionally provide minimal behavior.
 * - They are injected only for binary targets when semihosting=off.
 */

#include <stdint.h>

int _sys_open(const char* name, int openmode)
{
    (void)name;
    (void)openmode;
    /* Return a valid pseudo handle so C library init does not abort. */
    return 1;
}

int _sys_close(int file)
{
    (void)file;
    return 0;
}

int _sys_write(int file, const char* ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _sys_read(int file, char* ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

int _sys_istty(int file)
{
    (void)file;
    return 1;
}

int _sys_seek(int file, long pos)
{
    (void)file;
    (void)pos;
    return 0;
}

long _sys_flen(int file)
{
    (void)file;
    return 0;
}

int _sys_remove(const char* filename)
{
    (void)filename;
    return -1;
}

int _sys_rename(const char* oldname, const char* newname)
{
    (void)oldname;
    (void)newname;
    return -1;
}

int _sys_command_string(char* cmd, int len)
{
    (void)cmd;
    (void)len;
    return -1;
}

void _sys_exit(int return_code)
{
    (void)return_code;
    while (1) {
    }
}

void _ttywrch(int ch)
{
    (void)ch;
}
