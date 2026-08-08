#ifndef TT_METRICS_H
#define TT_METRICS_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "cell.h"
/* Redefine the new structure for the data slot */
#include "metrics_ex.h"

#define TT_METRICS_CELL_VERSION 1

/**
 * @struct tt_metrics
 * @brief Stored data in a single cell (slot) of a time-series database.
 * @note The set of system metrics collected by tinytd and stored
 * in the ring buffer. This is the single source of truth for what we
 * collect and how it is encoded.
 * @attention All percentage values are stored as integer * 100 (e.g. 25.5% ->
 * 2550) to avoid floating point in the shared memory region.
 */
struct tt_metrics {
  alignas(8) struct tt_cell_header hdr;
  alignas(8) uint64_t timestamp;      /* ms since epoch */
  alignas(8) uint64_t du_total_bytes; /* Total disk space, bytes */
  alignas(8) uint64_t du_free_bytes;  /* Free disk space, bytes */
  alignas(4) uint32_t net_rx_bytes;   /* Network RX, bytes/sec */
  alignas(4) uint32_t net_tx_bytes;   /* Network TX, bytes/sec */
  alignas(4) uint32_t nr_running;     /* Running processes */
  alignas(4) uint32_t nr_total;       /* Total processes */
  alignas(2) uint16_t cpu_usage_pct;  /* CPU usage * 100 */
  alignas(2) uint16_t mem_usage_pct;  /* Memory usage * 100 */
  alignas(2) uint16_t load_1min;      /* Load average 1m * 100 */
  alignas(2) uint16_t load_5min;      /* Load average 5m * 100 */
  alignas(2) uint16_t load_15min;     /* Load average 15m * 100 */
  // uint16_t du_usage;       /* Disk usage * 100 */
  alignas(1) uint8_t _reserved[6];
}; /* Total 64 bytes */

_Static_assert(sizeof(struct tt_metrics) == 64,
               "tt_metrics_ex size must be 64 bytes");
_Static_assert(alignof(struct tt_metrics) == 8,
               "tt_metrics_ex alignment must be 8");

/**
 * @brief Average all numeric fields across N samples.
 * timestamp is set to the latest sample's timestamp.
 * @note Conforms to ttr_aggregate_fn signature.
 */
void tt_metrics_aggregate_avg(const void* samples, uint32_t count,
                              size_t cell_size, void* out);

/**
 * @brief Take the maximum value of each field across N
 * samples. Useful for peak-detection aggregation (e.g. worst-case CPU spike
 * over a window). timestamp is set to the latest sample's timestamp.
 * @note Conforms to ttr_aggregate_fn signature.
 */
void tt_metrics_aggregate_max(const void* samples, uint32_t count,
                              size_t cell_size, void* out);

/**
 * @brief Take the minimum value of each field across N
 * samples. Useful for detecting idle periods or free-space floors.
 * timestamp is set to the latest sample's timestamp.
 * @note Conforms to ttr_aggregate_fn signature.
 */
void tt_metrics_aggregate_min(const void* samples, uint32_t count,
                              size_t cell_size, void* out);

/* Alias kept for backward compatibility — resolves to aggregate_avg. */
// #define tt_metrics_aggregate tt_metrics_aggregate_avg

/**
 * @struct tt_agg_metrics
 * @brief Aggregated metrics for L2/L3 ring levels.
 * @note Stores min/max/avg per window so peaks are not lost.
 * L1 continues to use tt_metrics (raw samples).
 * @attention Wire size: 3 * sizeof(tt_metrics) = 156 bytes.
 * The `is_aggregated` flag in PKT_HISTORY_RESP distinguishes L1 vs L2/L3.
 */
struct tt_agg_metrics {
  struct tt_metrics avg; /* Average over the aggregation window */
  struct tt_metrics min; /* Per-field minimum */
  struct tt_metrics max; /* Per-field maximum */
}; /* 156 bytes */

/**
 * @brief Compute tt_agg_metrics (avg+min+max) from N tt_metrics samples.
 */
void tt_metrics_aggregate_agg(const void* samples, uint32_t count,
                              size_t cell_size, struct tt_agg_metrics* out);

#endif /* TT_METRICS_H */
