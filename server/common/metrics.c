#include "metrics.h"

#include <string.h>

void tt_metrics_aggregate_avg(const void* samples, uint32_t count,
                              size_t cell_size, void* out) {
  if (!samples || !out || count == 0)
    return;

  uint64_t cpu = 0, mem = 0, net_rx_bytes = 0, net_tx_bytes = 0;
  uint64_t load1 = 0, load5 = 0, load15 = 0;
  uint64_t du_total = 0, du_free = 0;
  uint32_t nr_running = 0, nr_total = 0;
  uint64_t last_ts = 0;

  for (uint32_t i = 0; i < count; i++) {
    const struct tt_metrics* s =
        (const struct tt_metrics*)((const uint8_t*)samples + i * cell_size);
    cpu += s->cpu_usage_pct;
    mem += s->mem_usage_pct;
    net_rx_bytes += s->net_rx_bytes;
    net_tx_bytes += s->net_tx_bytes;
    load1 += s->load_1min;
    load5 += s->load_5min;
    load15 += s->load_15min;
    nr_running += s->nr_running;
    nr_total += s->nr_total;
    // du_usage += s->du_usage;
    du_total += s->du_total_bytes;
    du_free += s->du_free_bytes;
    if (s->timestamp > last_ts)
      last_ts = s->timestamp;
  }

  struct tt_metrics* agg = (struct tt_metrics*)out;
  memset(agg, 0, sizeof(*agg));
  agg->timestamp = last_ts;
  agg->cpu_usage_pct = (uint16_t)(cpu / count);
  agg->mem_usage_pct = (uint16_t)(mem / count);
  agg->net_rx_bytes = (uint32_t)(net_rx_bytes / count);
  agg->net_tx_bytes = (uint32_t)(net_tx_bytes / count);
  agg->load_1min = (uint16_t)(load1 / count);
  agg->load_5min = (uint16_t)(load5 / count);
  agg->load_15min = (uint16_t)(load15 / count);
  agg->nr_running = nr_running / count;
  agg->nr_total = nr_total / count;
  // agg->du_usage = (uint16_t)(du_usage / count);
  agg->du_total_bytes = du_total / count;
  agg->du_free_bytes = du_free / count;
}

void tt_metrics_aggregate_max(const void* samples, uint32_t count,
                              size_t cell_size, void* out) {
  if (!samples || !out || count == 0)
    return;

  const struct tt_metrics* first = (const struct tt_metrics*)samples;
  struct tt_metrics* agg = (struct tt_metrics*)out;
  *agg = *first;

  for (uint32_t i = 1; i < count; i++) {
    const struct tt_metrics* s =
        (const struct tt_metrics*)((const uint8_t*)samples + i * cell_size);
#define MAX_FIELD(f) \
  if (s->f > agg->f) \
  agg->f = s->f
    MAX_FIELD(cpu_usage_pct);
    MAX_FIELD(mem_usage_pct);
    MAX_FIELD(net_rx_bytes);
    MAX_FIELD(net_tx_bytes);
    MAX_FIELD(load_1min);
    MAX_FIELD(load_5min);
    MAX_FIELD(load_15min);
    MAX_FIELD(nr_running);
    MAX_FIELD(nr_total);
    // MAX_FIELD(du_usage);
    MAX_FIELD(du_total_bytes);
    MAX_FIELD(du_free_bytes);
    if (s->timestamp > agg->timestamp)
      agg->timestamp = s->timestamp;
#undef MAX_FIELD
  }
}

void tt_metrics_aggregate_min(const void* samples, uint32_t count,
                              size_t cell_size, void* out) {
  if (!samples || !out || count == 0)
    return;

  const struct tt_metrics* first = (const struct tt_metrics*)samples;
  struct tt_metrics* agg = (struct tt_metrics*)out;
  *agg = *first;

  for (uint32_t i = 1; i < count; i++) {
    const struct tt_metrics* s =
        (const struct tt_metrics*)((const uint8_t*)samples + i * cell_size);
#define MIN_FIELD(f) \
  if (s->f < agg->f) \
  agg->f = s->f
    MIN_FIELD(cpu_usage_pct);
    MIN_FIELD(mem_usage_pct);
    MIN_FIELD(net_rx_bytes);
    MIN_FIELD(net_tx_bytes);
    MIN_FIELD(load_1min);
    MIN_FIELD(load_5min);
    MIN_FIELD(load_15min);
    MIN_FIELD(nr_running);
    MIN_FIELD(nr_total);
    // MIN_FIELD(du_usage);
    MIN_FIELD(du_total_bytes);
    MIN_FIELD(du_free_bytes);
    if (s->timestamp > agg->timestamp)
      agg->timestamp = s->timestamp;
#undef MIN_FIELD
  }
}

void tt_metrics_aggregate_agg(const void* samples, uint32_t count,
                              size_t cell_size, struct tt_agg_metrics* out) {
  if (!samples || !out || count == 0)
    return;
  tt_metrics_aggregate_avg(samples, count, cell_size, &out->avg);
  tt_metrics_aggregate_min(samples, count, cell_size, &out->min);
  tt_metrics_aggregate_max(samples, count, cell_size, &out->max);
}
