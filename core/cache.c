#include "../include/cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * L1I  512 KB  8-way  64B lines   4 cycles
 * L1D  512 KB  8-way  64B lines   4 cycles
 * L2   32  MB 16-way  64B lines  12 cycles
 * L3   256 MB 32-way  64B lines  40 cycles
 * ------------------------------------------------------------------------- */

static const struct { int sets; int ways; int latency; } CONFIGS[] = {
    [CACHE_L1I] = { 1024,  8,  4 },
    [CACHE_L1D] = { 1024,  8,  4 },
    [CACHE_L2]  = { 32768, 16, 12 },
    [CACHE_L3]  = { 65536, 32, 40 },
};

void cache_init(Cache *c, CacheLevel level, int sets, int ways, int latency) {
    c->sets     = sets;
    c->ways     = ways;
    c->latency  = latency;
    c->level    = level;
    c->accesses = 0;
    c->hits     = 0;
    c->misses   = 0;
    c->evictions= 0;
    c->tick     = 0;

    /* log2 calculations */
    c->line_bits = 6;  /* log2(64) */
    int s = sets;
    c->set_bits = 0;
    while (s >>= 1) c->set_bits++;

    c->lines = calloc(sets * ways, sizeof(CacheLine));
    assert(c->lines);
}

void cache_destroy(Cache *c) {
    free(c->lines);
    c->lines = NULL;
}

static CacheLine *get_line(Cache *c, int set, int way) {
    return &c->lines[set * c->ways + way];
}

static int find_lru(Cache *c, int set) {
    int lru_way = 0;
    uint64_t min_tick = UINT64_MAX;
    for (int w = 0; w < c->ways; w++) {
        CacheLine *l = get_line(c, set, w);
        if (!l->valid) return w;
        if (l->last_used < min_tick) {
            min_tick = l->last_used;
            lru_way  = w;
        }
    }
    return lru_way;
}

CacheResult cache_access(Cache *c, uint64_t addr, int write,
                         Cache *next, uint8_t *mem, size_t mem_size) {
    c->accesses++;
    c->tick++;

    uint64_t tag  = addr >> (c->line_bits + c->set_bits);
    int      set  = (addr >> c->line_bits) & (c->sets - 1);
    int      off  = addr & (CACHE_LINE_BYTES - 1);

    /* Probe all ways */
    for (int w = 0; w < c->ways; w++) {
        CacheLine *l = get_line(c, set, w);
        if (l->valid && l->tag == tag) {
            l->last_used = c->tick;
            if (write) l->dirty = 1;
            c->hits++;
            return CACHE_HIT;
        }
    }

    /* Miss */
    c->misses++;
    int evict_way = find_lru(c, set);
    CacheLine *victim = get_line(c, set, evict_way);

    /* Write-back dirty line */
    if (victim->valid && victim->dirty && mem) {
        uint64_t wb_addr = (victim->tag << (c->line_bits + c->set_bits)) |
                           ((uint64_t)set << c->line_bits);
        if (wb_addr + CACHE_LINE_BYTES <= mem_size) {
            memcpy(mem + wb_addr, victim->data, CACHE_LINE_BYTES);
        }
        c->evictions++;
    }

    /* Fill from next level or memory */
    uint64_t line_addr = addr & ~(uint64_t)(CACHE_LINE_BYTES - 1);
    if (line_addr + CACHE_LINE_BYTES <= mem_size) {
        memcpy(victim->data, mem + line_addr, CACHE_LINE_BYTES);
    }

    victim->tag       = tag;
    victim->valid     = 1;
    victim->dirty     = write;
    victim->last_used = c->tick;

    (void)off;
    (void)next;

    return (c->level == CACHE_L1I || c->level == CACHE_L1D)
           ? CACHE_MISS_L1
           : (c->level == CACHE_L2) ? CACHE_MISS_L2 : CACHE_MISS_L3;
}

void cache_flush(Cache *c) {
    memset(c->lines, 0, c->sets * c->ways * sizeof(CacheLine));
}

void cache_dump_stats(const Cache *c, const char *name) {
    double hr = c->accesses
                ? 100.0 * (double)c->hits / c->accesses
                : 0.0;
    printf("  %-6s  accesses=%-10llu hits=%-10llu misses=%-10llu  hit_rate=%.2f%%\n",
           name,
           (unsigned long long)c->accesses,
           (unsigned long long)c->hits,
           (unsigned long long)c->misses,
           hr);
}

/* ---- Hierarchy ---------------------------------------------------------- */

void hierarchy_init(CacheHierarchy *h) {
    cache_init(&h->l1i, CACHE_L1I,
               CONFIGS[CACHE_L1I].sets, CONFIGS[CACHE_L1I].ways,
               CONFIGS[CACHE_L1I].latency);
    cache_init(&h->l1d, CACHE_L1D,
               CONFIGS[CACHE_L1D].sets, CONFIGS[CACHE_L1D].ways,
               CONFIGS[CACHE_L1D].latency);
    cache_init(&h->l2,  CACHE_L2,
               CONFIGS[CACHE_L2].sets,  CONFIGS[CACHE_L2].ways,
               CONFIGS[CACHE_L2].latency);
    cache_init(&h->l3,  CACHE_L3,
               CONFIGS[CACHE_L3].sets,  CONFIGS[CACHE_L3].ways,
               CONFIGS[CACHE_L3].latency);
    h->mem_accesses       = 0;
    h->total_latency_cycles = 0;
}

void hierarchy_destroy(CacheHierarchy *h) {
    cache_destroy(&h->l1i);
    cache_destroy(&h->l1d);
    cache_destroy(&h->l2);
    cache_destroy(&h->l3);
}

CacheResult hierarchy_read(CacheHierarchy *h, uint64_t addr,
                           uint8_t *mem, size_t mem_size) {
    CacheResult r = cache_access(&h->l1d, addr, 0, &h->l2, mem, mem_size);
    if (r == CACHE_HIT) { h->total_latency_cycles += h->l1d.latency; return r; }
    r = cache_access(&h->l2, addr, 0, &h->l3, mem, mem_size);
    if (r == CACHE_HIT) { h->total_latency_cycles += h->l2.latency; return r; }
    r = cache_access(&h->l3, addr, 0, NULL, mem, mem_size);
    h->total_latency_cycles += h->l3.latency;
    if (r != CACHE_HIT) h->mem_accesses++;
    return r;
}

CacheResult hierarchy_write(CacheHierarchy *h, uint64_t addr,
                            uint8_t *mem, size_t mem_size) {
    CacheResult r = cache_access(&h->l1d, addr, 1, &h->l2, mem, mem_size);
    if (r != CACHE_HIT) {
        cache_access(&h->l2, addr, 1, &h->l3, mem, mem_size);
        cache_access(&h->l3, addr, 1, NULL,   mem, mem_size);
        h->mem_accesses++;
    }
    return r;
}

void hierarchy_dump(const CacheHierarchy *h) {
    printf("\n=== Cache hierarchy statistics ===\n");
    cache_dump_stats(&h->l1d, "L1D");
    cache_dump_stats(&h->l1i, "L1I");
    cache_dump_stats(&h->l2,  "L2");
    cache_dump_stats(&h->l3,  "L3");
    printf("  Main memory accesses : %llu\n",
           (unsigned long long)h->mem_accesses);
    printf("  Total latency cycles : %llu\n",
           (unsigned long long)h->total_latency_cycles);
    printf("\n");
}
