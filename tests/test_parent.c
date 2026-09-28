/**
 * @file test_parent.c
 * @brief Модульні та інтеграційні тести функціоналу керівника зміни (Issue #1).
 * @details Перевіряє валідацію аргументів, генерацію унікальних номерів,
 *          роботу неіменованого каналу pipe, чергу повідомлень System V,
 *          синхронізацію готовності сигналами SIGUSR1/SIGUSR2 та очищення ресурсів.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>

#include "common.h"
#include "parent.h"
#include "semaphore_utils.h"

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        printf("  [V] ТЕСТ %02d: %s - ПРОЙДЕНО\n", g_tests_total, msg); \
        g_tests_passed++; \
    } else { \
        printf("  [X] ТЕСТ %02d: %s - НЕВДАЧА!\n", g_tests_total, msg); \
    } \
} while(0)

/* ========================================================================= */
/* 1. ТЕСТУВАННЯ ПАРСИНГУ ТА ВАЛІДАЦІЇ АРГУМЕНТІВ                            */
/* ========================================================================= */
static void test_parse_arguments(void) {
    printf("\n--- ТЕСТУВАННЯ 1: Валідація аргументів командного рядка ---\n");

    int count = 0;

    /* Коректні дані */
    char *valid_args[] = { "./factory", "42", NULL };
    ZavodErrorCode res1 = parse_arguments(2, valid_args, &count);
    TEST_ASSERT(res1 == ZAVOD_SUCCESS && count == 42, "Коректне додатне число (42)");

    /* Немає аргументів */
    char *no_args[] = { "./factory", NULL };
    TEST_ASSERT(parse_arguments(1, no_args, &count) == ZAVOD_ERR_INVALID_ARG, "Відсутній аргумент N");

    /* Від'ємне число */
    char *neg_args[] = { "./factory", "-15", NULL };
    TEST_ASSERT(parse_arguments(2, neg_args, &count) == ZAVOD_ERR_INVALID_ARG, "Від'ємне число (-15)");

    /* Нуль */
    char *zero_args[] = { "./factory", "0", NULL };
    TEST_ASSERT(parse_arguments(2, zero_args, &count) == ZAVOD_ERR_INVALID_ARG, "Нуль як кількість виробів");

    /* Нечислові символи */
    char *str_args[] = { "./factory", "abc", NULL };
    TEST_ASSERT(parse_arguments(2, str_args, &count) == ZAVOD_ERR_INVALID_ARG, "Текстовий рядок замість числа");

    /* Число з літерами в кінці */
    char *mixed_args[] = { "./factory", "100items", NULL };
    TEST_ASSERT(parse_arguments(2, mixed_args, &count) == ZAVOD_ERR_INVALID_ARG, "Число із суфіксом (100items)");
}

/* ========================================================================= */
/* 2. ТЕСТУВАННЯ ГЕНЕРАЦІЇ СЕРІЙНИХ НОМЕРІВ                                  */
/* ========================================================================= */
static void test_serial_generation(void) {
    printf("\n--- ТЕСТУВАННЯ 2: Генерація унікальних серійних номерів ---\n");

    const int N = 200;
    uint32_t serials[200];
    bool all_valid = true;
    bool all_unique = true;

    for (int i = 0; i < N; i++) {
        serials[i] = generate_serial_number((uint32_t)(i + 1));
        if (serials[i] == 0) {
            all_valid = false;
        }
    }
    TEST_ASSERT(all_valid, "Усі серійні номери є ненульовими та додатними");

    /* Перевірка на унікальність */
    for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
            if (serials[i] == serials[j]) {
                all_unique = false;
                break;
            }
        }
    }
    TEST_ASSERT(all_unique, "Усі 200 згенерованих номерів є строго унікальними");
}

/* ========================================================================= */
/* 3. ТЕСТУВАННЯ НЕІМЕНОВАНОГО КАНАЛУ PIPE                                   */
/* ========================================================================= */
static void test_pipe_communication(void) {
    printf("\n--- ТЕСТУВАННЯ 3: Передача деталей через unnamed pipe ---\n");

    int pipe_fd[2];
    ZavodErrorCode p_res = create_pipe(pipe_fd);
    TEST_ASSERT(p_res == ZAVOD_SUCCESS, "Створення неіменованого каналу pipe()");

    const int ITEMS_COUNT = 10;
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork error");
        return;
    }

    if (pid == 0) {
        /* Дочірній процес-читач (імітує робітника 1) */
        close(pipe_fd[1]);

        PipeItem item;
        int received = 0;
        bool valid_sequence = true;

        while (read(pipe_fd[0], &item, sizeof(PipeItem)) > 0) {
            received++;
            if (item.id != (uint32_t)received || item.serial_number == 0) {
                valid_sequence = false;
            }
        }

        close(pipe_fd[0]);
        exit((received == ITEMS_COUNT && valid_sequence) ? 0 : 1);
    }

    /* Батьківський процес */
    close(pipe_fd[0]);
    ZavodErrorCode s_res = send_items_via_pipe(pipe_fd[1], ITEMS_COUNT);
    TEST_ASSERT(s_res == ZAVOD_SUCCESS, "Запис N деталей у pipe та закриття на EOF");

    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "Дочірній процес успішно прочитав усі 10 деталей без втрат та отримав EOF");
}

