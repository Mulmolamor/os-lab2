#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Сборка командной строки с экранированием пробелов, кавычек и слешей */
static char *make_cmd_line(const char *const argv[]) {
    size_t cap = 256, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    for (size_t i = 0; argv[i] != NULL; ++i) {
        const char *s = argv[i];
        int quote = (s[0] == '\0' || strpbrk(s, " \t\"") != NULL);

        /* Запас памяти под текущий аргумент с запасом x2 на экранирование */
        size_t need = len + strlen(s) * 2 + 4;
        if (need > cap) {
            cap = need * 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }

        if (i > 0) buf[len++] = ' ';
        if (!quote) {
            strcpy(buf + len, s);
            len += strlen(s);
            continue;
        }

        buf[len++] = '"';
        for (size_t j = 0; s[j] != '\0'; ) {
            size_t bs = 0;
            while (s[j] == '\\') { bs++; j++; }

            if (s[j] == '\0') {
                memset(buf + len, '\\', bs * 2);
                len += bs * 2;
            } else if (s[j] == '"') {
                memset(buf + len, '\\', bs * 2 + 1);
                len += bs * 2 + 1;
                buf[len++] = '"';
                j++;
            } else {
                memset(buf + len, '\\', bs);
                len += bs;
                buf[len++] = s[j++];
            }
        }
        buf[len++] = '"';
    }

    buf[len] = '\0';
    return buf;
}

int pl_start(const char *const argv[], pl_process *proc) {
    if (!argv || !argv[0] || !proc) return -1;

    char *cmd = make_cmd_line(argv);
    if (!cmd) return -1;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) return -1;

    CloseHandle(pi.hThread);
    proc->handle = (intptr_t)pi.hProcess;
    proc->pid = (long)pi.dwProcessId;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code) {
    if (!proc || !proc->handle) return -1;

    HANDLE h = (HANDLE)proc->handle;
    DWORD code = 0;
    if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(h, &code)) {
        CloseHandle(h);
        proc->handle = 0;
        return -1;
    }

    CloseHandle(h);
    proc->handle = 0;
    if (exit_code) *exit_code = (int)code;
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code) {
    if (!proc || !proc->handle) return -1;

    HANDLE h = (HANDLE)proc->handle;
    DWORD res = WaitForSingleObject(h, 0);
    if (res == WAIT_TIMEOUT) return 0;

    DWORD code = 0;
    if (res != WAIT_OBJECT_0 || !GetExitCodeProcess(h, &code)) {
        CloseHandle(h);
        proc->handle = 0;
        return -1;
    }

    CloseHandle(h);
    proc->handle = 0;
    if (exit_code) *exit_code = (int)code;
    return 1;
}

#else /* POSIX */

#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

static int get_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

int pl_start(const char *const argv[], pl_process *proc) {
    if (!argv || !argv[0] || !proc) return -1;

    pid_t pid;
    if (posix_spawnp(&pid, argv[0], NULL, NULL, (char *const *)argv, environ) != 0) {
        return -1;
    }

    proc->handle = 0;
    proc->pid = (long)pid;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code) {
    if (!proc || proc->pid <= 0) return -1;

    int status = 0;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0) return -1;

    proc->pid = -1;
    if (exit_code) *exit_code = get_code(status);
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code) {
    if (!proc || proc->pid <= 0) return -1;

    int status = 0;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);

    if (r < 0) return -1;
    if (r == 0) return 0;

    proc->pid = -1;
    if (exit_code) *exit_code = get_code(status);
    return 1;
}

#endif
