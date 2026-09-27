# =============================================================================
# Makefile для проєкту "Завод" (Line Quality Control System)
# Модуль: Спільна інфраструктура та синхронізація через семафори (Issue #4)
# =============================================================================

# Компілятор та прапорці компіляції (стандарт C11, суворі попередження)
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS = -lrt -pthread

# Назви виконуваних файлів
TARGET = factory
PARENT_BIN = parent
WORKER1_BIN = worker1
WORKER2_BIN = worker2
TEST_BIN = test_semaphores

# Директорії проєкту
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
TEST_DIR = tests

# Назва Docker-образу для ізольованого складання
IMAGE_NAME = zavod-build-env

# Спільні об'єктні файли
COMMON_OBJS = $(OBJ_DIR)/semaphore_utils.o

# Основна ціль за замовчуванням
all: $(OBJ_DIR) $(TARGET)

# Створення каталогу для об'єктних файлів
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Компіляція спільних об'єктних модулів
$(OBJ_DIR)/semaphore_utils.o: $(SRC_DIR)/semaphore_utils.c $(INC_DIR)/semaphore_utils.h $(INC_DIR)/common.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c $(INC_DIR)/common.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Збирання головної програми фабрики
$(TARGET): $(OBJ_DIR)/main.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Ціль для батьківського процесу (керівник)
parent: $(TARGET)

# Ціль для компіляції робітника 1 (перевіряючий, Issue #2)
worker1: $(COMMON_OBJS)
	@if [ -f $(SRC_DIR)/worker1.c ]; then \
		$(CC) $(CFLAGS) $(SRC_DIR)/worker1.c $(COMMON_OBJS) -o $(WORKER1_BIN) $(LDFLAGS); \
		echo "Робітник 1 успішно зібраний: $(WORKER1_BIN)"; \
	else \
		echo "Файл $(SRC_DIR)/worker1.c очікується від колеги (Issue #2)."; \
	fi

# Ціль для компіляції робітника 2 (тестувальник, Issue #3)
worker2: $(COMMON_OBJS)
	@if [ -f $(SRC_DIR)/worker2.c ]; then \
		$(CC) $(CFLAGS) $(SRC_DIR)/worker2.c $(COMMON_OBJS) -o $(WORKER2_BIN) $(LDFLAGS); \
		echo "Робітник 2 успішно зібраний: $(WORKER2_BIN)"; \
	else \
		echo "Файл $(SRC_DIR)/worker2.c очікується від колеги (Issue #3)."; \
	fi

# Ціль для збирання тестового стенду семафорів
$(TEST_BIN): $(TEST_DIR)/test_semaphores.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Тестування модуля семафорів
test: $(TEST_BIN)
	./$(TEST_BIN)

# Повне очищення бінарних файлів, об'єктних файлів, логів та IPC-ресурсів
clean:
	@echo "Видалення скомпільованих файлів та логів..."
	rm -rf $(TARGET) $(PARENT_BIN) $(WORKER1_BIN) $(WORKER2_BIN) $(TEST_BIN) $(OBJ_DIR)
	rm -f breaks.log break_log.txt /tmp/factory_qc_fifo /tmp/zavod_fifo
	@echo "Видалення системних семафорів із /dev/shm..."
	rm -f /dev/shm/sem.zavod_break_sem /dev/shm/sem.factory_break_room
	@echo "Очищення залишків System V IPC через ipcrm (у разі наявності)..."
	-ipcrm -a 2>/dev/null || true
	@echo "Очищення завершено успішно."

# Команди для роботи в Docker-контейнері
docker-build:
	docker build -t $(IMAGE_NAME) .

docker-shell:
	docker run --rm -it \
		--ipc=host \
		-v "$$(pwd)":/workspace \
		$(IMAGE_NAME)

docker-run:
	docker run --rm \
		--ipc=host \
		-v "$$(pwd)":/workspace \
		$(IMAGE_NAME) bash -c "make clean && make && ./$(TARGET) 10"

.PHONY: all clean parent worker1 worker2 test docker-build docker-shell docker-run
