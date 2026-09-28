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
#include <sys/wait.h>

#include "parent.h"
#include "common.h"
#include "semaphore_utils.h"

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

/**
 * @brief Породжує два дочірні процеси (Робітник 1 та Робітник 2) через fork та налаштовує pipe.
 * @param pipe_fd Масив дескрипторів неіменованого каналу.
 * @param out_pid1 Вказівник для збереження PID першого дочірнього процесу.
 * @param out_pid2 Вказівник для збереження PID другого дочірнього процесу.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_FORK при помилці.
 */
ZavodErrorCode launch_workers(int pipe_fd[2], pid_t *out_pid1, pid_t *out_pid2) {
    if (pipe_fd == NULL || out_pid1 == NULL || out_pid2 == NULL) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Некоректні аргументи у launch_workers.\n");
        return ZAVOD_ERR_INVALID_ARG;
    }

    /* 1. Запуск дочірнього процесу №1: Робітник 1 (Перевіряючий, Issue #2) */
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка виклику fork для Робітника 1");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return ZAVOD_ERR_FORK;
    }

    if (pid1 == 0) {
        /* Дочірній процес №1 */
        close(pipe_fd[1]); /* Закриваємо невикористовуваний кінець запису */

        /* Перенаправляємо дескриптор читання pipe на STDIN_FILENO для зручності */
        if (dup2(pipe_fd[0], STDIN_FILENO) == -1) {
            perror("[РОБІТНИК 1 - ПОМИЛКА] Помилка дублювання дескриптора dup2");
            close(pipe_fd[0]);
            exit(EXIT_FAILURE);
        }

        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", pipe_fd[0]);

        /* Відновлюємо маску сигналів перед викликом exec */
        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        sigprocmask(SIG_SETMASK, &empty_mask, NULL);

        /* Запуск виконуваного файлу робітника 1 */
        execl("./worker1", "worker1", fd_str, NULL);

        /* Якщо execl повернувся — сталася помилка (наприклад, бінарник ще не зібрано) */
        perror("[РОБІТНИК 1 - ПОМИЛКА] Не вдалося виконати ./worker1 (execl)");
        close(pipe_fd[0]);
        exit(EXIT_FAILURE);
    }

    *out_pid1 = pid1;
    printf("[КЕРІВНИК] Запущено процес Робітника 1 (Перевіряючий, PID: %d).\n", pid1);
    fflush(stdout);

    /* 2. Запуск дочірнього процесу №2: Робітник 2 (Тестувальник, Issue #3) */
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка виклику fork для Робітника 2");
        /* Запобігаємо появі процесу-зомбі */
        kill(pid1, SIGTERM);
        waitpid(pid1, NULL, 0);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return ZAVOD_ERR_FORK;
    }

    if (pid2 == 0) {
        /* Дочірній процес №2: йому не потрібен unnamed pipe керівника */
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        /* Відновлюємо маску сигналів перед викликом exec */
        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        sigprocmask(SIG_SETMASK, &empty_mask, NULL);

        /* Запуск виконуваного файлу робітника 2 */
        execl("./worker2", "worker2", NULL);

        /* Якщо execl повернувся — сталася помилка */
        perror("[РОБІТНИК 2 - ПОМИЛКА] Не вдалося виконати ./worker2 (execl)");
        exit(EXIT_FAILURE);
    }

    *out_pid2 = pid2;
    printf("[КЕРІВНИК] Запущено процес Робітника 2 (Тестувальник, PID: %d).\n", pid2);
    fflush(stdout);

    /*
     * Батьківський процес закриває кінець читання pipe_fd[0],
     * оскільки він лише записуватиме у pipe_fd[1].
     */
    if (close(pipe_fd[0]) == -1) {
        perror("[КЕРІВНИК - ПОМИЛКА] Помилка закриття pipe_fd[0] у батька");
    }

    return ZAVOD_SUCCESS;
}

/**
 * @brief Читає в циклі повідомлення з черги IPC, виводить результати та рахує статистику.
 * @param msqid Дескриптор черги повідомлень System V.
 * @param pid2 PID процесу Робітника 2 (для моніторингу його завершення).
 * @param total_count Загальна запланована кількість виробів (N).
 * @param out_passed Вказівник для збереження кількості протестованих/пройдених деталей.
 * @param out_defects Вказівник для збереження кількості відсіяного браку.
 * @return ZAVOD_SUCCESS у разі успіху, відповідний код помилки при збої.
 */
