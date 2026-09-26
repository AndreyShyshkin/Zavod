#include <stdio.h>
#include "common.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <items_count>\n", argv[0]);
        return 1;
    }
    printf("Factory supervisor started. Items: %s\n", argv[1]);
    return 0;
}