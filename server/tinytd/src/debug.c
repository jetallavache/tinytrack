#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef TTD_DEBUG

#include <stdio.h>
#include <sys/resource.h>
#include <time.h>

#include "common/log/log_internal.h"
#include "common/ringbuf.h"
#include "cpu.h"
#include "debug.h"
#include "mem.h"

#define tt_metrics tt_metrics_ex

static void fmt_ts(uint64_t ts_ms, char* buf, size_t len) {
  if (ts_ms == 0) {
    snprintf(buf, len, " (none) ");
    return;
  }
  time_t t = (time_t)(ts_ms / 1000);
  struct tm* tm = localtime(&t);
  strftime(buf, len, "%H:%M:%S", tm);
}

static void metrics_printf(const struct tt_metrics* s) {
  char ts[16], mem_state[128], cpu_state[128];
  fmt_ts(s->timestamp, ts, sizeof(ts));
  ttd_mem_state_fmt(&s->mem_state_flags, mem_state, sizeof(mem_state));
  ttd_cpu_state_fmt(&s->cpu_state_flags, cpu_state, sizeof(cpu_state));

  float du_usage =
      ((s->du_total_bytes - s->du_free_bytes) * 100 / s->du_total_bytes);
  tt_log_debug(
      "[dbg metrics] \n"
      "> ts=%s \n"
      "> hdr.type=%hhu \n"
      "> cpu=%5.2f%%  %s \n"
      "> mem=%5.2f%%  %s \n"
      "> rx=%5u bps tx=%5u bps\n"
      "> load=%5.2f/%5.2f/%5.2f \n"
      "> du=%.2f%%",
      ts, s->hdr.type, s->cpu_usage_pct / 100.0, cpu_state,
      s->mem_usage_pct / 100.0, mem_state, s->net_rx_bytes, s->net_tx_bytes,
      s->load_1min / 100.0, s->load_5min / 100.0, s->load_15min / 100.0,
      du_usage);
}

static void event_printf(const struct tt_event* e) {
  char ts[16];
  fmt_ts(e->timestamp, ts, sizeof(ts));

  tt_log_debug(
      "[dbg event] \n"
      "> ts=%s \n"
      "> hdr.type=%hhu \n"
      "> event_code=%hu \n"
      "> severity=%hhu \n"
      "> component=%hhu \n"
      "> prev_state=%u \n"
      "> new_state=%u \n"
      "> metric_value=%hu \n"
      "> threshold=%hu \n"
      "> process_pid=%d \n"
      "> duration_ms=%u \n"
      "> event_id=%u \n"
      "> correlation_id=%lu \n",
      ts, e->hdr.type, e->event_code, e->severity, e->component, e->prev_state,
      e->new_state, e->metric_value, e->threshold, e->process_pid,
      e->duration_ms, e->event_id, e->correlation_id);
}

void ttd_debug_dump_l1(const void* live_addr, uint32_t l1_capacity) {
  size_t cell_size = sizeof(struct tt_metrics);

  const struct ttr_meta* meta =
      (const struct ttr_meta*)((const uint8_t*)live_addr + TTR_HEADER_SIZE +
                               TTR_CONSUMER_TABLE_SIZE);
  const uint8_t* data = (const uint8_t*)live_addr + ttr_layout_l1_offset();

  uint32_t head = meta->head;
  uint32_t filled = head < l1_capacity ? head : l1_capacity;

  /* Print last 3 L1 entries */
  uint32_t show = filled < 3 ? filled : 3;
  tt_log_debug(
      "[dbg L1]\n> head=%u  filled=%u/%u", /* Last %u entries:\n (show) */
      head, filled, l1_capacity);

  for (uint32_t i = 0; i < show; i++) {
    uint32_t idx = (head - show + i + l1_capacity) % l1_capacity;
    const struct tt_metrics* s =
        (const struct tt_metrics*)(data + idx * cell_size);
    metrics_printf(s);
  }
}

void ttd_debug_dump_agg(int level, const struct tt_metrics* agg, uint32_t head,
                        uint32_t capacity) {
  (void)head;
  (void)capacity;
  metrics_printf(agg);
}

void ttd_debug_dump_le(const void* live_addr, struct ttd_config* cfg) {
  size_t cs = sizeof(struct tt_event);

  const struct ttr_meta* meta =
      (const struct ttr_meta*)((const uint8_t*)live_addr +
                               ttr_layout_le_meta_offset(cfg->l1_capacity,
                                                         cfg->l2_capacity,
                                                         cfg->l3_capacity, cs));
  const uint8_t* data = (const uint8_t*)live_addr +
                        ttr_layout_le_offset(cfg->l1_capacity, cfg->l2_capacity,
                                             cfg->l3_capacity, cs);

  uint32_t head = meta->head;
  uint32_t filled = head < cfg->le_capacity ? head : cfg->le_capacity;

  /* Print last 3 L1 entries */
  uint32_t show = filled < 3 ? filled : 3;
  tt_log_debug(
      "[dbg LE]\n> head=%u  filled=%u/%u", /* Last %u entries:\n (show) */
      head, filled, cfg->le_capacity);

  for (uint32_t i = 0; i < show; i++) {
    uint32_t idx = (head - show + i + cfg->le_capacity) % cfg->le_capacity;
    const struct tt_event* e = (const struct tt_event*)(data + idx * cs);
    event_printf(e);
  }
}

void ttd_debug_dump_rusage(void) {
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) < 0)
    return;
  tt_log_debug(
      "[dbg rusage] \n"
      "> minflt=%ld majflt=%ld nvcsw=%ld nivcsw=%ld  inblock=%ld oublock=%ld",
      ru.ru_minflt, ru.ru_majflt, ru.ru_nvcsw, ru.ru_nivcsw, ru.ru_inblock,
      ru.ru_oublock);
}

#endif /* TTD_DEBUG */
