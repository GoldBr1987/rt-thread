/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-11-23     Yihui        The first version
 * 2013-11-24     aozima       fixed _sys_read()/_sys_write() issues.
 * 2014-08-03     bernard      If using msh, use system() implementation
 *                             in msh.
 * 2020-08-05     Meco Man     fixed _sys_flen() compiling-warning when
 *                             RT_USING_DFS is not defined
 * 2020-02-13     Meco Man     re-implement exit() and abort()
 * 2020-02-14     Meco Man     implement _sys_tmpnam()
 */

#include <string.h>
#include <rt_sys.h>

#include <rtthread.h>
#include "libc.h"

#ifdef RT_USING_DFS
#include <dfs_posix.h>
#endif

#define DBG_TAG    "armlibc.syscalls"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

#ifdef __CLANG_ARM
__asm(".global __use_no_semihosting\n\t");
#else
#pragma import(__use_no_semihosting_swi)
#endif

/* Standard IO device handles. */
#define STDIN       0
#define STDOUT      1
#define STDERR      2

/* Standard IO device name defines. */
const char __stdin_name[]  = "STDIN";
const char __stdout_name[] = "STDOUT";
const char __stderr_name[] = "STDERR";

/**
 * required by fopen() and freopen().
 *
 * @param name - file name with path.
 * @param openmode - a bitmap hose bits mostly correspond directly to
 *                     the ISO mode specification.
 * @return  -1 if an error occurs.
 */
FILEHANDLE _sys_open(const char *name, int openmode)
{
#ifdef RT_USING_DFS
    int fd;
    int mode = O_RDONLY;
#endif

    /* Register standard Input Output devices. */
    if (strcmp(name, __stdin_name) == 0)
        return (STDIN);
    if (strcmp(name, __stdout_name) == 0)
        return (STDOUT);
    if (strcmp(name, __stderr_name) == 0)
        return (STDERR);

#ifndef RT_USING_DFS
    return -1;
#else
    /* Correct openmode from fopen to open */
    if (openmode & OPEN_PLUS)
    {
        if (openmode & OPEN_W)
        {
            mode |= (O_RDWR | O_TRUNC | O_CREAT);
        }
        else if (openmode & OPEN_A)
        {
            mode |= (O_RDWR | O_APPEND | O_CREAT);
        }
        else
            mode |= O_RDWR;
    }
    else
    {
        if (openmode & OPEN_W)
        {
            mode |= (O_WRONLY | O_TRUNC | O_CREAT);
        }
        else if (openmode & OPEN_A)
        {
            mode |= (O_WRONLY | O_APPEND | O_CREAT);
        }
    }

    fd = open(name, mode, 0);
    if (fd < 0)
        return -1;
    else
        return fd;
#endif
}

int _sys_close(FILEHANDLE fh)
{
#ifndef RT_USING_DFS
    return 0;
#else
    if (fh <= STDERR) return 0;

    return close(fh);
#endif
}

/*
 * Read from a file. Can return:
 *  - zero if the read was completely successful
 *  - the number of bytes _not_ read, if the read was partially successful
 *  - the number of bytes not read, plus the top bit set (0x80000000), if
 *    the read was partially successful due to end of file
 *  - -1 if some error other than EOF occurred
 *
 * It is also legal to signal EOF by returning no data but
 * signalling no error (i.e. the top-bit-set mechanism need never
 * be used).
 *
 * So if (for example) the user is trying to read 8 bytes at a time
 * from a file in which only 5 remain, this routine can do three
 * equally valid things:
 *
 *  - it can return 0x80000003 (3 bytes not read due to EOF)
 *  - OR it can return 3 (3 bytes not read), and then return
 *    0x80000008 (8 bytes not read due to EOF) on the next attempt
 *  - OR it can return 3 (3 bytes not read), and then return
 *    8 (8 bytes not read, meaning 0 read, meaning EOF) on the next
 *    attempt
 *
 * `mode' exists for historical reasons and must be ignored.
 */
