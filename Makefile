CC     = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS=

CORE_SRCS = core/isa.c core/cache.c core/predictor.c core/main.c
ASM_SRCS  = assembler/assembler.c

.PHONY: all clean run-matmul

all: build/pcsim build/pcasm

build:
	mkdir -p build

build/pcsim: $(CORE_SRCS) | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/pcasm: $(ASM_SRCS) | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/matrix_multiply.bin: build/pcasm benchmarks/matrix_multiply.pca
	./build/pcasm benchmarks/matrix_multiply.pca build/matrix_multiply.bin

run-matmul: build/pcsim build/matrix_multiply.bin
	./build/pcsim build/matrix_multiply.bin

clean:
	rm -rf build
