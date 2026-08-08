#ifndef TTD_WRITER_H
#define TTD_WRITER_H

#include "common/event.h"
#include "common/metrics.h"
#include "common/ringbuf.h"
#include "config.h"

/**
 * Ответственность:
 * - metrics - TSDB
 * - events - event buffer
 */

/* Wrapper around ttr_writer for tinytd */
struct ttd_writer {
  struct ttr_writer ring;
};

#define tt_metrics tt_metrics_ex

int ttd_writer_init(struct ttd_writer* ctx, struct ttd_config* cfg);
int ttd_writer_write_l1(struct ttd_writer* ctx, struct tt_metrics* sample);
int ttd_writer_aggregate_l2(struct ttd_writer* ctx);
int ttd_writer_aggregate_l3(struct ttd_writer* ctx);
int ttd_writer_write_le(struct ttd_writer* ctx, struct tt_event* sample);
int ttd_writer_shadow_sync(struct ttd_writer* ctx);
void ttd_writer_cleanup(struct ttd_writer* ctx);

#endif /* TTD_WRITER_H */
