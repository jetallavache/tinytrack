#ifndef TTD_MEM_H
#define TTD_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/reduce.h"
#include "fetch.h"
#include "trends.h"

/*
 ┌─────────────────────────────────────────────────────────┐
 │ Группа            │ Источники из meminfo                │
 ├───────────────────┼─────────────────────────────────────┤
 │ 1. Доступность    │ MemAvailable / MemTotal             │
 │ 2. Давление       │ Active + Inactive vs Total          │
 │ 3. Подкачка       │ SwapTotal, SwapFree, SwapCached     │
 │ 4. Анонимные      │ AnonPages                           │
 │    страницы       │                                     │
 │ 5. Грязные/IO     │ Dirty, Writeback                    │
 │ 6. Ядерные        │ Slab, SReclaimable, SUnreclaim,     │
 │    структуры      │ KernelStack, PageTables             │
 │ 7. Тренд          │ Скорость роста SwapCached           │
 │    SwapCached     │                                     │
 │ 8. Тренд          │ Скорость роста SUnreclaim           │
 │    SUnreclaim     │                                     │
 └─────────────────────────────────────────────────────────┘
*/

/*
 * The bit field of states (from low to high)
 */
struct mem_state_flags {
  uint16_t availability : 2;   /* Bits 0-1   — порог MemAvailable */
  uint16_t pressure : 2;       /* Bits 2-3   — давление на память */
  uint16_t swap : 2;           /* Bits 4-5   — состояние подкачки */
  uint16_t anon_pages : 2;     /* Bits 6-7   — анонимные страницы */
  uint16_t dirty_io : 2;       /* Bits 8-9   — грязные страницы   */
  uint16_t kernel_structs : 2; /* Bits 10-11 — ядерные аллокации  */
  uint16_t swap_cached : 2; /* Bits 12-13 — тренд SwapCached (скорость роста) */
  uint16_t s_unreclaim : 2; /* Bits 14-15 — тренд SUnreclaim (скорость роста) */
  /* ... */
  /* uint16_t reserved : 0; */
};

/* Offset constants — document the format */
#define MEM_STATE_AVAILABILITY_SHIFT 0
#define MEM_STATE_PRESSURE_SHIFT 2
#define MEM_STATE_SWAP_SHIFT 4
#define MEM_STATE_ANON_PAGES_SHIFT 6
#define MEM_STATE_DIRTY_IO_SHIFT 8
#define MEM_STATE_KERNEL_SHIFT 10
#define MEM_STATE_SWAP_CACHED_TREND_SHIFT 12
#define MEM_STATE_S_UNRECLAIM_TREND_SHIFT 14
/* Bits 0 reserved */

#define MEM_STATE_AVAILABILITY_MASK 0x0003 /* Bits 0-1   */
#define MEM_STATE_PRESSURE_MASK 0x000C     /* Bits 2-3   */
#define MEM_STATE_SWAP_MASK 0x0030         /* Bits 4-5   */
#define MEM_STATE_ANON_PAGES_MASK 0x00C0   /* Bits 6-7   */
#define MEM_STATE_DIRTY_IO_MASK 0x0300     /* Bits 8-9   */
#define MEM_STATE_KERNEL_MASK 0x0C00       /* Bits 10-11 */
#define MEM_STATE_SWAP_CACHED_TREND_MASK 0x3000
#define MEM_STATE_S_UNRECLAIM_TREND_MASK 0xC000

/* State values */
#define STATE_OK 0x0
#define STATE_WARNING 0x1
#define STATE_CRITICAL 0x2
#define STATE_RESERVED 0x3

// // availability (MemAvailable / MemTotal)
// #define MEM_AVAIL_GREEN   0  // > 20%
// #define MEM_AVAIL_YELLOW  1  // 10-20%
// #define MEM_AVAIL_ORANGE  2  // 5-10%
// #define MEM_AVAIL_RED     3  // < 5%

// // pressure (скорость изменения MemAvailable)
// #define MEM_PRESS_NORMAL  0  // стабильно или растет
// #define MEM_PRESS_ELEV    1  // снижается медленно (< 1%/min)
// #define MEM_PRESS_HIGH    2  // снижается быстро (1-5%/min)
// #define MEM_PRESS_CRIT    3  // резкое падение (> 5%/min)

