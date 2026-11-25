#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) {
    Sleep(ms);
}
#else
#include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* Режим ребёнка: поспать argv[3] мс и вернуть код argv[2] */
    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        sleep_ms(atoi(argv[3]));
        return atoi(argv[2]);
    }

    /* Режим ребёнка для теста 3: проверить, что аргумент дошёл без искажений */
    if (argc >= 3 && strcmp(argv[1], "--check") == 0) {
        return strcmp(argv[2], "hello \"big\" world\\") == 0 ? 0 : 1;
    }

    const char *self = argv[0];
    int all_ok = 1;
    pl_process proc;
    int code = 0;

    /* Тест 1: Фоновый запуск (1 с, код 42) + проверка pl_poll */
    const char *t1[] = { self, "--child", "42", "1000", NULL };
    int ticks = 0;
    if (pl_start(t1, &proc) == 0) {
        while (pl_poll(&proc, &code) == 0) {
            ticks++;
            sleep_ms(100);
        }
    }
    if (ticks > 3 && code == 42) {
        printf("[ OK ] Тест 1: фоновый запуск (итераций: %d, код: %d)\n", ticks, code);
    } else {
        printf("[FAIL] Тест 1\n");
        all_ok = 0;
    }

    /* Тест 2: Ожидание через pl_wait (0.3 с, код 7) */
    const char *t2[] = { self, "--child", "7", "300", NULL };
    code = 0;
    if (pl_start(t2, &proc) == 0 && pl_wait(&proc, &code) == 0 && code == 7) {
        printf("[ OK ] Тест 2: pl_wait (код: %d)\n", code);
    } else {
        printf("[FAIL] Тест 2\n");
        all_ok = 0;
    }

    /* Тест 3: Аргумент с пробелами, кавычками и обратным слешем */
    const char *t3[] = { self, "--check", "hello \"big\" world\\", NULL };
    code = -1;
    if (pl_start(t3, &proc) == 0 && pl_wait(&proc, &code) == 0 && code == 0) {
        printf("[ OK ] Тест 3: аргумент со спецсимволами\n");
    } else {
        printf("[FAIL] Тест 3\n");
        all_ok = 0;
    }

    /* Тест 4: Запуск несуществующей программы */
    const char *t4[] = { "no_such_program_12345", NULL };
    if (pl_start(t4, &proc) == -1) {
        printf("[ OK ] Тест 4: несуществующая программа вернула -1\n");
    } else {
        printf("[FAIL] Тест 4\n");
        all_ok = 0;
    }

    /* Тест 5: Код возврата 300 (на Linux обрезается до 8 бит -> 44) */
    const char *t5[] = { self, "--child", "300", "50", NULL };
    code = 0;
#ifdef _WIN32
    int expected = 300;
#else
    int expected = 44;
#endif
    if (pl_start(t5, &proc) == 0 && pl_wait(&proc, &code) == 0 && code == expected) {
        printf("[ OK ] Тест 5: код 300 -> %d\n", code);
    } else {
        printf("[FAIL] Тест 5\n");
        all_ok = 0;
    }

    return all_ok ? 0 : 1;
}
