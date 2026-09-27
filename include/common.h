/**
 * @file common.h
 * @brief Спільна міжпроцесна інфраструктура та структури даних проєкту "Завод".
 * @details Містить визначення констант для каналів FIFO, черг повідомлень (MQ),
 *          іменованих POSIX-семафорів, форматів повідомлень та кодів помилок.
 * 
 * Усі коментарі та документація виконані українською мовою згідно зі стандартами проєкту.
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================= */
/* КОНСТАНТИ МІЖПРОЦЕСНОЇ ВЗАЄМОДІЇ (IPC)                                   */
/* ========================================================================= */

/**
 * @def FIFO_PATH
 * @brief Шлях до іменованого каналу (FIFO) для передачі деталей між робітниками.
 */
#define FIFO_PATH           "/tmp/zavod_fifo"

/**
 * @def FACTORY_QC_FIFO
 * @brief Аліас для збереження зворотної сумісності з попередніми модулями.
 */
#define FACTORY_QC_FIFO     FIFO_PATH

/**
 * @def MQ_NAME
 * @brief Назва POSIX черги повідомлень для передачі метрик керівнику.
 */
#define MQ_NAME             "/zavod_metrics_mq"

/**
 * @def FACTORY_METRICS_MQ
 * @brief Аліас назви черги повідомлень для сумісності з початковим кодом.
 */
#define FACTORY_METRICS_MQ  MQ_NAME

/**
 * @def FTOK_PATH
 * @brief Базовий шлях для системного генератора ключів ftok.
 */
#define FTOK_PATH           "/tmp"

/**
 * @def FTOK_PROJ_ID_MSG
 * @brief Символьний ідентифікатор проекту для генерації ключа черги повідомлень.
 */
#define FTOK_PROJ_ID_MSG    'M'

/**
 * @def FTOK_PROJ_ID_SEM
 * @brief Символьний ідентифікатор проекту для генерації ключа семафорів.
 */
#define FTOK_PROJ_ID_SEM    'S'

/**
 * @def SEM_NAME
 * @brief Унікальне ім'я іменованого POSIX-семафора кімнати відпочинку робітників.
 */
#define SEM_NAME            "/zavod_break_sem"

/**
 * @def FACTORY_BREAK_ROOM
 * @brief Аліас імені семафора кімнати відпочинку для зворотної сумісності.
 */
#define FACTORY_BREAK_ROOM  SEM_NAME

/**
 * @def SEM_PERMS
 * @brief Права доступу до семафора у вісімковому форматі (читання/запис для власника та групи).
 */
#define SEM_PERMS           0644

/**
 * @def LOG_FILE
 * @brief Назва файлу журналу виходів та повернень із перерви.
 */
#define LOG_FILE            "break_log.txt"

/**
 * @def BREAKS_LOG
 * @brief Аліас назви файлу журналу для зворотної сумісності.
 */
#define BREAKS_LOG          LOG_FILE

/* ========================================================================= */
/* ІДЕНТИФІКАТОРИ РОБІТНИКІВ ТА ПАРАМЕТРИ СИСТЕМИ                          */
/* ========================================================================= */

/**
 * @def SUPERVISOR_ID
 * @brief Ідентифікатор процесу-керівника (батьківський процес, Issue #1).
 */
#define SUPERVISOR_ID       0

/**
 * @def WORKER_INSPECTOR_ID
 * @brief Ідентифікатор робітника-перевіряючого (дочірній процес 1, Issue #2).
 */
#define WORKER_INSPECTOR_ID 1

/**
 * @def WORKER_TESTER_ID
 * @brief Ідентифікатор робітника-тестувальника (дочірній процес 2, Issue #3).
 */
#define WORKER_TESTER_ID    2

/**
 * @def QUALITY_SCORE_DEFECT_THRESHOLD
 * @brief Мінімальний бал якості, нижче якого деталь вважається бракованою.
 */
#define QUALITY_SCORE_DEFECT_THRESHOLD 50

/**
 * @def DEFAULT_DEFECT_PROBABILITY
 * @brief Базова ймовірність браку у відсотках для генерації виробів (15%).
 */
#define DEFAULT_DEFECT_PROBABILITY     15

/**
 * @enum ZavodErrorCode
 * @brief Стандартизовані числові коди результатів та помилок системи "Завод".
 */
typedef enum {
    ZAVOD_SUCCESS          =  0,  /**< Успішне завершення операції */
    ZAVOD_ERR_FIFO         = -1,  /**< Помилка створення, відкриття або обміну через FIFO */
    ZAVOD_ERR_SEM          = -2,  /**< Помилка ініціалізації, блокування або очищення семафора */
    ZAVOD_ERR_MQ           = -3,  /**< Помилка надсилання чи отримання повідомлення з черги */
    ZAVOD_ERR_FORK         = -4,  /**< Помилка створення нового процесу викликом fork */
    ZAVOD_ERR_FILE         = -5,  /**< Помилка файлової системи при роботі з лог-файлом */
    ZAVOD_ERR_INVALID_ARG  = -6   /**< Передано некоректний вхідний аргумент або параметр */
} ZavodErrorCode;

/* ========================================================================= */
/* СТРУКТУРИ ДАНИХ                                                           */
/* ========================================================================= */

/**
 * @enum ItemStatus
 * @brief Статус виробу після первинного візуального огляду.
 */
typedef enum {
    STATUS_DEFECT   = 0,  /**< Брак — виріб пошкоджений або дефектний */
    STATUS_STANDARD = 1   /**< Стандарт — виріб відповідає нормам первинного огляду */
} ItemStatus;

/**
 * @struct IntermediateItem
 * @brief Структура проміжного виробу, що передається через FIFO від перевіряючого.
 */
typedef struct {
    uint32_t serial_number;  /**< Унікальний порядковий номер деталі на конвеєрі */
    ItemStatus status;       /**< Результат первинного огляду */
} IntermediateItem;

/**
 * @struct FinalMetric
 * @brief Фінальна оцінка якості деталі після інструментального тестування.
 */
typedef struct {
    uint32_t serial_number;  /**< Унікальний серійний номер виробу */
    uint8_t quality_score;   /**< Кількісна оцінка якості (0..100) */
} FinalMetric;

/**
 * @def MSG_BUFFER_TEXT_LEN
 * @brief Максимальна довжина текстового супроводу в черзі повідомлень.
 */
#define MSG_BUFFER_TEXT_LEN 256

/**
 * @struct msg_buffer
 * @brief Уніфікована структура повідомлення для черг IPC.
 */
struct msg_buffer {
    long msg_type;                       /**< Тип повідомлення (наприклад, 1 для метрик, 2 для сигналу завершення) */
    char msg_text[MSG_BUFFER_TEXT_LEN];  /**< Текстовий опис стану виробу або системне повідомлення */
    FinalMetric metric;                  /**< Корисне навантаження — числова метрика якості */
};

#endif /* COMMON_H */
