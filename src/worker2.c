/**
 * @file worker2.c
 * @brief Реалізація другого дочірнього процесу: "Тестувальник" (Issue #3).
 * @details Виконує читання деталей з FIFO, фільтрацію браку, виставлення балів
 *          якості стандартним деталям (1-10), передачу метрик у чергу System V
 *          та регламентований відпочинок за допомогою POSIX-семафора.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "common.h"
#include "semaphore_utils.h"

/* Періодичність виходу на перерву (кожні 3 перевірені стандартні деталі) */
#define BREAK_INTERVAL 3

/**
 * @brief Допоміжна функція виходу на регламентовану перерву
 */
static void handle_worker_break(void) {
    printf("[РОБІТНИК 2] Спроба виходу на перерву...\n");
    fflush(stdout);

    if (leave_for_break(WORKER_TESTER_ID) == ZAVOD_SUCCESS) {
        printf("[РОБІТНИК 2] Перебуває в кімнаті відпочинку (перерва)...\n");
        fflush(stdout);
        
        /* Коротка симуляція відпочинку (250 мс) через nanosleep */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 250000000L };
        nanosleep(&ts, NULL);

        if (return_from_break(WORKER_TESTER_ID) != ZAVOD_SUCCESS) {
            fprintf(stderr, "[РОБІТНИК 2 - ПОМИЛКА] Не вдалося коректно повернутися з перерви!\n");
        }
    } else {
        fprintf(stderr, "[РОБІТНИК 2 - ПОМИЛКА] Помилка захоплення семафора перерви.\n");
    }
}

int main(void) {
    printf("[РОБІТНИК 2] Старт процесу тестувальника (PID: %d, PPID: %d).\n", getpid(), getppid());
    fflush(stdout);

    /* 1. Ініціалізація псевдовипадкового генератора чисел */
    srand((unsigned int)(time(NULL) ^ getpid()));

    /* 2. Підключення до іменованого POSIX-семафора перерв (Issue #4) */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[РОБІТНИК 2 - ПОМИЛКА] Не вдалося ініціалізувати семафор перерв.\n");
        return EXIT_FAILURE;
    }

    /* 3. Підключення до черги повідомлень System V через спільний ftok-ключ */
    key_t msg_key = ftok(FTOK_PATH, FTOK_PROJ_ID_MSG);
    if (msg_key == (key_t)-1) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Збій виклику ftok для черги повідомлень");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    int msqid = msgget(msg_key, 0666);
    if (msqid == -1) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Збій msgget (черга ще не створена батьківським процесом)");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    /* 4. Сигналізація батьківському процесу про готовність до роботи (SIGUSR2) */
    if (kill(getppid(), SIG_WORKER2_READY) == -1) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Збій надсилання сигналу SIG_WORKER2_READY (SIGUSR2)");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }
    printf("[РОБІТНИК 2] Сигнал готовності SIGUSR2 успішно надіслано керівнику зміни.\n");
    fflush(stdout);

    /* 5. Відкриття іменованого каналу (FIFO) на читання */
    printf("[РОБІТНИК 2] Очікування підключення до FIFO (%s)...\n", FIFO_PATH);
    fflush(stdout);

    /* Забезпечуємо існування FIFO перед відкриттям, щоб уникнути ENOENT */
    if (mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Помилка створення FIFO mkfifo()");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    int fifo_fd = open(FIFO_PATH, O_RDONLY);
    if (fifo_fd == -1) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Не вдалося відкрити FIFO на читання");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }
    printf("[РОБІТНИК 2] Канал FIFO успішно відкрито. Початок прийому деталей...\n");
    fflush(stdout);

    IntermediateItem item;
    ssize_t bytes_read = 0;
    int total_items_received = 0;
    int tested_standard_count = 0;
    size_t payload_size = sizeof(struct msg_buffer) - sizeof(long);

    /* 6. Основний цикл читання виробів з каналу FIFO до виявлення EOF */
    while ((bytes_read = read(fifo_fd, &item, sizeof(IntermediateItem))) > 0) {
        if (bytes_read != (ssize_t)sizeof(IntermediateItem)) {
            fprintf(stderr, "[РОБІТНИК 2 - УВАГА] Зчитано неповний кадр структури IntermediateItem.\n");
            continue;
        }

        total_items_received++;

        /* Вивід отриманих даних на екран згідно з умовами завдання */
        printf("[РОБІТНИК 2 - КОНВЕЄР] Деталь №%u: статус -> %s\n",
               item.serial_number,
               (item.status == STATUS_STANDARD ? "СТАНДАРТ" : "БРАК"));
        fflush(stdout);

        if (item.status == STATUS_DEFECT) {
            /* Відбраковані деталі не тестуються */
            printf("[РОБІТНИК 2] Деталь №%u відбракована першим робітником. Тестування не проводиться.\n",
                   item.serial_number);
            fflush(stdout);
        } else {
            /* Стандартні деталі: проводимо фінальне тестування (бал якості 1–10 згідно з завданням) */
            int score_10 = (rand() % 10) + 1;
            uint8_t final_metric_score = (uint8_t)score_10;

            printf("[РОБІТНИК 2] Фінальний тест деталі №%u виконано. Бал якості: %d/10.\n",
                   item.serial_number, score_10);
            fflush(stdout);

            /* Формування та відправка повідомлення в чергу System V */
            struct msg_buffer msg;
            memset(&msg, 0, sizeof(msg));
            msg.msg_type = MSG_TYPE_METRIC;
            msg.metric.serial_number = item.serial_number;
            msg.metric.quality_score = final_metric_score;
            snprintf(msg.msg_text, sizeof(msg.msg_text), "Бал якості: %d/10", score_10);

            /* Блокуюча відправка повідомлення */
            while (msgsnd(msqid, &msg, payload_size, 0) == -1) {
                if (errno == EINTR) {
                    continue; /* Переривання сигналом ОС — повторюємо запит */
                }
                perror("[РОБІТНИК 2 - ПОМИЛКА] Збій msgsnd при відправці метрики");
                break;
            }

            tested_standard_count++;

            /* Перевірка регламенту виходу на перерву */
            if (tested_standard_count % BREAK_INTERVAL == 0) {
                handle_worker_break();
            }
        }
    }

    if (bytes_read == -1) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Помилка системного виклику read()");
    }

    /* 7. Надсилання повідомлення-маркера завершення для батьківського процесу */
    struct msg_buffer stop_msg;
    memset(&stop_msg, 0, sizeof(stop_msg));
    stop_msg.msg_type = MSG_TYPE_STOP;
    stop_msg.metric.serial_number = 0;
    stop_msg.metric.quality_score = 0;
    strncpy(stop_msg.msg_text, "STOP", sizeof(stop_msg.msg_text) - 1);

    if (msgsnd(msqid, &stop_msg, payload_size, 0) == -1) {
        perror("[РОБІТНИК 2 - ПОМИЛКА] Не вдалося надіслати маркер MSG_TYPE_STOP");
    }

    /* 8. Закриття дескрипторів та звільнення ресурсів */
    close(fifo_fd);
    cleanup_break_semaphore();

    printf("[РОБІТНИК 2] Обробку каналу FIFO завершено. Разом отримано: %d (перевірено стандартних: %d).\n",
           total_items_received, tested_standard_count);
    fflush(stdout);

    return EXIT_SUCCESS;
}