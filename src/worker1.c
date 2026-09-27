/*
 * Заглушка/шаблон для процесу Робітника 1: Перевіряючий (Issue #2).
 * 
 * Демонструє інтеграцію з модулем семафорів (Issue #4) та структурами common.h.
 * Усі коментарі в коді виконано українською мовою.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include "semaphore_utils.h"

int main(void) {
    printf("[РОБІТНИК 1 - ПЕРЕВІРЯЮЧИЙ] Процес запущено (PID: %d).\n", getpid());

    /* Підключення до існуючого семафора кімнати відпочинку */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[РОБІТНИК 1] Не вдалося підключитися до семафора перерв.\n");
        return EXIT_FAILURE;
    }

    printf("[РОБІТНИК 1] Виконується первинний огляд деталі...\n");
    sleep(1);

    /* Імітація виходу на перерву */
    printf("[РОБІТНИК 1] Запит на вихід на перерву...\n");
    if (leave_for_break(WORKER_INSPECTOR_ID) == ZAVOD_SUCCESS) {
        printf("[РОБІТНИК 1] Знаходиться на перерві (відпочинок)...\n");
        sleep(1);
        return_from_break(WORKER_INSPECTOR_ID);
    }

    /* Звільнення локальних ресурсів */
    cleanup_break_semaphore();
    printf("[РОБІТНИК 1] Роботу завершено.\n");
    return EXIT_SUCCESS;
}
