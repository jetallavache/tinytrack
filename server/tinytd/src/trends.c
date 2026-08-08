#include "trends.h"

#define EMA_ALPHA 30 /* ema-smoothing, 0.30 at a fixed point */

void ttd_trends_memory(struct ttd_trends* tr, unsigned long swap_cached,
                       unsigned long s_unreclaim, time_t timestamp_ms) {
  uint8_t write_idx = tr->history_idx;
  tr->swap_cached_history[tr->history_idx] = swap_cached;
  tr->s_unreclaim_history[tr->history_idx] = s_unreclaim;
  tr->timestamps[tr->history_idx] = timestamp_ms;
  tr->history_idx = (tr->history_idx + 1) % 10;

  if (tr->history_count < 10)
    tr->history_count++;

  if (tr->history_count >= 2) {
    uint8_t oldest_idx = tr->history_idx;
    uint8_t newest_idx = write_idx;

    uint64_t dt_ms = tr->timestamps[newest_idx] - tr->timestamps[oldest_idx];
    if (dt_ms > 0) {
      /* swap_cached */
      int64_t swap_val_old = tr->swap_cached_history[oldest_idx];
      int64_t swap_val_new = (int64_t)tr->swap_cached_history[newest_idx];

      int64_t swap_diff;
      if (swap_val_new >= swap_val_old)
        swap_diff = (int64_t)(swap_val_new - swap_val_old);
      else
        swap_diff = -(int64_t)(swap_val_new - swap_val_old);

      int64_t swap_rate = (int64_t)(swap_diff * 1000 / dt_ms);

      if (tr->history_count == 2)
        tr->swap_cached_ema_rate = swap_rate;
      else {
        tr->swap_cached_ema_rate =
            (EMA_ALPHA * swap_rate +
             (100 - EMA_ALPHA) * tr->swap_cached_ema_rate) /
            100;
      }

      /* s_unreclaim */
      int64_t s_unreclaim_val_old = tr->s_unreclaim_history[oldest_idx];
      int64_t s_unreclaim_val_new = tr->s_unreclaim_history[newest_idx];

      int64_t s_unreclaim_diff;
      if (s_unreclaim_val_new >= s_unreclaim_val_old)
        s_unreclaim_diff = (int64_t)(s_unreclaim_val_new - s_unreclaim_val_old);
      else
        s_unreclaim_diff =
            -(int64_t)(s_unreclaim_val_new - s_unreclaim_val_old);

      int64_t s_unreclaim_rate = (int64_t)(s_unreclaim_diff * 1000 / dt_ms);

      if (tr->history_count == 2)
        tr->s_unreclaim_ema_rate = s_unreclaim_rate;
      else {
        tr->s_unreclaim_ema_rate =
            (EMA_ALPHA * s_unreclaim_rate +
             (100 - EMA_ALPHA) * tr->s_unreclaim_ema_rate) /
            100;
      }
    }
  }
}