ZavodErrorCode read_results_from_queue(int msqid, pid_t pid2, int total_count, int *out_passed, int *out_defects) {
    if (msqid < 0 || total_count <= 0 || out_passed == NULL || out_defects == NULL) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Некоректні аргументи у read_results_from_queue.\n");
        return ZAVOD_ERR_INVALID_ARG;
    }

    *out_passed = 0;
    *out_defects = 0;

    printf("[КЕРІВНИК] Очікування та зчитування результатів тестування з черги повідомлень (msqid: %d)...\n", msqid);
    fflush(stdout);

    struct msg_buffer msg;
    bool worker2_finished = false;

    while (1) {
        /*
         * Використовуємо IPC_NOWAIT для неблокуючого опитування черги
         * із паралельною перевіркою життєздатності процесу Робітника 2.
         */
        ssize_t res = msgrcv(msqid, &msg, sizeof(struct msg_buffer) - sizeof(long), 0, IPC_NOWAIT);

        if (res > 0) {
            /* Отримано повідомлення з черги */
            if (msg.msg_type == MSG_TYPE_STOP) {
                printf("[КЕРІВНИК] Отримано маркер завершення передачі (MSG_TYPE_STOP) від Робітника 2.\n");
                fflush(stdout);
                break;
            }

            if (msg.msg_type == MSG_TYPE_METRIC) {
                (*out_passed)++;
                uint32_t serial = msg.metric.serial_number;
                uint8_t score = msg.metric.quality_score;

                printf("[КЕРІВНИК - РЕЗУЛЬТАТ] Виріб #%d: серійний номер = %u, оцінка якості = %u/100 (статус: %s)%s%s\n",
                       *out_passed, serial, score,
                       (score >= QUALITY_SCORE_DEFECT_THRESHOLD ? "СТАНДАРТ" : "БРАК"),
                       (msg.msg_text[0] != '\0' ? " | Опис: " : ""),
                       (msg.msg_text[0] != '\0' ? msg.msg_text : ""));
                fflush(stdout);

                if (*out_passed >= total_count) {
                    /* Отримано результати для всіх виробів партії */
                    break;
                }
            }
        } else {
            /* Якщо msgrcv повернув помилку */
            if (errno == ENOMSG) {
                /* Черга наразі порожня. Перевіряємо стан процесу Робітника 2 */
                if (pid2 > 0) {
                    int status = 0;
                    pid_t wait_res = waitpid(pid2, &status, WNOHANG);
                    if (wait_res == pid2) {
                        /* Робітник 2 завершив роботу. Дренуємо останні можливі залишки і виходимо */
                        worker2_finished = true;
                        while (msgrcv(msqid, &msg, sizeof(struct msg_buffer) - sizeof(long), 0, IPC_NOWAIT) > 0) {
                            if (msg.msg_type == MSG_TYPE_METRIC) {
                                (*out_passed)++;
                                printf("[КЕРІВНИК - РЕЗУЛЬТАТ] Виріб #%d: серійний номер = %u, оцінка якості = %u/100\n",
                                       *out_passed, msg.metric.serial_number, msg.metric.quality_score);
                                fflush(stdout);
                            }
                        }
                        break;
                    }
                }

                if (worker2_finished) {
                    break;
                }

                /* Коротка пауза (10 мс) перед наступною перевіркою черги */
                usleep(10000);
            } else if (errno == EINTR) {
                /* Системний виклик перервано сигналом */
                continue;
            } else {
                /* Інша помилка черги (наприклад, видалення або відсутність доступу) */
                perror("[КЕРІВНИК - ПОМИЛКА] Помилка читання з черги msgrcv");
                break;
            }
        }
    }

    /* Визначаємо кількість відсіяного браку */
    *out_defects = total_count - *out_passed;
    if (*out_defects < 0) {
        *out_defects = 0;
    }

    printf("[КЕРІВНИК] Зчитування черги повідомлень завершено. Успішно отримано: %d виробів.\n", *out_passed);
    fflush(stdout);

    return ZAVOD_SUCCESS;
}

