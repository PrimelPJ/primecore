#include "../include/isa.h"
#include "../include/cache.h"
#include "../include/predictor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MEM_SIZE (4 * 1024 * 1024)   /* 4 MB */

static void seed_matrix(PrimeCoreState *s, uint64_t base, int n) {
    for (int i = 0; i < n * n; i++) {
        uint64_t v = (uint64_t)(i + 1);
        memcpy(s->mem + base + i * 8, &v, 8);
    }
}

static void print_matrix(PrimeCoreState *s, uint64_t base, int n,
                          const char *name) {
    printf("%s:\n", name);
    for (int i = 0; i < n; i++) {
        printf("  ");
        for (int j = 0; j < n; j++) {
            uint64_t v;
            memcpy(&v, s->mem + base + (i * n + j) * 8, 8);
            printf("%6llu ", (unsigned long long)v);
        }
        printf("\n");
    }
}

static void run_binary(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *prog = malloc(sz);
    fread(prog, 1, sz, f);
    fclose(f);

    PrimeCoreState *s = pc_state_create(MEM_SIZE);
    pc_reset(s);

    /* Seed input matrices A and B */
    seed_matrix(s, 0x0000, 4);
    seed_matrix(s, 0x0080, 4);

    pc_load(s, prog, sz, 0x1000);
    s->pc = 0x1000;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pc_run(s);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) +
                     (t1.tv_nsec - t0.tv_nsec) * 1e-9;

    print_matrix(s, 0x0100, 4, "Result C = A*B");
    pc_dump_stats(s);
    printf("  Wall time: %.4f s\n\n", elapsed);

    /* Cache simulation */
    CacheHierarchy hier;
    hierarchy_init(&hier);

    /* Replay memory accesses through cache model */
    for (uint64_t addr = 0; addr < 0x200; addr += 8) {
        hierarchy_read(&hier, addr, s->mem, MEM_SIZE);
        hierarchy_write(&hier, 0x100 + addr % 0x80, s->mem, MEM_SIZE);
    }
    hierarchy_dump(&hier);
    hierarchy_destroy(&hier);

    /* Branch predictor demo */
    BranchPredictor bp;
    bp_init(&bp);
    uint64_t dummy_target;
    /* Simulate a loop branch: taken 15 times, not taken once */
    for (int i = 0; i < 16; i++) {
        BranchDir pred = bp_predict(&bp, 0x1020, 0, 0, &dummy_target);
        BranchDir actual = (i < 15) ? BP_TAKEN : BP_NOT_TAKEN;
        bp_update(&bp, 0x1020, actual, 0x1020, 0, 0);
        (void)pred;
    }
    bp_dump_stats(&bp);

    pc_state_destroy(s);
    free(prog);
}

static void print_banner(void) {
    printf(
        "\n"
        " ______   ______     __     __    __     ______     ______     ______     ______     ______    \n"
        "/\\  == \\ /\\  == \\   /\\ \\   /\\ \"-./  \\   /\\  ___\\   /\\  ___\\   /\\  __ \\   /\\  == \\   /\\  ___\\  \n"
        "\\ \\  _-/ \\ \\  __<   \\ \\ \\  \\ \\ \\-./\\ \\  \\ \\  __\\   \\ \\ \\____  \\ \\ \\/\\ \\  \\ \\  __<   \\ \\  __\\  \n"
        " \\ \\_\\    \\ \\_\\ \\_\\  \\ \\_\\  \\ \\_\\ \\ \\_\\  \\ \\_____\\  \\ \\_____\\  \\ \\_____\\  \\ \\_\\ \\_\\  \\ \\_____\\ \n"
        "  \\/_/     \\/_/ /_/   \\/_/   \\/_/  \\/_/   \\/_____/   \\/_____/   \\/_____/   \\/_/ /_/   \\/_____/ \n"
        "\n"
        "  PrimeCore Architecture Simulator  v1.0\n"
        "  12-stage superscalar  |  12-wide fetch  |  512-bit vectors  |  3-level cache  |  Tournament BP\n"
        "\n"
    );
}

int main(int argc, char **argv) {
    print_banner();

    if (argc >= 2) {
        printf("Running binary: %s\n\n", argv[1]);
        run_binary(argv[1]);
    } else {
        printf("Usage: pcsim <program.bin>\n");
        printf("       pcasm <program.pca> <program.bin> && pcsim <program.bin>\n\n");
        printf("Demos: make run-matmul\n\n");
    }

    return 0;
}
