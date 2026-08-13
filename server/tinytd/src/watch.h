#ifndef TTD_WATCH_H
#define TTD_WATCH_H

#include <stdint.h>

#include "common/event.h"
#include "common/metrics.h"
#include "writer.h"

/**
 * Ответственность:
 * история, состояние, детекция, корреляция и генерация событий
 */

#define tt_metrics tt_metrics_ex

/**
 * The latest known values (for detecting changes)
 */
struct ttd_watch_latest {
  uint16_t oom_kill_count;
  uint64_t net_rx_bytes;
  uint64_t net_tx_bytes;
  uint16_t fd_usage;
};

/**
 * Leak detection counters
 */
struct ttd_watch_leaks {
  uint64_t check_start_ts;
  uint16_t check_pid;
  uint32_t check_rss_start;
};

/**
 * Structure for storing the analysis context
 * @note Ring buffer of the last N samples for trends.
 */
struct ttd_watch {
  struct tt_metrics* recent_samples;
  uint32_t sample_count;
  uint32_t max_samples;
  uint64_t last_analysis_ts;

  struct ttd_watch_latest last;
  struct ttd_watch_leaks leaks;

  // struct ttd_watch_state state;
  // struct ttd_event_buffer *events;
  // struct ttd_trends *trends;
};

void ttd_watch_metrics(struct ttd_watch* watch, struct tt_metrics* current,
                       struct ttd_writer* writer);

void ttd_watch_init(struct ttd_watch* watch);
void ttd_watch_cleanup();

#endif /* TTD_WATCH_H */