/* ========================================================================= */
/* 4. ТЕСТУВАННЯ ЧЕРГИ ПОВІДОМЛЕНЬ SYSTEM V                                 */
/* ========================================================================= */
static void test_message_queue_lifecycle(void) {
    printf("\n--- ТЕСТУВАННЯ 4: Створення, читання та очищення черги повідомлень ---\n");

    int msqid = init_message_queue();
    TEST_ASSERT(msqid >= 0, "Створення та підключення черги повідомлень msgget()");

    /* Надсилаємо тестове повідомлення з метрикою якості */
    struct msg_buffer send_msg;
    send_msg.msg_type = MSG_TYPE_METRIC;
    send_msg.metric.serial_number = 12345;
    send_msg.metric.quality_score = 85;
    strncpy(send_msg.msg_text, "Тестова деталь високої якості", sizeof(send_msg.msg_text) - 1);

    int snd_res = msgsnd(msqid, &send_msg, sizeof(struct msg_buffer) - sizeof(long), 0);
    TEST_ASSERT(snd_res == 0, "Відправка повідомлення в чергу через msgsnd()");

    /* Надсилаємо маркер завершення */
    struct msg_buffer stop_msg;
    stop_msg.msg_type = MSG_TYPE_STOP;
    stop_msg.metric.serial_number = 0;
    stop_msg.metric.quality_score = 0;
    stop_msg.msg_text[0] = '\0';
    msgsnd(msqid, &stop_msg, sizeof(struct msg_buffer) - sizeof(long), 0);

    /* Зчитуємо повідомлення функцією керівника */
    int passed = 0;
    int defects = 0;
    ZavodErrorCode r_res = read_results_from_queue(msqid, 0, 1, &passed, &defects);
    TEST_ASSERT(r_res == ZAVOD_SUCCESS && passed == 1,
                "Успішне зчитування метрики та розпізнавання MSG_TYPE_STOP");

    /* Очищення черги повідомлень */
    cleanup_ipc_resources(msqid);

    /* Перевіряємо, що черга дійсно видалена з пам'яті ядра */
    key_t key = ftok(FTOK_PATH, FTOK_PROJ_ID_MSG);
    int check_qid = msgget(key, 0);
    TEST_ASSERT(check_qid == -1, "Чергу повідомлень успішно видалено з ядра (IPC_RMID)");
}

/* ========================================================================= */
/* 5. ТЕСТУВАННЯ СИНХРОНІЗАЦІЇ СИГНАЛАМИ ГОТОВНОСТІ                          */
/* ========================================================================= */
static void test_signal_synchronization(void) {
    printf("\n--- ТЕСТУВАННЯ 5: Синхронізація готовності сигналами (SIGUSR1, SIGUSR2) ---\n");

    ZavodErrorCode s_res = setup_signal_handlers();
    TEST_ASSERT(s_res == ZAVOD_SUCCESS, "Налаштування sigaction та блокування сигналів перед fork");

    pid_t ppid = getpid();

    /* Створюємо імітатор Робітника 1 */
    pid_t p1 = fork();
    if (p1 == 0) {
        usleep(20000); /* 20 мс затримка */
        kill(ppid, SIG_WORKER1_READY);
        exit(0);
    }

    /* Створюємо імітатор Робітника 2 */
    pid_t p2 = fork();
    if (p2 == 0) {
        usleep(40000); /* 40 мс затримка */
        kill(ppid, SIG_WORKER2_READY);
        exit(0);
    }

    /* Керівник очікує обидва сигнали */
    ZavodErrorCode w_res = wait_for_workers_ready();
    TEST_ASSERT(w_res == ZAVOD_SUCCESS, "Очікування та успішний прийом обох сигналів (SIGUSR1 + SIGUSR2)");

    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
}

int main(void) {
    printf("=================================================================\n");
    printf("     АВТОМАТИЗОВАНЕ ТЕСТУВАННЯ МОДУЛЯ КЕРІВНИКА (ISSUE #1)       \n");
    printf("=================================================================\n");

    test_parse_arguments();
    test_serial_generation();
    test_pipe_communication();
    test_message_queue_lifecycle();
    test_signal_synchronization();

    printf("\n=================================================================\n");
    printf("РЕЗУЛЬТАТ ТЕСТУВАННЯ ISSUE #1: %d/%d ТЕСТІВ УСПІШНО ПРОЙДЕНО!\n",
           g_tests_passed, g_tests_total);
    printf("=================================================================\n\n");

    return (g_tests_passed == g_tests_total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