int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
#ifdef RT_USING_DFS
    int size;
#endif

    if (fh == STDIN)
    {
#ifdef RT_USING_POSIX
        if (libc_stdio_get_console() < 0)
        {
            LOG_W("Do not invoke standard output before initializing libc");
            return 0;
        }
        size = read(STDIN_FILENO, buf, len);
        return len - size;
#else
        /* no stdin */
        return -1;
#endif
    }
    else if ((fh == STDOUT) || (fh == STDERR))
    {
        return -1;
    }

#ifndef RT_USING_DFS
    return 0;
#else
    size = read(fh, buf, len);
    if (size >= 0)
        return len - size;
    else
        return -1;
#endif
}

/*
 * Write to a file. Returns 0 on success, negative on error, and
 * the number of characters _not_ written on partial success.
 * `mode' exists for historical reasons and must be ignored.
 */
int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
#ifdef RT_USING_DFS
    int size;
#endif

    if ((fh == STDOUT) || (fh == STDERR))
    {
#if !defined(RT_USING_CONSOLE) || !defined(RT_USING_DEVICE)
        return 0;
#else
#ifdef RT_USING_POSIX
        if (libc_stdio_get_console() < 0)
        {
            LOG_W("Do not invoke standard input before initializing libc");
            return 0;
        }
        size = write(STDOUT_FILENO, buf, len);
        return len - size;
#else
        if (rt_console_get_device())
        {
            rt_device_write(rt_console_get_device(), -1, buf, len);
            return 0;
        }

        return -1;
#endif
#endif
    }
    else if (fh == STDIN)
    {
        return -1;
    }

#ifndef RT_USING_DFS
    return 0;
#else
    size = write(fh, buf, len);
    if (size >= 0)
        return len - size;
    else
        return -1;
#endif
}

/*
 * Move the file position to a given offset from the file start.
 * Returns >=0 on success, <0 on failure.
 */
int _sys_seek(FILEHANDLE fh, long pos)
{
    if (fh < STDERR)
        return -1;

#ifndef RT_USING_DFS
    return -1;
#else

    /* position is relative to the start of file fh */
    return lseek(fh, pos, 0);
#endif
}

/**
 * used by tmpnam() or tmpfile()
 */
int _sys_tmpnam(char *name, int fileno, unsigned maxlength)
{
    rt_snprintf(name, maxlength, "tem%03d", fileno);
    return 1;
}

char *_sys_command_string(char *cmd, int len)
{
    /* no support */
    return RT_NULL;
}

/* This function writes a character to the console. */
void _ttywrch(int ch)
{
#ifdef RT_USING_CONSOLE
    char c;

    c = (char)ch;
    rt_kprintf(&c);
#endif
}

/* for exit() and abort() */
RT_WEAK void _sys_exit(int return_code)
{
    extern void __rt_libc_exit(int status);
    __rt_libc_exit(return_code);
    while(1);
}

/**
 * return current length of file.
 *
 * @param fh - file handle
 * @return file length, or -1 on failed
 */
long _sys_flen(FILEHANDLE fh)
{
#ifdef RT_USING_DFS
    struct stat stat;

    if (fh < STDERR)
        return -1;

    fstat(fh, &stat);
    return stat.st_size;
#else
    return -1;
#endif
}

int _sys_istty(FILEHANDLE fh)
{
    if((STDIN <= fh) && (fh <= STDERR))
        return 1;
    else
        return 0;
}

int remove(const char *filename)
{
#ifndef RT_USING_DFS
    return -1;
#else
    return unlink(filename);
#endif
}

#ifdef __MICROLIB
#include <stdio.h>

int fputc(int c, FILE *f)
{
    char ch[2] = {0};

    ch[0] = c;
    rt_kprintf(&ch[0]);
    return 1;
}

int fgetc(FILE *f)
{
#ifdef RT_USING_POSIX
    char ch;

    if (libc_stdio_read(&ch, 1) == 1)
        return ch;
#endif

    return -1;
}
#endif