// // swap (swap usage)
// #define MEM_SWAP_NONE     0  // < 5% swap used
// #define MEM_SWAP_LOW      1  // 5-20% swap used
// #define MEM_SWAP_MED      2  // 20-50% swap used
// #define MEM_SWAP_HIGH     3  // > 50% swap used

// // anon_pages (тренд анонимных страниц — детектор утечек)
// #define MEM_ANON_STABLE   0  // stable or decreasing
// #define MEM_ANON_GROWING  1  // growing < 1MB/min
// #define MEM_ANON_LEAK     2  // growing 1-10MB/min (potential leak)
// #define MEM_ANON_SPIKE    3  // growing > 10MB/min (active leak)

// // dirty_io (грязные страницы — риск потери данных)
// #define MEM_DIRTY_LOW     0  // < 10% of Dirty Ratio
// #define MEM_DIRTY_NORM    1  // 10-50%
// #define MEM_DIRTY_HIGH    2  // 50-80% (throttling soon)
// #define MEM_DIRTY_CRIT    3  // > 80% (writes throttled)

// // kernel_structs (SUnreclaim + KernelStack + PageTables)
// #define MEM_KERN_NORM     0  // < 5% of RAM
// #define MEM_KERN_ELEV     1  // 5-10% (watch)
// #define MEM_KERN_HIGH     2  // 10-20% (possible leak)
// #define MEM_KERN_CRIT     3  // > 20% (kernel memory leak)

// // swap_cached (тренд — если растет при нехватке памяти = trashing)
// #define MEM_SCACHE_DECR   0  // decreasing (good)
// #define MEM_SCACHE_STAB   1  // stable
// #define MEM_SCACHE_GROW   2  // growing (bad if under pressure)
// #define MEM_SCACHE_SPIKE  3  // rapid growth (trashing indicator)

// // s_unreclaim (неосвобождаемая память ядра — растет = утечка в ядре)
// #define MEM_SUNR_STABLE   0  // stable
// #define MEM_SUNR_GROW     1  // growing slowly
// #define MEM_SUNR_LEAK     2  // growing fast (kernel leak)
// #define MEM_SUNR_CRIT     3  // growing extremely (critical leak)

/* Threshold values should be set in the config. */
struct mem_state_thresholds {
  uint8_t avail_warning_pct;              /* Default 20 */
  uint8_t avail_critical_pct;             /* Default 10 */
  uint8_t active_warning_pct;             /* Default 70 */
  uint8_t active_critical_pct;            /* Default 90 */
  uint8_t swap_warning_pct;               /* Default 30 */
  uint8_t swap_critical_pct;              /* Default 60 */
  uint8_t anonp_warning_pct;              /* Default 50 */
  uint8_t anonp_critical_pct;             /* Default 80 */
  uint8_t dirty_warning_pct;              /* Default 5  */
  uint8_t dirty_critical_pct;             /* Default 10 */
  uint8_t kernel_warning_pct;             /* Default 15 */
  uint8_t kernel_critical_pct;            /* Default 25 */
  int32_t swap_cached_warning_rate_kbps;  /* Default 100 KB/sec */
  int32_t swap_cached_critical_rate_kbps; /* Default 500 KB/sec */
  int32_t s_unreclaim_warning_rate_kbps;  /* Default 100 KB/sec */
  int32_t s_unreclaim_critical_rate_kbps; /* Default 500 KB/sec */
};

/* If you need to keep a long history, you can use delta encoding. */
struct mem_state_delta {
  uint64_t timestamp_ms;
  uint16_t changed_mask; /* Bits of the changed groups */
  uint16_t new_states;   /* Only the changed states */
};

uint16_t ttd_mem_state(struct ttd_fetch* fch);
void ttd_mem_state_fmt(const uint16_t* wire_data, char* buf, size_t len);

struct tt_reduce_accumulator* ttd_mem_reduce_create(void);
void ttd_mem_reduce_accumulate(struct tt_reduce_accumulator* acc_ptr,
                               const void* value, uint32_t count,
                               uint32_t index, bool is_last, void* out);
void ttd_mem_reduce_destroy(struct tt_reduce_accumulator* acc_ptr);

#endif /* TTD_MEM_H */