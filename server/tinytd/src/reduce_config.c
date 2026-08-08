#include "reduce_config.h"

#include <stddef.h>

#include "common/metrics.h"
#include "common/reduce.h"
#include "cpu.h"
#include "mem.h"

#define tt_metrics tt_metrics_ex

static const struct tt_reduce_aggregator ttd_aggregators[] = {
    {
        .name = "timestamp",
        .offset = offsetof(struct tt_metrics, timestamp),
        .size = sizeof(uint64_t),
        .create = tt_reduce_latest_u64_create,
        .accumulate = tt_reduce_latest_u64_accumulate,
        .destroy = tt_reduce_latest_u64_destroy,
    },
    {
        .name = "du_total_bytes",
        .offset = offsetof(struct tt_metrics, du_total_bytes),
        .size = sizeof(uint64_t),
        .create = tt_reduce_ema_u64_create,
        .accumulate = tt_reduce_ema_u64_accumulate,
        .destroy = tt_reduce_ema_u64_destroy,
    },
    {
        .name = "du_free_bytes",
        .offset = offsetof(struct tt_metrics, du_free_bytes),
        .size = sizeof(uint64_t),
        .create = tt_reduce_ema_u64_create,
        .accumulate = tt_reduce_ema_u64_accumulate,
        .destroy = tt_reduce_ema_u64_destroy,
    },
    {
        .name = "nr_running",
        .offset = offsetof(struct tt_metrics, nr_running),
        .size = sizeof(uint32_t),
        .create = tt_reduce_ema_u32_create,
        .accumulate = tt_reduce_ema_u32_accumulate,
        .destroy = tt_reduce_ema_u32_destroy,
    },
    {
        .name = "nr_total",
        .offset = offsetof(struct tt_metrics, nr_total),
        .size = sizeof(uint32_t),
        .create = tt_reduce_ema_u32_create,
        .accumulate = tt_reduce_ema_u32_accumulate,
        .destroy = tt_reduce_ema_u32_destroy,
    },
    {
        .name = "net_rx_bytes",
        .offset = offsetof(struct tt_metrics, net_rx_bytes),
        .size = sizeof(uint32_t),
        .create = tt_reduce_ema_u32_create,
        .accumulate = tt_reduce_ema_u32_accumulate,
        .destroy = tt_reduce_ema_u32_destroy,
    },
    {
        .name = "net_tx_bytes",
        .offset = offsetof(struct tt_metrics, net_tx_bytes),
        .size = sizeof(uint32_t),
        .create = tt_reduce_ema_u32_create,
        .accumulate = tt_reduce_ema_u32_accumulate,
        .destroy = tt_reduce_ema_u32_destroy,
    },
    {
        .name = "net_state_flags",
        .offset = offsetof(struct tt_metrics, net_state_flags),
        .size = sizeof(uint16_t),
        .create = NULL,
        .accumulate = NULL,
        .destroy = NULL,
    },
    {
        .name = "cpu_usage_pct",
        .offset = offsetof(struct tt_metrics, cpu_usage_pct),
        .size = sizeof(uint16_t),
        .create = tt_reduce_ema_u16_create,
        .accumulate = tt_reduce_ema_u16_accumulate,
        .destroy = tt_reduce_ema_u16_destroy,
    },
    {
        .name = "cpu_state_flags",
        .offset = offsetof(struct tt_metrics, cpu_state_flags),
        .size = sizeof(uint16_t),
        .create = ttd_cpu_reduce_create,
        .accumulate = ttd_cpu_reduce_accumulate,
        .destroy = ttd_cpu_reduce_destroy,
    },
    {
        .name = "mem_usage_pct",
        .offset = offsetof(struct tt_metrics, mem_usage_pct),
        .size = sizeof(uint16_t),
        .create = tt_reduce_ema_u16_create,
        .accumulate = tt_reduce_ema_u16_accumulate,
        .destroy = tt_reduce_ema_u16_destroy,
    },
    {
        .name = "mem_state_flags",
        .offset = offsetof(struct tt_metrics, mem_state_flags),
        .size = sizeof(uint16_t),
        .create = ttd_mem_reduce_create,
        .accumulate = ttd_mem_reduce_accumulate,
        .destroy = ttd_mem_reduce_destroy,
    },
    {
        .name = "load_1min",
        .offset = offsetof(struct tt_metrics, load_1min),
        .size = sizeof(uint16_t),
        .create = tt_reduce_ema_u16_create,
        .accumulate = tt_reduce_ema_u16_accumulate,
        .destroy = tt_reduce_ema_u16_destroy,
    },
    {
        .name = "load_5min",
        .offset = offsetof(struct tt_metrics, load_5min),
        .size = sizeof(uint16_t),
        .create = tt_reduce_ema_u16_create,
        .accumulate = tt_reduce_ema_u16_accumulate,
        .destroy = tt_reduce_ema_u16_destroy,
    },
    {
        .name = "load_15min",
        .offset = offsetof(struct tt_metrics, load_15min),
        .size = sizeof(uint16_t),
        .create = tt_reduce_ema_u16_create,
        .accumulate = tt_reduce_ema_u16_accumulate,
        .destroy = tt_reduce_ema_u16_destroy,
    },
    {
        .name = "crit_count",
        .offset = offsetof(struct tt_metrics, crit_count),
        .size = sizeof(uint8_t),
        .create = NULL,
        .accumulate = NULL,
        .destroy = NULL,
    },
};

const struct tt_reduce_actions ttd_reduce_actions = {
    .reducers = ttd_aggregators,
    .count = (uint32_t)sizeof(ttd_aggregators) /
             (uint32_t)sizeof(ttd_aggregators[0]),
};