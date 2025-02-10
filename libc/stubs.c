// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "panic.h"

int __ctype_b_loc;
int __ctype_tolower_loc;

int* __errno_location()
{
    static int _errno;
    return &_errno;
}

void atexit()
{
    /* ignore atexit handlers */
    return;
}

void clock_gettime() { LIBC_PANIC; }
void close() { LIBC_PANIC; }
void closedir() { LIBC_PANIC; }
void connect() { LIBC_PANIC; }
void dladdr() { LIBC_PANIC; }
void dlclose() { LIBC_PANIC; }
void dlerror() { LIBC_PANIC; }
void dlopen() { LIBC_PANIC; }
void dlsym() { LIBC_PANIC; }
void abort() { LIBC_PANIC; }
void fcntl() { LIBC_PANIC; }
void __fdelt_chk() { LIBC_PANIC; }
void feof() { LIBC_PANIC; }
void ferror() { LIBC_PANIC; }
void fflush() { LIBC_PANIC; }
void fgets() { LIBC_PANIC; }
void __fgets_chk() { LIBC_PANIC; }
void fileno() { LIBC_PANIC; }
void fopen() { LIBC_PANIC; }
void __fprintf_chk() { LIBC_PANIC; }
void fputc() { LIBC_PANIC; }
void fputs() { LIBC_PANIC; }
void fread() { LIBC_PANIC; }
void freeaddrinfo() { LIBC_PANIC; }
void fseek() { LIBC_PANIC; }
void fstat() { LIBC_PANIC; }
void ftell() { LIBC_PANIC; }
void fwrite() { LIBC_PANIC; }
void gai_strerror() { LIBC_PANIC; }
void getaddrinfo() { LIBC_PANIC; }
void gethostbyname() { LIBC_PANIC; }
void getnameinfo() { LIBC_PANIC; }
void getpeername() { LIBC_PANIC; }
void getpid() { LIBC_PANIC; }
void getsockname() { LIBC_PANIC; }
void getsockopt() { LIBC_PANIC; }
void gettimeofday() { LIBC_PANIC; }
void gmtime() { LIBC_PANIC; }
void ioctl() { LIBC_PANIC; }
void __isoc99_sscanf() { LIBC_PANIC; }
void listen() { LIBC_PANIC; }
void madvise() { LIBC_PANIC; }
void __memset_chk() { LIBC_PANIC; }
void mlock() { LIBC_PANIC; }
void mmap() { LIBC_PANIC; }
void mprotect() { LIBC_PANIC; }
void munmap() { LIBC_PANIC; }
void nanosleep() { LIBC_PANIC; }
void open() { LIBC_PANIC; }
void opendir() { LIBC_PANIC; }
void perror() { LIBC_PANIC; }
void read() { LIBC_PANIC; }
void readdir() { LIBC_PANIC; }
void recvfrom() { LIBC_PANIC; }
void recvmmsg() { LIBC_PANIC; }
void select() { LIBC_PANIC; }
void sendmmsg() { LIBC_PANIC; }
void sendmsg() { LIBC_PANIC; }
void sendto() { LIBC_PANIC; }
void setsockopt() { LIBC_PANIC; }
void shmat() { LIBC_PANIC; }
void shmdt() { LIBC_PANIC; }
void shmget() { LIBC_PANIC; }
void shutdown() { LIBC_PANIC; }
void sigaction() { LIBC_PANIC; }
void signal() { LIBC_PANIC; }
void socket() { LIBC_PANIC; }
void __sprintf_chk() { LIBC_PANIC; }
void stat() { LIBC_PANIC; }
void strrchr() { LIBC_PANIC; }
void strtok() { LIBC_PANIC; }
void syscall() { LIBC_PANIC; }
void sysconf() { LIBC_PANIC; }
void tcgetattr() { LIBC_PANIC; }
void tcsetattr() { LIBC_PANIC; }
void uname() { LIBC_PANIC; }
void __vfprintf_chk() { LIBC_PANIC; }
void write() { LIBC_PANIC; }
void __xpg_strerror_r() { LIBC_PANIC; }
void bind() { LIBC_PANIC; }
void accept() { LIBC_PANIC; }
void fclose() { LIBC_PANIC; }
void poll() { LIBC_PANIC; }
