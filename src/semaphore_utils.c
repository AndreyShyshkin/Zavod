/*
 * Реалізація модуля синхронізації перерв робітників через POSIX-семафори (Issue #4).
 * 
 * Модуль забезпечує:
 * 1. Гарантію того, що в кімнаті відпочинку одночасно перебуває не більше одного робітника.
 * 2. Атомарне логування виходів та повернень у файл break_log.txt через прапорець O_APPEND.
 * 3. Безпечне створення, закриття та видалення семафора на рівні ядра ОС.
 * 4. Розширену діагностику системних помилок через errno.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "semaphore_utils.h"
#include "common.h"

/* Вказівник на іменований POSIX-семафор */
static sem_t *g_break_sem = SEM_FAILED;

/* PID процесу, який створив семафор (батьківський процес/керівник) */
static pid_t g_creator_pid = 0;

/* Максимальна кількість підтримуваних робітників для відстеження початку перерви */
#define MAX_TRACKED_WORKERS 32

/* Масив збереження часу виходу на перерву для кожного робітника */
static time_t g_worker_start_times[MAX_TRACKED_WORKERS] = {0};

/*
 * Допоміжна функція детального виводу помилки для системних викликів семафорів
 */
static void log_sem_error(const char *operation, int err_code) {
    char err_buf[256];
    strerror_r(err_code, err_buf, sizeof(err_buf));
    fprintf(stderr, "[ПОМИЛКА СЕМАФОРА] Операція '%s' зазнала невдачі (код %d: %s)\n",
            operation, err_code, err_buf);
}

/*
 * Ініціалізація іменованого семафора перерв.
 * Відкриває або створює семафор із правами 0644 та початковим значенням 1.
 */
int init_break_semaphore(void) {
    if (g_break_sem != SEM_FAILED) {
        /* Семафор вже ініціалізовано в цьому процесі */
        return ZAVOD_SUCCESS;
    }

    /*
     * Виклик sem_open з O_CREAT створює новий семафор, якщо він відсутній,
     * або повертає дескриптор наявного семафора. Початкове значення 1 означає,
     * що рівно один процес може одночасно зайти в критичну секцію (перерву).
     */
    g_break_sem = sem_open(SEM_NAME, O_CREAT, SEM_PERMS, 1);
    if (g_break_sem == SEM_FAILED) {
        int err = errno;
        log_sem_error("sem_open", err);
        if (err == EACCES) {
            fprintf(stderr, "[ДІАГНОСТИКА] Відмовлено в доступі до семафора %s.\n", SEM_NAME);
        } else if (err == ENFILE || err == EMFILE) {
            fprintf(stderr, "[ДІАГНОСТИКА] Перевищено ліміт відкритих дескрипторів у системі або процесі.\n");
        } else if (err == ENOSPC) {
            fprintf(stderr, "[ДІАГНОСТИКА] Недостатньо пам'яті для розміщення нового семафора в системі.\n");
        }
        return ZAVOD_ERR_SEM;
    }

    /* Фіксуємо PID ініціатора для подальшого безпечного очищення */
    if (g_creator_pid == 0) {
        g_creator_pid = getpid();
    }

    return ZAVOD_SUCCESS;
}

/*
 * Захоплення семафора (вихід робітника на перерву).
 */
int leave_for_break(int worker_id) {
    /* Валідація ідентифікатора робітника */
    if (worker_id <= 0 || worker_id >= MAX_TRACKED_WORKERS) {
        fprintf(stderr, "[ПОМИЛКА ВАЛІДАЦІЇ] Некоректний ідентифікатор робітника: %d (допустимо: 1..%d).\n",
                worker_id, MAX_TRACKED_WORKERS - 1);
        return ZAVOD_ERR_INVALID_ARG;
    }

    if (g_break_sem == SEM_FAILED) {
        if (init_break_semaphore() != ZAVOD_SUCCESS) {
            return ZAVOD_ERR_SEM;
        }
    }

    /*
     * Блокуюче очікування звільнення семафора.
     * Обробляємо випадок переривання системного виклику сигналом (EINTR).
     */
    while (sem_wait(g_break_sem) == -1) {
        int err = errno;
        if (err == EINTR) {
            /* Системний виклик було перервано сигналом — повторюємо спробу */
            continue;
        }
        log_sem_error("sem_wait", err);
        if (err == EINVAL) {
            fprintf(stderr, "[ДІАГНОСТИКА] Семафор не є валідним або вказує на пошкоджену пам'ять.\n");
        } else if (err == EDEADLK) {
            fprintf(stderr, "[ДІАГНОСТИКА] Виявлено взаємне блокування (дедлок) при спробі захоплення семафора.\n");
        }
        return ZAVOD_ERR_SEM;
    }

    /* Фіксуємо поточний системний час початку перерви */
    time_t start_time = time(NULL);
    g_worker_start_times[worker_id] = start_time;

    /* Вивід статусу в термінал для наочності */
    printf("[СЕМАФОР] Робітник #%d пішов на перерву (кімната відпочинку зайнята).\n", worker_id);
    fflush(stdout);

    return ZAVOD_SUCCESS;
}

/*
 * Повернення робітника з перерви, атомарне логування та звільнення семафора.
 */