/**
 * @brief Очікує завершення обох дочірніх процесів та виводить фінальну статистику зміни.
 * @param pid1 PID процесу Робітника 1.
 * @param pid2 PID процесу Робітника 2.
 * @param total_count Загальна кількість виробів (N).
 * @param passed_count Кількість виробів, що успішно пройшли повний контроль.
 * @return ZAVOD_SUCCESS у разі успіху.
 */
ZavodErrorCode wait_and_print_summary(pid_t pid1, pid_t pid2, int total_count, int passed_count) {
    int status1 = 0, status2 = 0;
    char worker1_status_str[64] = "Завершено успішно";
    char worker2_status_str[64] = "Завершено успішно";

    printf("\n[КЕРІВНИК] Очікування завершення роботи дочірніх процесів (waitpid)...\n");
    fflush(stdout);

    /* Очікування завершення Робітника 1 */
    if (pid1 > 0) {
        if (waitpid(pid1, &status1, 0) == -1) {
            perror("[КЕРІВНИК - ПОМИЛКА] Помилка waitpid для Робітника 1");
            snprintf(worker1_status_str, sizeof(worker1_status_str), "Помилка очікування");
        } else {
            if (WIFEXITED(status1)) {
                int exit_code = WEXITSTATUS(status1);
                printf("[КЕРІВНИК] Робітник 1 (PID %d) завершив роботу з кодом виходу: %d.\n",
                       pid1, exit_code);
                snprintf(worker1_status_str, sizeof(worker1_status_str), "Код виходу %d", exit_code);
            } else if (WIFSIGNALED(status1)) {
                int term_sig = WTERMSIG(status1);
                printf("[КЕРІВНИК] Робітник 1 (PID %d) завершився через сигнал: %d.\n",
                       pid1, term_sig);
                snprintf(worker1_status_str, sizeof(worker1_status_str), "Сигнал %d", term_sig);
            }
        }
    }

    /* Очікування завершення Робітника 2 */
    if (pid2 > 0) {
        if (waitpid(pid2, &status2, 0) == -1) {
            perror("[КЕРІВНИК - ПОМИЛКА] Помилка waitpid для Робітника 2");
            snprintf(worker2_status_str, sizeof(worker2_status_str), "Помилка очікування");
        } else {
            if (WIFEXITED(status2)) {
                int exit_code = WEXITSTATUS(status2);
                printf("[КЕРІВНИК] Робітник 2 (PID %d) завершив роботу з кодом виходу: %d.\n",
                       pid2, exit_code);
                snprintf(worker2_status_str, sizeof(worker2_status_str), "Код виходу %d", exit_code);
            } else if (WIFSIGNALED(status2)) {
                int term_sig = WTERMSIG(status2);
                printf("[КЕРІВНИК] Робітник 2 (PID %d) завершився через сигнал: %d.\n",
                       pid2, term_sig);
                snprintf(worker2_status_str, sizeof(worker2_status_str), "Сигнал %d", term_sig);
            }
        }
    }

    int defect_count = total_count - passed_count;
    if (defect_count < 0) {
        defect_count = 0;
    }
    double passed_pct = (total_count > 0) ? ((double)passed_count * 100.0 / total_count) : 0.0;
    double defect_pct = (total_count > 0) ? ((double)defect_count * 100.0 / total_count) : 0.0;

    printf("\n=================================================================\n");
    printf("                  ПІДСУМКОВА СТАТИСТИКА ЗМІНИ                     \n");
    printf("=================================================================\n");
    printf(" Загальна кількість деталей (план N):    %d\n", total_count);
    printf(" Успішно пройшли повний контроль якості: %d (%.1f%%)\n", passed_count, passed_pct);
    printf(" Відсіяно як брак (дефектні вироби):     %d (%.1f%%)\n", defect_count, defect_pct);
    printf(" Стан процесу Робітника 1 (PID %d):      %s\n", pid1, worker1_status_str);
    printf(" Стан процесу Робітника 2 (PID %d):      %s\n", pid2, worker2_status_str);
    printf("=================================================================\n\n");
    fflush(stdout);

    return ZAVOD_SUCCESS;
}

/**
 * @brief Видаляє чергу повідомлень та очищує IPC-ресурси керівника.
 * @param msqid Дескриптор черги повідомлень System V для видалення.
 */
