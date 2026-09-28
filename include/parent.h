/**
 * @file parent.h
 * @brief Інтерфейс функцій керівника зміни (батьківського процесу, Issue #1).
 * @details Описує прототипи функцій оркестрації: валідація аргументів,
 *          налаштування сигналів, запуск робітників, робота з unnamed pipe
 *          та чергою повідомлень IPC.
 * 
 * Усі коментарі в коді виконано українською мовою.
 */

#ifndef PARENT_H
#define PARENT_H

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include "common.h"

/**
 * @brief Перевіряє та зчитує кількість виробів із аргументів командного рядка.
 * @param argc Кількість аргументів програми.
 * @param argv Масив аргументів командного рядка.
 * @param out_count Вказівник для збереження розпарсеного числа N (N > 0).
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_INVALID_ARG при помилці.
 */
ZavodErrorCode parse_arguments(int argc, char *argv[], int *out_count);

/**
 * @brief Налаштовує обробники сигналів готовності робітників та блокування перед fork.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_SIGNAL при помилці.
 */
ZavodErrorCode setup_signal_handlers(void);

#endif /* PARENT_H */
