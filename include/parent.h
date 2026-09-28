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

/**
 * @brief Створює або підключається до черги повідомлень System V через ftok.
 * @return Дескриптор черги (msqid >= 0) у разі успіху або -1 при помилці.
 */
int init_message_queue(void);

/**
 * @brief Породжує два дочірні процеси (Робітник 1 та Робітник 2) через fork та налаштовує pipe.
 * @param pipe_fd Масив дескрипторів неіменованого каналу.
 * @param out_pid1 Вказівник для збереження PID першого дочірнього процесу.
 * @param out_pid2 Вказівник для збереження PID другого дочірнього процесу.
 * @return ZAVOD_SUCCESS у разі успіху, ZAVOD_ERR_FORK при помилці.
 */
ZavodErrorCode launch_workers(int pipe_fd[2], pid_t *out_pid1, pid_t *out_pid2);

/**
 * @brief Читає в циклі повідомлення з черги IPC, виводить результати та рахує статистику.
 * @param msqid Дескриптор черги повідомлень System V.
 * @param pid2 PID процесу Робітника 2 (для моніторингу його завершення).
 * @param total_count Загальна запланована кількість виробів (N).
 * @param out_passed Вказівник для збереження кількості протестованих/пройдених деталей.
 * @param out_defects Вказівник для збереження кількості відсіяного браку.
 * @return ZAVOD_SUCCESS у разі успіху, відповідний код помилки при збої.
 */
ZavodErrorCode read_results_from_queue(int msqid, pid_t pid2, int total_count, int *out_passed, int *out_defects);

/**
 * @brief Очікує завершення обох дочірніх процесів та виводить фінальну статистику зміни.
 * @param pid1 PID процесу Робітника 1.
 * @param pid2 PID процесу Робітника 2.
 * @param total_count Загальна кількість виробів (N).
 * @param passed_count Кількість виробів, що успішно пройшли повний контроль.
 * @return ZAVOD_SUCCESS у разі успіху.
 */
ZavodErrorCode wait_and_print_summary(pid_t pid1, pid_t pid2, int total_count, int passed_count);

/**
 * @brief Видаляє чергу повідомлень та очищує IPC-ресурси керівника.
 * @param msqid Дескриптор черги повідомлень System V для видалення.
 */
void cleanup_ipc_resources(int msqid);

/**
 * @brief Головний керуючий цикл оркестрації керівника заводу.
 * @param argc Кількість аргументів програми.
 * @param argv Масив аргументів командного рядка.
 * @return EXIT_SUCCESS або EXIT_FAILURE.
 */
int run_supervisor(int argc, char *argv[]);

#endif /* PARENT_H */
