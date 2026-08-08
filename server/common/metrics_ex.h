#ifndef TT_METRICS_EX_H
#define TT_METRICS_EX_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "cell.h"

#define TT_METRICS_EX_CELL_VERSION 1

/**
 * @brief The extended set of system metrics collected by tinytd and stored
 * in the ring buffer.
 *
 * @note All percentage values are stored as integer * 100 (e.g. 25.5% -> 2550)
 * to avoid floating point in the shared memory region.
 */
struct tt_metrics_ex {
  alignas(8) struct tt_cell_header hdr;

  alignas(8) uint64_t timestamp; /* ms since epoch */

  alignas(8) uint64_t du_total_bytes; /* Total disk space, bytes */
  alignas(8) uint64_t du_free_bytes;  /* Free disk space, bytes */

  alignas(4) uint32_t nr_running; /* Running processes */
  alignas(4) uint32_t nr_total;   /* Total processes */

  alignas(8) uint64_t net_rx_bytes; /* COUNTER,  Network RX, bytes */
  alignas(8) uint64_t net_tx_bytes; /* COUNTER,  Network TX, bytes */
  alignas(2) uint16_t
      net_state_flags; /* GAUGE,    5x2 bits of network states */

  alignas(2) uint16_t cpu_usage_pct; /* GAUGE,    CPU usage * 100, percent */
  alignas(2) uint16_t
      cpu_state_flags; /* GAUGE,    5x2 bits of processor states */

  alignas(2) uint16_t mem_usage_pct; /* GAUGE,    Memory usage * 100, percent */
  alignas(2) uint16_t mem_state_flags; /* GAUGE,    5x2 bits of memory states */

  alignas(2) uint16_t load_1min;  /* Load average 1m * 100 */
  alignas(2) uint16_t load_5min;  /* Load average 5m * 100 */
  alignas(2) uint16_t load_15min; /* Load average 15m * 100 */

  /* New fields for detect events */

  alignas(2) uint16_t oom_kill_count;   /* COUNTER,  From /proc/vmstat */
  alignas(2) uint16_t inode_usage_pct;  /* Inodes % * 100 */
  alignas(2) uint16_t fd_usage_pct;     /* File descriptors % * 100 */

  alignas(2) uint16_t procs_blocked;    /* Processes in D-state */
  alignas(2) uint16_t procs_zombie;     /* Processes in Z-state */
  alignas(2) uint16_t top_consumer_pid; /* PID of the main CPU consumer */
  alignas(2) uint16_t top_consumer_pct; /* How much % CPU it uses */
  alignas(2) uint16_t top_rss_pid;      /* PID of the main RAM consumer */
  alignas(2) uint16_t top_rss_mb;       /* How many MB it uses */
  alignas(2) uint16_t swap_in_kb;       /* Swapin over the last second */
  alignas(2) uint16_t swap_out_kb;      /* Swapout for the last second */
  alignas(1) uint8_t kernel_tainted;    /* Kernel taint flag */

  /*
   * Количество критических состояний.
   * Что мы принимаем за ошибку, задавая этот показатель?
   *
   * crit_count увеличивается когда:
   * 1. Любой компонент переходит в RED состояние
   * 2. Произошел OOM kill
   * 3. Диск заполнен > 95%
   * 4. Обнаружена утечка памяти
   * 5. Swap thrashing активен
   * 6. Процесс в D-state > 30 секунд
   * 7. Network connectivity lost
   *
   * Это не детальные события, а счетчик "всего плохого"
   */
  alignas(1) uint8_t crit_count;

  /**
   * Рекомендую добавить (используя reserved)
   */
  // alignas(2) uint16_t swap_usage_pct;  // процент использования swap
  // alignas(2) uint16_t disk_inode_pct;  // процент использования inodes
  // alignas(1) uint8_t  oom_score;      // OOM score системы (0-100)
  // alignas(1) uint8_t  reserved[3];    // остаток зарезервирован

  alignas(1) uint8_t reserved[32];
}; /* Total 128 bytes */

_Static_assert(sizeof(struct tt_metrics_ex) == 128,
               "tt_metrics_ex size must be 64 bytes");
_Static_assert(alignof(struct tt_metrics_ex) == 8,
               "tt_metrics_ex alignment must be 8");

void tt_metrics_ex_serialize(const struct tt_metrics_ex* m, uint8_t* buf);
void tt_metrics_ex_deserialize(const uint8_t* buf, struct tt_metrics_ex* m);

/**
 * @brief Average all numeric fields across N samples.
 * timestamp is set to the latest sample's timestamp.
 * @note Conforms to ttr_aggregate_fn signature.
 */
void tt_metrics_ex_reduce_fn(const void* samples, uint32_t count, size_t cell_size,
                          void* out, const void* actions_ptr);

/* Alias kept for backward compatibility — resolves to aggregate_avg. */
#define tt_metrics_aggregate tt_metrics_ex_reduce_fn

/**
 * @struct tt_agg_metrics_ex
 * @brief Aggregated metrics for L2/L3 ring levels.
 * @note Stores min/max/avg per window so peaks are not lost.
 * L1 continues to use tt_metrics (raw samples).
 * @attention Wire size: 3 * sizeof(tt_metrics) = 156 bytes.
 * The `is_aggregated` flag in PKT_HISTORY_RESP distinguishes L1 vs L2/L3.
 */
struct tt_agg_metrics_ex {
  struct tt_metrics_ex avg; /* Average over the aggregation window */
  struct tt_metrics_ex min; /* Per-field minimum */
  struct tt_metrics_ex max; /* Per-field maximum */
};

#endif /* TT_METRICS_EX_H */
