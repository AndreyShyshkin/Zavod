/**
 * @file parent.c
 * @brief Реалізація функцій оркестрації керівника зміни (батьківського процесу, Issue #1).
 * @details Включає валідацію вхідних параметрів, налаштування сигналів, роботу
 *          з unnamed pipe, запуск дітей через fork() та обробку черги повідомлень.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <signal.h>

#include "parent.h"
#include "common.h"

/* Глобальні атомарні прапорці готовності робітників (змінюються в обробнику сигналів) */
static volatile sig_atomic_t g_worker1_ready = 0;
static volatile sig_atomic_t g_worker2_ready = 0;
static sigset_t g_orig_sigmask;
static bool g_sigmask_saved = false;

/**
 * @brief Обробник сигналів готовності SIG_WORKER1_READY та SIG_WORKER2_READY.
 * @param sig Номер отриманого сигналу.
 */
static void handle_worker_ready_signal(int sig) {
    if (sig == SIG_WORKER1_READY) {
        g_worker1_ready = 1;
    } else if (sig == SIG_WORKER2_READY) {
        g_worker2_ready = 1;
    }
}

/**
 * @brief Налаштовує обробники сигналів готовності робітників та блокування перед fork.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_SIGNAL при помилці.
 */
ZavodErrorCode setup_signal_handlers(void) {
    g_worker1_ready = 0;
    g_worker2_ready = 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_worker_ready_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIG_WORKER1_READY, &sa, NULL) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Не вдалося налаштувати обробник SIGUSR1");
        return ZAVOD_ERR_SIGNAL;
    }

    if (sigaction(SIG_WORKER2_READY, &sa, NULL) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Не вдалося налаштувати обробник SIGUSR2");
        return ZAVOD_ERR_SIGNAL;
    }

    /* Блокуємо сигнали перед fork, щоб жоден сигнал не загубився до очікування */
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIG_WORKER1_READY);
    sigaddset(&block_mask, SIG_WORKER2_READY);

    if (sigprocmask(SIG_BLOCK, &block_mask, &g_orig_sigmask) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка блокування сигналів у sigprocmask");
        return ZAVOD_ERR_SIGNAL;
    }
    g_sigmask_saved = true;

    return ZAVOD_SUCCESS;
}

/**
 * @brief Очікує надходження сигналів готовності від обох робітників.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_SIGNAL при помилці.
 */
ZavodErrorCode wait_for_workers_ready(void) {
    if (!g_sigmask_saved) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Обробники сигналів не були ініціалізовані.\n");
        return ZAVOD_ERR_SIGNAL;
    }

    printf("[КЕРІВНИК] Очікування сигналів готовності від робітників (SIGUSR1, SIGUSR2)...\n");
    fflush(stdout);

    bool worker1_logged = false;
    bool worker2_logged = false;

    while (!g_worker1_ready || !g_worker2_ready) {
        /*
         * sigsuspend тимчасово замінює маску сигналів на g_orig_sigmask
         * та блокує процес до отримання будь-якого розблокованого сигналу.
         */
        sigsuspend(&g_orig_sigmask);

        if (g_worker1_ready && !worker1_logged) {
            printf("[КЕРІВНИК] Робітник 1 готовий (отримано сигнал готовності SIGUSR1).\n");
            fflush(stdout);
            worker1_logged = true;
        }

        if (g_worker2_ready && !worker2_logged) {
            printf("[КЕРІВНИК] Робітник 2 готовий (отримано сигнал готовності SIGUSR2).\n");
            fflush(stdout);
            worker2_logged = true;
        }
    }

    printf("[КЕРІВНИК] Обидва робітники підтвердили готовність до роботи.\n");
    fflush(stdout);

    /* Відновлюємо стандартну маску сигналів процесу */
    if (sigprocmask(SIG_SETMASK, &g_orig_sigmask, NULL) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка відновлення маски сигналів");
        return ZAVOD_ERR_SIGNAL;
    }

    return ZAVOD_SUCCESS;
}

/**
 * @brief Перевіряє та зчитує кількість виробів із аргументів командного рядка.
 * @param argc Кількість аргументів програми.
 * @param argv Масив аргументів командного рядка.
 * @param out_count Вказівник для збереження розпарсеного числа N (N > 0).
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_INVALID_ARG при помилці.
 */
ZavodErrorCode parse_arguments(int argc, char *argv[], int *out_count) {
    if (out_count == NULL) {
        return ZAVOD_ERR_INVALID_ARG;
    }

    if (argc < 2 || argv == NULL || argv[1] == NULL) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Не передано обов'язковий аргумент <N> (кількість деталей).\n");
        fprintf(stderr, "Використання: %s <N>\n", (argv && argv[0]) ? argv[0] : "./factory");
        fprintf(stderr, "Де N — додатне ціле число більше нуля (наприклад: ./factory 10).\n");
        return ZAVOD_ERR_INVALID_ARG;
    }

    /* Перевірка на порожній рядок */
    if (argv[1][0] == '\0') {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Аргумент кількості деталей не може бути порожнім.\n");
        return ZAVOD_ERR_INVALID_ARG;
    }

    char *endptr = NULL;
    errno = 0;
    long val = strtol(argv[1], &endptr, 10);

    /* Перевірка на помилки перетворення, діапазону та зайві символи */
    if (errno != 0 || *endptr != '\0' || val <= 0 || val > 1000000) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Некоректне значення кількості деталей: '%s'.\n", argv[1]);
        fprintf(stderr, "Значення N має бути додатним цілим числом у діапазоні [1..1000000].\n");
        return ZAVOD_ERR_INVALID_ARG;
    }

    *out_count = (int)val;
    return ZAVOD_SUCCESS;
}
