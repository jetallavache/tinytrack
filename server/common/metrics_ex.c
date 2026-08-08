
#include "metrics_ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/log_internal.h"
#include "reduce.h"
#include "wire.h"

void tt_metrics_ex_serialize(const struct tt_metrics_ex* m, uint8_t* buf) {
  size_t off = 0;

  tt_wire_write_be64(buf + off, m->timestamp);
  off += 8;
  tt_wire_write_be64(buf + off, m->du_total_bytes);
  off += 8;
  tt_wire_write_be64(buf + off, m->du_free_bytes);
  off += 8;

  tt_wire_write_be32(buf + off, m->net_rx_bytes);
  off += 4;
  tt_wire_write_be32(buf + off, m->net_tx_bytes);
  off += 4;
  tt_wire_write_be32(buf + off, m->nr_running);
  off += 4;
  tt_wire_write_be32(buf + off, m->nr_total);
  off += 4;

  tt_wire_write_be16(buf + off, m->cpu_usage_pct);
  off += 2;
  tt_wire_write_be16(buf + off, m->cpu_state_flags);
  off += 2;
  tt_wire_write_be16(buf + off, m->mem_usage_pct);
  off += 2;
  tt_wire_write_be16(buf + off, m->mem_state_flags);
  off += 2;
  tt_wire_write_be16(buf + off, m->net_state_flags);
  off += 2;
  tt_wire_write_be16(buf + off, m->load_1min);
  off += 2;
  tt_wire_write_be16(buf + off, m->load_5min);
  off += 2;
  tt_wire_write_be16(buf + off, m->load_15min);
  off += 2;

  tt_wire_write_u8(buf + off, m->crit_count);
  off += 1;
  memcpy(buf + off, m->reserved, 7);
  off += 7;
}

void tt_metrics_ex_deserialize(const uint8_t* buf, struct tt_metrics_ex* m) {
  size_t off = 0;

  m->timestamp = tt_wire_read_be64(buf + off);
  off += 8;
  m->du_total_bytes = tt_wire_read_be64(buf + off);
  off += 8;
  m->du_free_bytes = tt_wire_read_be64(buf + off);
  off += 8;

  m->net_rx_bytes = tt_wire_read_be32(buf + off);
  off += 4;
  m->net_tx_bytes = tt_wire_read_be32(buf + off);
  off += 4;
  m->nr_running = tt_wire_read_be32(buf + off);
  off += 4;
  m->nr_total = tt_wire_read_be32(buf + off);
  off += 4;

  m->cpu_usage_pct = tt_wire_read_be16(buf + off);
  off += 2;
  m->cpu_state_flags = tt_wire_read_be16(buf + off);
  off += 2;
  m->mem_usage_pct = tt_wire_read_be16(buf + off);
  off += 2;
  m->mem_state_flags = tt_wire_read_be16(buf + off);
  off += 2;
  m->net_state_flags = tt_wire_read_be16(buf + off);
  off += 2;
  m->load_1min = tt_wire_read_be16(buf + off);
  off += 2;
  m->load_5min = tt_wire_read_be16(buf + off);
  off += 2;
  m->load_15min = tt_wire_read_be16(buf + off);
  off += 2;

  m->crit_count = tt_wire_read_u8(buf + off);
  off += 1;
  memcpy(m->reserved, buf + off, 7);
  off += 7;
}

void tt_metrics_ex_reduce(const void* samples, uint32_t count, size_t cell_size,
                          void* out, const void* actions_ptr) {
  if (!samples || !out || count == 0)
    return;

  tt_log_debug(
      "[dbg reduce] \n"
      "> samples=%p, count=%u, actions_ptr=%p\n",
      samples, count, actions_ptr);

  if (!actions_ptr) {
    tt_log_err("(fatal) actions_ptr is NULL!\n");
    return;
  }

  const struct tt_reduce_actions* actions =
      (const struct tt_reduce_actions*)actions_ptr;

  tt_log_debug(
      "[dbg reduce] \n"
      "> actions->reducers=%p, actions->count=%u\n",
      actions->reducers, actions->count);

  if (!actions->reducers) {
    tt_log_err("(fatal) actions->reducers is NULL!\n");
    return;
  }

  struct tt_reduce_accumulator** accumulators = NULL;
  uint32_t c = 0;

  if (actions && actions->reducers && actions->count > 0) {
    c = actions->count;
    accumulators = calloc(c, sizeof(*accumulators));

    for (uint32_t i = 0; i < c; i++) {
      tt_log_debug(
          "[dbg reduce] \n"
          "> reducer[%u] name='%s', create=%p\n",
          i, actions->reducers[i].name, actions->reducers[i].create);

      if (actions->reducers[i].create) {
        accumulators[i] = actions->reducers[i].create();
        tt_log_debug(
            "[dbg reduce] \n"
            "> accumulator[%u] = %p\n",
            i, accumulators[i]);
      } else {
        tt_log_err("reducer[%u]: '%s' has NULL create function!\n", i,
                   actions->reducers[i].name);
      }
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    /*
    const struct tt_metrics_ex* s =
        (const struct tt_metrics_ex*)((const uint8_t*)samples +
                                            i * cell_size);
    */
    const uint8_t* s = (const uint8_t*)samples + i * cell_size;
    bool is_last = (i == count - 1);

    for (uint32_t f = 0; f < c; f++) {
      if (!accumulators[f])
        continue;

      const struct tt_reduce_aggregator* aggr = &actions->reducers[f];
      const void* field_value = s + aggr->offset;

      /* Perform aggregation according to the specified configuration */
      aggr->accumulate(accumulators[f], field_value, count, i, is_last, NULL);
    }
  }

  struct tt_metrics_ex* result = (struct tt_metrics_ex*)out;
  memset(result, 0, cell_size);

  result->hdr.version = TT_METRICS_EX_CELL_VERSION;
  result->hdr.type = TT_CELL_METRICS_L2;
  result->hdr.flags = 0x0;
  result->hdr.serial = 0;

  for (uint32_t i = 0; i < c; i++) {
    if (!accumulators[i])
      continue;

    const struct tt_reduce_aggregator* aggr = &actions->reducers[i];
    void* result_field = (uint8_t*)result + aggr->offset;

    /* Save */
    aggr->accumulate(accumulators[i], NULL, count, count, true, result_field);
    aggr->destroy(accumulators[i]);
  }

  free(accumulators);
}