int return_from_break(int worker_id) {
    /* Валідація ідентифікатора робітника */
    if (worker_id <= 0 || worker_id >= MAX_TRACKED_WORKERS) {
        fprintf(stderr, "[ПОМИЛКА ВАЛІДАЦІЇ] Некоректний ідентифікатор робітника: %d (допустимо: 1..%d).\n",
                worker_id, MAX_TRACKED_WORKERS - 1);
        return ZAVOD_ERR_INVALID_ARG;
    }

    if (g_break_sem == SEM_FAILED) {
        fprintf(stderr, "[ПОМИЛКА] Спроба повернутися з перерви без ініціалізованого семафора.\n");
        return ZAVOD_ERR_SEM;
    }

    /* Фіксуємо час повернення */
    time_t end_time = time(NULL);

    /* Отримуємо час початку перерви для цього робітника */
    time_t start_time = end_time;
    if (worker_id >= 0 && worker_id < MAX_TRACKED_WORKERS && g_worker_start_times[worker_id] != 0) {
        start_time = g_worker_start_times[worker_id];
        g_worker_start_times[worker_id] = 0; /* Скидаємо збережений час */
    }

    /* Форматуємо часові мітки за допомогою time(), localtime(), strftime() */
    char start_str[64];
    char end_str[64];

    struct tm *tm_start = localtime(&start_time);
    if (tm_start != NULL) {
        strftime(start_str, sizeof(start_str), "%Y-%m-%d %H:%M:%S", tm_start);
    } else {
        snprintf(start_str, sizeof(start_str), "Невідомо");
    }

    struct tm *tm_end = localtime(&end_time);
    if (tm_end != NULL) {
        strftime(end_str, sizeof(end_str), "%Y-%m-%d %H:%M:%S", tm_end);
    } else {
        snprintf(end_str, sizeof(end_str), "Невідомо");
    }

    /* Формуємо стандартизований запис для журналу перерв */
    char log_entry[256];
    int entry_len = snprintf(log_entry, sizeof(log_entry),
                             "[Робітник %d] Вихід: %s — Повернення: %s\n",
                             worker_id, start_str, end_str);

    /*
     * Безпечне відкриття файлу логу:
     * O_WRONLY | O_CREAT | O_APPEND з правами 0644.
     * Завдяки O_APPEND ядро операційної системи гарантує атомарне позиціонування
     * в кінець файлу перед кожним системним викликом write, що повністю виключає
     * гонки даних (data races) між паралельними процесами без додаткових файлових блокувань.
     */
    int log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd != -1) {
        if (entry_len > 0) {
            ssize_t written = write(log_fd, log_entry, (size_t)entry_len);
            if (written < 0) {
                int write_err = errno;
                fprintf(stderr, "[ПОМИЛКА ЗАПИСУ] Не вдалося записати в лог-файл (errno %d: %s)\n",
                        write_err, strerror(write_err));
            } else if (written < (ssize_t)entry_len) {
                fprintf(stderr, "[УВАГА] Неповний запис у лог: записано %zd з %d байтів.\n",
                        written, entry_len);
            }
        }
        if (close(log_fd) == -1) {
            perror("[ПОМИЛКА] Не вдалося коректно закрити дескриптор лог-файлу");
        }
    } else {
        int open_err = errno;
        fprintf(stderr, "[ПОМИЛКА ФАЙЛУ] Не вдалося відкрити %s (errno %d: %s)\n",
                LOG_FILE, open_err, strerror(open_err));
    }

    /*
     * Звільняємо семафор (sem_post), збільшуючи його лічильник до 1.
     * Тепер інший робітник може зайти в кімнату відпочинку.
     */
    if (sem_post(g_break_sem) == -1) {
        int post_err = errno;
        log_sem_error("sem_post", post_err);
        if (post_err == EOVERFLOW) {
            fprintf(stderr, "[ДІАГНОСТИКА] Переповнення лічильника семафора (перевищено SEM_VALUE_MAX).\n");
        }
        return ZAVOD_ERR_SEM;
    }

    printf("[СЕМАФОР] Робітник #%d повернувся з перерви (кімната відпочинку вільна).\n", worker_id);
    fflush(stdout);

    return ZAVOD_SUCCESS;
}

/*
 * Очищення дескриптора семафора поточного процесу (sem_close).
 * Якщо викликається процесом-творцем, також видаляє семафор із системи.
 */
int cleanup_break_semaphore(void) {
    int result = ZAVOD_SUCCESS;

    if (g_break_sem != SEM_FAILED) {
        if (sem_close(g_break_sem) == -1) {
            int close_err = errno;
            log_sem_error("sem_close", close_err);
            result = ZAVOD_ERR_SEM;
        }
        g_break_sem = SEM_FAILED;
    }

    /* Якщо це процес-творець (батьківський процес), видаляємо семафор із системи */
    if (g_creator_pid != 0 && getpid() == g_creator_pid) {
        if (unlink_break_semaphore() != ZAVOD_SUCCESS) {
            result = ZAVOD_ERR_SEM;
        }
    }

    return result;
}

/*
 * Видалення іменованого семафора із глобального простору імен ядра (sem_unlink).
 */
int unlink_break_semaphore(void) {
    if (sem_unlink(SEM_NAME) == -1) {
        int unlink_err = errno;
        if (unlink_err != ENOENT) {
            log_sem_error("sem_unlink", unlink_err);
            return ZAVOD_ERR_SEM;
        }
    }
    return ZAVOD_SUCCESS;
}
