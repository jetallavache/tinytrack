#ifndef TTD_TRENDS_H
#define TTD_TRENDS_H

#include <stdint.h>
#include <time.h>

#define TTD_TRENDS_COUNT 10

struct ttd_trends {
  uint64_t swap_cached_history[TTD_TRENDS_COUNT];
  uint64_t s_unreclaim_history[TTD_TRENDS_COUNT];
  time_t timestamps[TTD_TRENDS_COUNT];
  uint8_t history_idx;
  uint8_t history_count;
  int64_t swap_cached_ema_rate;
  int64_t s_unreclaim_ema_rate;
  int32_t warning_rate_kbps;  /* for example, 100 KB/sec */
  int32_t critical_rate_kbps; /* for example, 500 KB/sec */
};

void ttd_trends_memory(struct ttd_trends* tr, unsigned long swap_cached,
                       unsigned long s_unreclaim, time_t timestamp_ms);

#endif /* TTD_TRENDS_H */