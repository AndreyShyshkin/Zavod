/**
 * @file main.c
 * @brief Головний процес керівника заводу (Factory Supervisor, Issue #1).
 * @details Виконує ініціалізацію інфраструктури, запуск лінії контролю якості
 *          та коректне звільнення ресурсів при завершенні зміни.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "semaphore_utils.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Використання: %s <items_count>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Керівник заводу розпочав роботу. Заплановано деталей: %s\n", argv[1]);

    /* Ініціалізація іменованого семафора кімнати відпочинку */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[ПОМИЛКА] Не вдалося ініціалізувати семафор перерв.\n");
        return EXIT_FAILURE;
    }
    printf("[КЕРІВНИК] Семафор кімнати відпочинку успішно налаштовано (%s).\n", SEM_NAME);

    /* Фінальне очищення ресурсів семафора */
    cleanup_break_semaphore();
    printf("[КЕРІВНИК] Роботу зміни завершено. Ресурси звільнено.\n");

    return EXIT_SUCCESS;
}