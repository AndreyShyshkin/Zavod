CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS = -lrt -pthread

IMAGE_NAME = zavod-build-env
TARGET = factory
SRCS = $(wildcard src/*.c)

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) breaks.log /tmp/factory_qc_fifo

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

.PHONY: all clean docker-build docker-shell docker-run
