/*
 * Заглушка/шаблон для процесу Робітника 2: Тестувальник (Issue #3).
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
    printf("[РОБІТНИК 2 - ТЕСТУВАЛЬНИК] Процес запущено (PID: %d).\n", getpid());

    /* Підключення до існуючого семафора кімнати відпочинку */
    if (init_break_semaphore() != ZAVOD_SUCCESS) {
        fprintf(stderr, "[РОБІТНИК 2] Не вдалося підключитися до семафора перерв.\n");
        return EXIT_FAILURE;
    }

    printf("[РОБІТНИК 2] Очікування та тестування якості деталі...\n");
    sleep(1);

    /* Імітація виходу на перерву */
    printf("[РОБІТНИК 2] Запит на вихід на перерву...\n");
    if (leave_for_break(WORKER_TESTER_ID) == ZAVOD_SUCCESS) {
        printf("[РОБІТНИК 2] Знаходиться на перерві (відпочинок)...\n");
        sleep(1);
        return_from_break(WORKER_TESTER_ID);
    }

    /* Звільнення локальних ресурсів */
    cleanup_break_semaphore();
    printf("[РОБІТНИК 2] Роботу завершено.\n");
    return EXIT_SUCCESS;
}
