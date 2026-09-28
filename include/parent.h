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

/**
 * @brief Очікує надходження сигналів готовності від обох робітників.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_SIGNAL при помилці.
 */
ZavodErrorCode wait_for_workers_ready(void);

/**
 * @brief Ініціалізує генератор випадкових чисел керівника зерном (time ^ pid).
 */
void init_random_generator(void);

/**
 * @brief Генерує унікальний псевдовипадковий серійний номер виробу.
 * @param index Порядковий номер деталі на конвеєрі (1..N).
 * @return Згенерований серійний номер uint32_t.
 */
uint32_t generate_serial_number(uint32_t index);

/**
 * @brief Створює неіменований канал pipe для передачі виробів першому робітнику.
 * @param pipe_fd Масив із двох дескрипторів: [0] для читання, [1] для запису.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_PIPE при помилці.
 */
ZavodErrorCode create_pipe(int pipe_fd[2]);

/**
 * @brief Генерує та записує N деталей у неіменований канал, після чого закриває його для передачі EOF.
 * @param write_fd Дескриптор неіменованого каналу для запису.
 * @param count Кількість деталей (N) для передачі.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_PIPE або ZAVOD_ERR_INVALID_ARG при помилці.
 */
ZavodErrorCode send_items_via_pipe(int write_fd, int count);

#endif /* PARENT_H */
