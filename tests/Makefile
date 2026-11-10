# Host unit tests for the portable application code (Core/Src/app).
#
#   make            build and run
#   make SAN=1      also enable AddressSanitizer + UndefinedBehaviorSanitizer
#   make CC=clang   use another compiler

CC     ?= cc
BUILD  ?= build
ROOT   := ..

APP_SRCS  := $(wildcard $(ROOT)/Core/Src/app/*.c)
TEST_SRCS := $(wildcard *.c)

CFLAGS := -std=c11 -O1 -g -I$(ROOT)/Core/Inc -I. \
          -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Werror
LDFLAGS :=

ifeq ($(SAN),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
LDFLAGS += -fsanitize=address,undefined
endif

BIN := $(BUILD)/unit_tests

.PHONY: all run clean

all: run

$(BIN): $(APP_SRCS) $(TEST_SRCS) $(wildcard *.h) $(wildcard $(ROOT)/Core/Inc/app/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(APP_SRCS) $(TEST_SRCS) $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD)
