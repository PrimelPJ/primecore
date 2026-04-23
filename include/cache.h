#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>

/*
 * PrimeCore cache hierarchy
 *
 * L1I  512 KB  8-way  64B lines  4-cycle latency   per-core
 * L1D  512 KB  8-way  64B lines  4-cycle latency   per-core
 * L2   32 MB  16-way  64B lines 12-cycle latency   per-cluster (4 cores)
 * L3   256 MB 32-way  64B lines 40-cycle latency   shared
 *
 * Apple M4 for reference:
 * L1I  192 KB  8-way  P-core
 * L1D  128 KB  8-way  P-core
 * L2   16 MB  shared
 * No L3 on-die
 *
 * Replacement policy: pseudo-LRU (PLRU) with victim buffer
 */

#define CACHE_LINE_BYTES  64

typedef enum {
    CACHE_L1I = 0,
    CACHE_L1D = 1,
    CACHE_L2  = 2,
    CACHE_L3  = 3,
} CacheLevel;

typedef enum {
    CACHE_HIT       = 0,
    CACHE_MISS_L1   = 1,
    CACHE_MISS_L2   = 2,
    CACHE_MISS_L3   = 3,   /* main memory access */
} CacheResult;

typedef struct CacheLine {
    uint64_t tag;
    uint8_t  data[CACHE_LINE_BYTES];
    int      valid;
    int      dirty;
    uint64_t last_used;    /* for PLRU                                */
} CacheLine;

typedef struct {
    CacheLine *lines;       /* ways * sets entries                     */
    int        sets;
    int        ways;
    int        line_bits;   /* log2(CACHE_LINE_BYTES)                  */
    int        set_bits;    /* log2(sets)                              */
    int        latency;     /* cycles                                  */
    uint64_t   accesses;
    uint64_t   hits;
    uint64_t   misses;
    uint64_t   evictions;
    uint64_t   tick;        /* for LRU ordering                        */
    CacheLevel level;
} Cache;

typedef struct {
    Cache l1i;
    Cache l1d;
    Cache l2;
    Cache l3;
    uint64_t mem_accesses;
    uint64_t total_latency_cycles;
} CacheHierarchy;

/* ---- API ---------------------------------------------------------------- */

void        cache_init(Cache *c, CacheLevel level, int sets, int ways,
                       int latency);
void        cache_destroy(Cache *c);
CacheResult cache_access(Cache *c, uint64_t addr, int write,
                         Cache *next_level, uint8_t *backing_mem,
                         size_t mem_size);
void        cache_flush(Cache *c);
void        cache_dump_stats(const Cache *c, const char *name);

void        hierarchy_init(CacheHierarchy *h);
void        hierarchy_destroy(CacheHierarchy *h);
CacheResult hierarchy_read(CacheHierarchy *h, uint64_t addr,
                           uint8_t *mem, size_t mem_size);
CacheResult hierarchy_write(CacheHierarchy *h, uint64_t addr,
                            uint8_t *mem, size_t mem_size);
void        hierarchy_dump(const CacheHierarchy *h);

#endif /* CACHE_H */
