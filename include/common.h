#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define FIFO_PATH "/tmp/factory_qc_fifo"
#define MQ_NAME   "/factory_metrics_mq"
#define SEM_NAME  "/factory_break_room"
#define LOG_FILE  "breaks.log"

typedef enum {
    STATUS_DEFECT = 0,
    STATUS_STANDARD = 1
} ItemStatus;

typedef struct {
    uint32_t serial_number;
    ItemStatus status;
} IntermediateItem;

typedef struct {
    uint32_t serial_number;
    uint8_t quality_score;
} FinalMetric;

#endif /* COMMON_H */

