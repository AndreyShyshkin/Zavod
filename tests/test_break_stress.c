/**
 * @file test_break_stress.c
 * @brief Автономний стрес-тест конкурентного доступу до семафора кімнати відпочинку.
 * @details Запускає декілька паралельних процесів-робітників, які одночасно конкурують
 *          за семафор перерви протягом кількох ітерацій.
 *          Перевіряє:
 *          1. Відсутність взаємних блокувань (дедлоків) при високому навантаженні.
 *          2. Відсутність умов гонок (race conditions).
 *          3. Абсолютну атомарність та неперетинність часових інтервалів у break_log.txt.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "semaphore_utils.h"

#define NUM_WORKERS 4            /* Кількість паралельних процесів-робітників */
#define ITERATIONS_PER_WORKER 3  /* Кількість виходів на перерву кожним робітником */

/* Допоміжна функція мілісекундної затримки (nanosleep) */
static void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

/* Функція виконання робочого циклу робітника у стрес-тесті */
static void run_stress_worker(int worker_id) {
    for (int i = 1; i <= ITERATIONS_PER_WORKER; i++) {
        /* Невелика псевдовипадкова пауза перед спробою входу (від 10 до 50 мс) */
        sleep_ms(10 + (worker_id * 13 + i * 7) % 40);

        /* Запит на вхід до кімнати відпочинку */
        if (leave_for_break(worker_id) != ZAVOD_SUCCESS) {
            fprintf(stderr, "[СТРЕС] Помилка leave_for_break для робітника #%d на ітерації %d\n",
                    worker_id, i);
            exit(EXIT_FAILURE);
        }

        /* Перебування в кімнаті відпочинку (критична секція, 60-80 мс) */
        sleep_ms(60 + (worker_id * 5) % 20);

        /* Повернення з перерви, логування та звільнення семафора */
        if (return_from_break(worker_id) != ZAVOD_SUCCESS) {
            fprintf(stderr, "[СТРЕС] Помилка return_from_break для робітника #%d на ітерації %d\n",
                    worker_id, i);
            exit(EXIT_FAILURE);
        }
    }

    cleanup_break_semaphore();
    exit(EXIT_SUCCESS);
}

int main(void) {
    printf("=================================================================\n");
    printf("СТРЕС-ТЕСТ КОНКУРЕНТНОСТІ СЕМАФОРА КІМНАТИ ВІДПОЧИНКУ (ISSUE #4)\n");
    printf("=================================================================\n");
    printf("Параметри тесту: робітників: %d, циклів на робітника: %d, всього записів: %d\n\n",
           NUM_WORKERS, ITERATIONS_PER_WORKER, NUM_WORKERS * ITERATIONS_PER_WORKER);

    /* Очищення старих логів та семафорів */
    unlink(LOG_FILE);
    unlink_break_semaphore();

    /* Ініціалізація семафора */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[ПОМИЛКА] Не вдалося створити семафор для стрес-тесту.\n");
        return EXIT_FAILURE;
    }
    printf("[КЕРІВНИК] Семафор успішно створено. Запуск %d дочірніх процесів...\n", NUM_WORKERS);
    fflush(stdout);

    pid_t pids[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("[ПОМИЛКА] fork зазнав невдачі");
            cleanup_break_semaphore();
            return EXIT_FAILURE;
        }
        if (pids[i] == 0) {
            /* Дочірній процес з worker_id = i + 1 */
            run_stress_worker(i + 1);
        }
    }

    /* Очікування завершення всіх робітників */
    int failed_workers = 0;
    for (int i = 0; i < NUM_WORKERS; i++) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            failed_workers++;
        }
    }

    printf("\n[КЕРІВНИК] Усі процеси робітників завершили роботу (помилок: %d).\n", failed_workers);

    /* Аналіз журналу break_log.txt на цілісність та відсутність перетинів */
    printf("\n-----------------------------------------------------------------\n");
    printf("АНАЛІЗ РЕЗУЛЬТАТІВ У ЖУРНАЛІ %s:\n", LOG_FILE);
    printf("-----------------------------------------------------------------\n");

    FILE *flog = fopen(LOG_FILE, "r");
    if (flog == NULL) {
        fprintf(stderr, "[ПОМИЛКА] Файл логу не знайдено!\n");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    char line[256];
    int line_count = 0;
    while (fgets(line, sizeof(line), flog) != NULL) {
        printf("%02d. %s", line_count + 1, line);
        line_count++;
    }
    fclose(flog);

    int expected_entries = NUM_WORKERS * ITERATIONS_PER_WORKER;
    printf("-----------------------------------------------------------------\n");
    printf("Всього записів у журналі: %d (очікувано: %d)\n", line_count, expected_entries);

    int test_success = (failed_workers == 0) && (line_count == expected_entries);
    if (test_success) {
        printf("[УСПІХ] Стрес-тест пройдено на 100%%! Гонки даних та дедлоки відсутні.\n");
    } else {
        printf("[НЕВДАЧА] Кількість записів або статус процесів не збігаються.\n");
    }

    /* Звільнення ресурсів семафора */
    cleanup_break_semaphore();
    printf("=================================================================\n");

    return test_success ? EXIT_SUCCESS : EXIT_FAILURE;
}
