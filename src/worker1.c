/**
 * @file worker1.c
 * @brief Дочірній процес №1 "Перевіряючий" (Quality Inspector, Issue #2).
 * @details Отримує серійні номери від керівника через неіменований канал (unnamed pipe),
 *          виводить кожен номер на екран, симулює перевірку виробу на брак з імовірністю 15%,
 *          передає результат другому дочірньому процесу через іменований канал (FIFO)
 *          у вигляді структури IntermediateItem та періодично виходить на перерву,
 *          використовуючи POSIX-семафор кімнати відпочинку.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "common.h"
#include "semaphore_utils.h"

/* Рядкові константи для виводу результату перевірки */
#define STATUS_STR_STANDARD "стандарт"
#define STATUS_STR_DEFECT   "брак"

/* Параметри симуляції відпочинку */
#define BREAK_CHANCE_PERCENT 20   /* Ймовірність запиту на перерву після перевірки деталі (%) */
#define BREAK_DURATION_MS    150  /* Тривалість перебування в кімнаті відпочинку (мс) */

/**
 * @brief Допоміжна функція мілісекундної затримки на базі nanosleep.
 * @param ms Кількість мілісекунд для затримки.
 */
static void delay_ms(int ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

/**
 * @brief Безпечне створення іменованого каналу (FIFO) без стану гонки.
 * @param path Шлях до FIFO у системі.
 * @return ZAVOD_SUCCESS при успіху (або якщо вже створено), інакше ZAVOD_ERR_FIFO.
 */
static ZavodErrorCode ensure_fifo_exists(const char *path) {
    if (path == NULL) {
        return ZAVOD_ERR_INVALID_ARG;
    }

    if (mkfifo(path, 0666) == -1) {
        /* Якщо канал вже був створений іншим процесом — це нормальна ситуація */
        if (errno != EEXIST) {
            perror("[РОБІТНИК 1 - ПОМИЛКА] Помилка створення каналу mkfifo()");
            return ZAVOD_ERR_FIFO;
        }
    }

    return ZAVOD_SUCCESS;
}

/**
 * @brief Перевірка деталі на наявність дефекту (браку).
 * @param probability Відсоток ймовірності браку (наприклад, 15).
 * @return true, якщо деталь виявилася бракованою, інакше false.
 */
static bool check_if_defective(int probability) {
    return (rand() % 100) < probability;
}

/**
 * @brief Симуляція виходу робітника на перерву з використанням семафора.
 * @param worker_id Ідентифікатор поточного робітника.
 */
static void handle_optional_break(int worker_id, uint32_t processed_count) {
    /* Виходимо на перерву за ймовірністю або періодично */
    if ((rand() % 100) < BREAK_CHANCE_PERCENT || (processed_count % 4 == 0)) {
        printf("[РОБІТНИК 1] Запит на перерву (спроба зайняти кімнату відпочинку)...\n");
        fflush(stdout);

        /* Захоплення семафора кімнати відпочинку (блокуючий виклик) */
        if (leave_for_break(worker_id) == ZAVOD_SUCCESS) {
            delay_ms(BREAK_DURATION_MS);

            /* Звільнення семафора та атомарний запис у лог */
            return_from_break(worker_id);
        } else {
            fprintf(stderr, "[РОБІТНИК 1 - ПОМИЛКА] Не вдалося коректно здійснити вихід на перерву.\n");
        }
    }
}

int main(int argc, char *argv[]) {
    printf("[РОБІТНИК 1 - ПЕРЕВІРЯЮЧИЙ] Процес успішно стартував (PID: %d, PPID: %d).\n",
           getpid(), getppid());
    fflush(stdout);

    /* Ініціалізація генератора випадкових чисел зерном з урахуванням PID та поточного часу */
    srand((unsigned int)(time(NULL) ^ getpid()));

    /*
     * 1. СИГНАЛІЗАЦІЯ ГОТОВНОСТІ
     * Негайно надсилаємо сигнал SIGUSR1 батьківському процесу.
     */
    pid_t parent_pid = getppid();
    if (kill(parent_pid, SIG_WORKER1_READY) == -1) {
        perror("[РОБІТНИК 1 - ПОМИЛКА] Не вдалося надіслати SIGUSR1 керівнику");
        return EXIT_FAILURE;
    }
    printf("[РОБІТНИК 1] Сигнал готовності SIGUSR1 надіслано керівнику (PID %d).\n", parent_pid);
    fflush(stdout);

    /*
     * 2. ПІДКЛЮЧЕННЯ ДО СПІЛЬНОГО СЕМАФОРА КІМНАТИ ВІДПОЧИНКУ (Issue #4)
     */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[РОБІТНИК 1 - ПОМИЛКА] Не вдалося підключитися до семафора перерв.\n");
        return EXIT_FAILURE;
    }

    /*
     * 3. НАЛАШТУВАННЯ ЧИТАННЯ З UNNAMED PIPE
     * Батько передає дубльований STDIN_FILENO, або номер дескриптора в argv[1].
     */
    int read_pipe_fd = STDIN_FILENO;
    if (argc > 1 && argv[1] != NULL) {
        int passed_fd = atoi(argv[1]);
        if (passed_fd > 0 && fcntl(passed_fd, F_GETFD) != -1) {
            read_pipe_fd = passed_fd;
        }
    }

    /*
     * 4. СТВОРЕННЯ ТА ВІДКРИТТЯ ІМЕНОВАНОГО КАНАЛУ (FIFO) НА ЗАПИС
     */
    if (ensure_fifo_exists(FIFO_PATH) != ZAVOD_SUCCESS) {
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    printf("[РОБІТНИК 1] Відкриття FIFO (%s) на запис (блокується до підключення читача)...\n", FIFO_PATH);
    fflush(stdout);

    /* open() заблокується, доки процес №2 не відкриє свій кінець FIFO на читання */
    int fifo_fd = open(FIFO_PATH, O_WRONLY);
    if (fifo_fd == -1) {
        perror("[РОБІТНИК 1 - ПОМИЛКА] Не вдалося відкрити FIFO на запис");
        cleanup_break_semaphore();
        return EXIT_FAILURE;
    }

    printf("[РОБІТНИК 1] Канал FIFO підключено. Очікування виробів від керівника...\n");
    fflush(stdout);

    /*
     * 5. ОСНОВНИЙ ЦИКЛ ЧИТАННЯ, ПЕРЕВІРКИ ТА ПЕРЕДАЧІ
     */
    PipeItem item;
    uint32_t processed_count = 0;
    uint32_t defects_count = 0;
    ssize_t bytes_read = 0;

    /* Читання триває до моменту закриття pipe батьківським процесом (EOF) */
    while ((bytes_read = read(read_pipe_fd, &item, sizeof(PipeItem))) > 0) {
        if (bytes_read != (ssize_t)sizeof(PipeItem)) {
            fprintf(stderr, "[РОБІТНИК 1 - ПОМИЛКА] Отримано неповний пакет (%zd байтів).\n", bytes_read);
            continue;
        }

        processed_count++;
        uint32_t serial = item.serial_number;

        /* Вивід отриманого номера деталі на екран одразу після отримання */
        printf("[РОБІТНИК 1 - ВХІД] Отримано виріб №%u (серійний номер: %u)\n", item.id, serial);
        fflush(stdout);

        /* Визначення браку з імовірністю 15% */
        bool is_defect = check_if_defective(DEFAULT_DEFECT_PROBABILITY);
        const char *status_str = is_defect ? STATUS_STR_DEFECT : STATUS_STR_STANDARD;

        if (is_defect) {
            defects_count++;
        }

        /* Передача деталі у FIFO за узгодженим протоколом структури IntermediateItem */
        IntermediateItem out_item;
        out_item.serial_number = serial;
        out_item.status = is_defect ? STATUS_DEFECT : STATUS_STANDARD;

        ssize_t written = write(fifo_fd, &out_item, sizeof(IntermediateItem));
        if (written != (ssize_t)sizeof(IntermediateItem)) {
            perror("[РОБІТНИК 1 - ПОМИЛКА] Помилка запису результату у FIFO");
            break;
        }

        printf("[РОБІТНИК 1 - ВИХІД] Виріб %u перевірено -> [%s], надіслано в FIFO.\n", serial, status_str);
        fflush(stdout);

        /* Симуляція періодичного відпочинку за семафором */
        handle_optional_break(WORKER_INSPECTOR_ID, processed_count);
    }

    if (bytes_read == -1) {
        perror("[РОБІТНИК 1 - ПОМИЛКА] Помилка читання з unnamed pipe");
    }

    /*
     * 6. ЗАВЕРШЕННЯ РОБОТИ ТА ЗАКРИТТЯ КАНАЛІВ
     */
    printf("\n[РОБІТНИК 1] Керівник закрив pipe (отримано EOF).\n");
    printf("[РОБІТНИК 1] Підсумок зміни: оброблено %u деталей, відсіяно браку %u (%.1f%%).\n",
           processed_count, defects_count,
           processed_count > 0 ? ((double)defects_count * 100.0 / processed_count) : 0.0);
    fflush(stdout);

    /* Закриваємо FIFO: читач на тому кінці отримає EOF */
    if (close(fifo_fd) == -1) {
        perror("[РОБІТНИК 1 - ПОМИЛКА] Помилка закриття FIFO");
    }

    if (read_pipe_fd != STDIN_FILENO) {
        close(read_pipe_fd);
    }

    /* Звільняємо локальний дескриптор семафора */
    cleanup_break_semaphore();

    printf("[РОБІТНИК 1] Роботу успішно завершено. Вихід.\n");
    return EXIT_SUCCESS;
}
