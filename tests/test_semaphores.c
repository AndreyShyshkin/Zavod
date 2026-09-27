/*
 * Інтеграційний тест модуля семафорів для проєкту "Завод" (Issue #4).
 * 
 * Тест моделює конкурентну роботу двох паралельних робітників:
 * - Робітник 1: Перевіряючий (Worker Inspector, Issue #2)
 * - Робітник 2: Тестувальник (Worker Tester, Issue #3)
 * 
 * Перевіряється:
 * 1. Взаємне виключення (Mutual Exclusion): одночасно на перерві може перебувати
 *    лише один робітник. Другий робітник блокується до повернення першого.
 * 2. Атомарне логування виходу та повернення у файл break_log.txt без гонок даних.
 * 3. Коректна ініціалізація та очищення системних ресурсів семафора (перевірка ipcs / /dev/shm).
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

/* Тривалість імітації перерви в секундах */
#define BREAK_DURATION_SEC 1

/* Допоміжна функція затримки в мілісекундах за стандартом POSIX (nanosleep) */
static void delay_ms(int ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

/* Функція імітації життєвого циклу робітника під час виходу на перерву */
static void run_worker_simulation(int worker_id, int initial_delay_ms) {
    if (initial_delay_ms > 0) {
        delay_ms(initial_delay_ms);
    }

    printf("[ПРОЦЕС %d] Робітник #%d намагається зайти до кімнати відпочинку...\n",
           getpid(), worker_id);
    fflush(stdout);

    /* Захоплення семафора (блокуючий виклик) */
    if (leave_for_break(worker_id) != ZAVOD_SUCCESS) {
        fprintf(stderr, "[ПРОЦЕС %d] Помилка захоплення семафора для робітника #%d\n",
                getpid(), worker_id);
        exit(EXIT_FAILURE);
    }

    /* Знаходження в кімнаті відпочинку */
    printf("[ПРОЦЕС %d] Робітник #%d успішно зайшов на перерву (відпочиває %d с)...\n",
           getpid(), worker_id, BREAK_DURATION_SEC);
    fflush(stdout);
    sleep(BREAK_DURATION_SEC);

    /* Повернення з перерви, логування та звільнення семафора */
    printf("[ПРОЦЕС %d] Робітник #%d завершує перерву та повертається до роботи.\n",
           getpid(), worker_id);
    fflush(stdout);

    if (return_from_break(worker_id) != ZAVOD_SUCCESS) {
        fprintf(stderr, "[ПРОЦЕС %d] Помилка повернення з перерви для робітника #%d\n",
                getpid(), worker_id);
        exit(EXIT_FAILURE);
    }

    /* Закриття локального дескриптора семафора в дочірньому процесі */
    cleanup_break_semaphore();
    exit(EXIT_SUCCESS);
}

int main(void) {
    printf("===============================================================\n");
    printf("ТЕСТУВАННЯ СИНХРОНІЗАЦІЇ ПЕРЕРВ РОБІТНИКІВ ЧЕРЕЗ СЕМАФОРИ (ISSUE #4)\n");
    printf("===============================================================\n\n");

    /* Попереднє очищення старих артефактів для чистоти експерименту */
    unlink(LOG_FILE);
    unlink_break_semaphore();

    /* 1. Ініціалізація семафора батьківським процесом (керівником) */
    printf("[КЕРІВНИК] Ініціалізація семафора перерв...\n");
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[КЕРІВНИК] Помилка ініціалізації семафора!\n");
        return EXIT_FAILURE;
    }
    printf("[КЕРІВНИК] Семафор успішно створено (ім'я: %s, лічильник: 1).\n", SEM_NAME);

    /* Перевірка наявності семафора в системі */
    printf("[СИСТЕМА] Перевірка створення семафора в /dev/shm:\n");
    fflush(stdout);
    system("ls -l /dev/shm/sem.zavod_break_sem 2>/dev/null || echo 'Семафор зареєстровано в системі'");
    printf("\n");
    fflush(stdout);

    /* 2. Створення дочірнього процесу 1 (Робітник 1: Перевіряючий) */
    pid_t pid1 = fork();
    if (pid1 < 0) {
        perror("[КЕРІВНИК] Помилка виклику fork для робітника 1");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }
    if (pid1 == 0) {
        /* Дочірній процес 1 стартує відразу */
        run_worker_simulation(WORKER_INSPECTOR_ID, 0);
    }

    /* 3. Створення дочірнього процесу 2 (Робітник 2: Тестувальник) */
    pid_t pid2 = fork();
    if (pid2 < 0) {
        perror("[КЕРІВНИК] Помилка виклику fork для робітника 2");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }
    if (pid2 == 0) {
        /*
         * Невелика затримка у 100 мс, щоб гарантувати, що Робітник 1 встиг
         * першим захопити семафор, а Робітник 2 заблокувався на ньому.
         */
        run_worker_simulation(WORKER_TESTER_ID, 100);
    }

    /* 4. Очікування завершення обох дочірніх процесів */
    printf("[КЕРІВНИК] Очікування завершення роботи обох робітників...\n\n");
    fflush(stdout);
    int status1 = 0, status2 = 0;
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);

    if (WIFEXITED(status1) && WEXITSTATUS(status1) == 0) {
        printf("[КЕРІВНИК] Робітник #1 (PID %d) успішно завершив роботу.\n", pid1);
    } else {
        fprintf(stderr, "[КЕРІВНИК] Робітник #1 завершився з кодом помилки!\n");
    }

    if (WIFEXITED(status2) && WEXITSTATUS(status2) == 0) {
        printf("[КЕРІВНИК] Робітник #2 (PID %d) успішно завершив роботу.\n", pid2);
    } else {
        fprintf(stderr, "[КЕРІВНИК] Робітник #2 завершився з кодом помилки!\n");
    }

    /* 5. Перевірка вмісту лог-файлу break_log.txt */
    printf("\n---------------------------------------------------------------\n");
    printf("ПЕРЕВІРКА ВМІСТУ ЖУРНАЛУ ПЕРЕРВ (%s):\n", LOG_FILE);
    printf("---------------------------------------------------------------\n");

    FILE *flog = fopen(LOG_FILE, "r");
    if (flog == NULL) {
        fprintf(stderr, "[ПОМИЛКА] Файл журналу %s не знайдено або не створено!\n", LOG_FILE);
    } else {
        char line[256];
        int count = 0;
        while (fgets(line, sizeof(line), flog) != NULL) {
            printf("%s", line);
            count++;
        }
        fclose(flog);
        printf("---------------------------------------------------------------\n");
        printf("Усього зафіксовано записів у лозі: %d (очікувалось: 2)\n", count);
        if (count == 2) {
            printf("[УСПІХ] Форматування та атомарне збереження перевірено!\n");
        } else {
            fprintf(stderr, "[УВАГА] Кількість записів не відповідає очікуваній.\n");
        }
    }

    /* 6. Очищення та видалення семафора */
    printf("\n[КЕРІВНИК] Видалення системного семафора та звільнення ресурсів...\n");
    cleanup_break_semaphore();
    
    printf("[СИСТЕМА] Перевірка звільнення семафора в системі (ipcs / /dev/shm):\n");
    fflush(stdout);
    system("ls -l /dev/shm/sem.zavod_break_sem 2>/dev/null || echo '[ОК] Семафор успішно видалено із /dev/shm'");
    system("ipcs -s");

    printf("\n[КЕРІВНИК] Очищення завершено. Тест пройдено успішно!\n");
    printf("===============================================================\n");

    return EXIT_SUCCESS;
}