void cleanup_ipc_resources(int msqid) {
    printf("[КЕРІВНИК] Очищення системних IPC-ресурсів...\n");
    fflush(stdout);

    /* 1. Видалення черги повідомлень System V через msgctl(IPC_RMID) */
    if (msqid >= 0) {
        if (msgctl(msqid, IPC_RMID, NULL) == -1) {
            perror("[КЕРІВНИК - ПОМИЛКА] Помилка видалення черги повідомлень msgctl(IPC_RMID)");
        } else {
            printf("[КЕРІВНИК] Чергу повідомлень (msqid: %d) успішно видалено з ядра.\n", msqid);
            fflush(stdout);
        }
    }

    /* 2. Закриття та відв'язування семафора кімнати відпочинку */
    cleanup_break_semaphore();

    printf("[КЕРІВНИК] Усі системні IPC-ресурси успішно прибрано.\n");
    fflush(stdout);
}

/**
 * @brief Головний керуючий цикл оркестрації керівника заводу.
 * @param argc Кількість аргументів програми.
 * @param argv Масив аргументів командного рядка.
 * @return EXIT_SUCCESS або EXIT_FAILURE.
 */
int run_supervisor(int argc, char *argv[]) {
    printf("=================================================================\n");
    printf("     КЕРІВНИК ЗМІНИ ЗАВОДУ (SUPERVISOR PROCESS, ISSUE #1)        \n");
    printf("=================================================================\n");
    fflush(stdout);

    /* 1. Зчитування та валідація аргументів командного рядка */
    int count = 0;
    if (parse_arguments(argc, argv, &count) != ZAVOD_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[КЕРІВНИК] Заплановано випуск та перевірку деталей: %d шт.\n", count);
    fflush(stdout);

    /* 2. Ініціалізація семафора перерв (Issue #4) */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Не вдалося ініціалізувати семафор перерв.\n");
        return EXIT_FAILURE;
    }

    /* 3. Ініціалізація черги повідомлень (Issue #1 & #3) */
    int msqid = init_message_queue();
    if (msqid < 0) {
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    /* 4. Налаштування обробників сигналів та блокування перед fork */
    if (setup_signal_handlers() != ZAVOD_SUCCESS) {
        cleanup_ipc_resources(msqid);
        return EXIT_FAILURE;
    }

    /* 5. Створення неіменованого каналу pipe */
    int pipe_fd[2];
    if (create_pipe(pipe_fd) != ZAVOD_SUCCESS) {
        cleanup_ipc_resources(msqid);
        return EXIT_FAILURE;
    }

    /* 6. Породження дочірніх процесів через fork() */
    pid_t pid1 = 0, pid2 = 0;
    if (launch_workers(pipe_fd, &pid1, &pid2) != ZAVOD_SUCCESS) {
        cleanup_ipc_resources(msqid);
        return EXIT_FAILURE;
    }

    /* 7. Очікування сигналів готовності від обох робітників */
    if (wait_for_workers_ready() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Збій під час синхронізації готовності.\n");
        kill(pid1, SIGTERM);
        kill(pid2, SIGTERM);
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
        close(pipe_fd[1]);
        cleanup_ipc_resources(msqid);
        return EXIT_FAILURE;
    }

    /* 8. Генерація та відправка серійних номерів у pipe (і закриття на EOF) */
    if (send_items_via_pipe(pipe_fd[1], count) != ZAVOD_SUCCESS) {
        fprintf(stderr, "[КЕРІВНИК - ПОМИЛКА] Збій під час запису номерів у pipe.\n");
        kill(pid1, SIGTERM);
        kill(pid2, SIGTERM);
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
        cleanup_ipc_resources(msqid);
        return EXIT_FAILURE;
    }

    /* 9. Зчитування фінальних результатів із черги повідомлень */
    int passed_count = 0;
    int defect_count = 0;
    if (read_results_from_queue(msqid, pid2, count, &passed_count, &defect_count) != ZAVOD_SUCCESS) {
        fprintf(stderr, "[КЕРІВНИК - УВАГА] Зчитування черги повідомлень завершилося з попередженням.\n");
    }

    /* 10. Очікування завершення дочірніх процесів та підсумок */
    wait_and_print_summary(pid1, pid2, count, passed_count);

    /* 11. Фінальне очищення системних ресурсів */
    cleanup_ipc_resources(msqid);

    printf("[КЕРІВНИК] Роботу зміни успішно завершено. Усі ресурси звільнено.\n");
    printf("=================================================================\n");
    fflush(stdout);

    return EXIT_SUCCESS;
}
