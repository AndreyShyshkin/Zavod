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
#include <sys/ipc.h>
#include <sys/msg.h>

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

/**
 * @brief Ініціалізує генератор випадкових чисел керівника зерном (time ^ pid).
 */
void init_random_generator(void) {
    srand((unsigned int)(time(NULL) ^ getpid()));
}

/**
 * @brief Генерує унікальний псевдовипадковий серійний номер виробу.
 * @param index Порядковий номер деталі на конвеєрі (1..N).
 * @return Згенерований серійний номер uint32_t.
 */
uint32_t generate_serial_number(uint32_t index) {
    static bool seeded = false;
    if (!seeded) {
        init_random_generator();
        seeded = true;
    }

    /*
     * Формуємо унікальний серійний номер:
     * Базова частина: порядковий номер * 1000.
     * Випадкова складова: псевдовипадкове число [100..999].
     * Це гарантує взаємну унікальність для кожного виробу в партії.
     */
    uint32_t random_part = (uint32_t)(rand() % 900 + 100);
    return (index * 1000U) + random_part;
}

/**
 * @brief Створює неіменований канал pipe для передачі виробів першому робітнику.
 * @param pipe_fd Масив із двох дескрипторів: [0] для читання, [1] для запису.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_PIPE або ZAVOD_ERR_INVALID_ARG при помилці.
 */
ZavodErrorCode create_pipe(int pipe_fd[2]) {
    if (pipe_fd == NULL) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Вказівник pipe_fd є NULL.\n");
        return ZAVOD_ERR_INVALID_ARG;
    }

    if (pipe(pipe_fd) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Не вдалося створити неіменований канал pipe");
        return ZAVOD_ERR_PIPE;
    }

    printf("[КЕРІВНИК] Створено неіменований канал pipe (read fd: %d, write fd: %d).\n",
           pipe_fd[0], pipe_fd[1]);
    fflush(stdout);

    return ZAVOD_SUCCESS;
}

/**
 * @brief Генерує та записує N деталей у неіменований канал, після чого закриває його для передачі EOF.
 * @param write_fd Дескриптор неіменованого каналу для запису.
 * @param count Кількість деталей (N) для передачі.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_PIPE або ZAVOD_ERR_INVALID_ARG при помилці.
 */
ZavodErrorCode send_items_via_pipe(int write_fd, int count) {
    if (write_fd < 0 || count <= 0) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Некоректні параметри для send_items_via_pipe (fd: %d, count: %d).\n",
                write_fd, count);
        return ZAVOD_ERR_INVALID_ARG;
    }

    printf("[КЕРІВНИК] Початок генерації та передачі %d деталей у pipe...\n", count);
    fflush(stdout);

    for (int i = 1; i <= count; i++) {
        PipeItem item;
        item.id = (uint32_t)i;
        item.serial_number = generate_serial_number((uint32_t)i);

        ssize_t bytes_written = write(write_fd, &item, sizeof(PipeItem));
        if (bytes_written != (ssize_t)sizeof(PipeItem)) {
            perror("[КЕРІВНИК - ПОМИЛКА] Помилка запису елемента в unnamed pipe");
            close(write_fd);
            return ZAVOD_ERR_PIPE;
        }

        if (count <= 20 || i % (count / 10 == 0 ? 1 : count / 10) == 0 || i == count) {
            printf("[КЕРІВНИК] Записано в pipe: деталь #%u (серійний номер %u)\n",
                   item.id, item.serial_number);
            fflush(stdout);
        }
    }

    /* Закриваємо дескриптор запису, сигналізуючи EOF для робітника 1 */
    if (close(write_fd) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка закриття write_fd у pipe");
        return ZAVOD_ERR_PIPE;
    }

    printf("[КЕРІВНИК] Усі %d деталей успішно відправлено. Кінчик pipe закрито (надіслано EOF).\n", count);
    fflush(stdout);

    return ZAVOD_SUCCESS;
}

/**
 * @brief Створює або підключається до черги повідомлень System V через ftok.
 * @return Дескриптор черги (msqid >= 0) у разі успіху або -1 при помилці.
 */
int init_message_queue(void) {
    key_t key = ftok(FTOK_PATH, FTOK_PROJ_ID_MSG);
    if (key == (key_t)-1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка генерації ключа ftok для черги повідомлень");
        return -1;
    }

    int msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка виклику msgget для створення черги повідомлень");
        return -1;
    }

    /* Очищуємо залишкові повідомлення від попередніх запусків (drain queue) */
    struct msg_buffer dummy;
    while (msgrcv(msqid, &dummy, sizeof(struct msg_buffer) - sizeof(long), 0, IPC_NOWAIT) > 0) {
        /* Дренування старих повідомлень */
    }

    printf("[КЕРІВНИК] Черга повідомлень IPC підготовлена (msqid: %d, key: 0x%08x).\n",
           msqid, (unsigned int)key);
    fflush(stdout);

    return msqid;
}
