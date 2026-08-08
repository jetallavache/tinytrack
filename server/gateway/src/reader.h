#ifndef TINYTRACK_READER_H
#define TINYTRACK_READER_H

#include "common/proto/v1.h"
#include "common/proto/v2.h"
#include "common/ringbuf.h"

#define tt_metrics tt_metrics_ex

/* Wrapper around ttr_reader for tinytrack */
struct ttg_reader {
  struct ttr_reader ring;
};

int ttg_reader_open(struct ttg_reader* ctx, const char* path);
int ttg_reader_get_latest(struct ttg_reader* ctx, struct tt_metrics* out);

/*
 * Read up to `count` historical samples from ring level (RING_LEVEL_L1/L2/L3).
 * Writes into `out` array. Returns number of samples read, or negative on
 * error.
 */
int ttg_reader_get_history(struct ttg_reader* ctx, uint8_t level,
                           struct tt_metrics* out, int count);

/*
 * Fill tt_proto_ring_stat for a given ring level.
 * Returns 0 on success, -1 on invalid level.
 */
int ttg_reader_get_stats(struct ttg_reader* ctx, uint8_t level,
                         struct tt_proto_ring_stat* out);

/*
 * Fill tt_proto_sysinfo with hostname, OS string, uptime and ring capacities.
 * Returns 0 on success.
 */
int ttg_reader_get_sysinfo(struct ttg_reader* ctx,
                           struct tt_proto_sysinfo* out);

void ttg_reader_close(struct ttg_reader* ctx);

/*
 * Check if tinytd is alive by inspecting writer_pid and last_update_ts.
 * Returns 0 if alive, -1 if stale/dead (logs a warning).
 * Non-fatal: gateway continues even if tinytd is not running.
 */
int ttg_reader_check_liveness(struct ttg_reader* ctx);

#endif
