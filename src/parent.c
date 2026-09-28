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

#include "parent.h"
#include "common.h"

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
