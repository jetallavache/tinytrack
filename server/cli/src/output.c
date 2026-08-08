#include "output.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

const char* ttc_color(const struct ttc_ctx* ctx, const char* code) {
  return ctx->color ? code : "";
}

const char* ttc_pct_color(const struct ttc_ctx* ctx, double pct, double warn,
                          double crit) {
  if (!ctx->color)
    return "";
  if (pct >= crit)
    return COL_RED;
  if (pct >= warn)
    return COL_YELLOW;
  return COL_GREEN;
}

void ttc_fmt_bytes(unsigned long bytes, char* buf, size_t len) {
  if (bytes >= 1UL << 30)
    snprintf(buf, len, "%.1f GB", bytes / (double)(1UL << 30));
  else if (bytes >= 1UL << 20)
    snprintf(buf, len, "%.1f MB", bytes / (double)(1UL << 20));
  else if (bytes >= 1UL << 10)
    snprintf(buf, len, "%.1f KB", bytes / (double)(1UL << 10));
  else
    snprintf(buf, len, "%lu B", bytes);
}

void ttc_fmt_ts(uint64_t ts_ms, char* buf, size_t len) {
  if (ts_ms == 0) {
    snprintf(buf, len, "--:--:--");
    return;
  }
  time_t t = (time_t)(ts_ms / 1000);
  struct tm* tm = localtime(&t);
  strftime(buf, len, "%H:%M:%S", tm);
}

void ttc_print_sep(const struct ttc_ctx* ctx, int width) {
  printf("%s", ttc_color(ctx, COL_DIM));
  for (int i = 0; i < width; i++)
    putchar('-');
  printf("%s\n", ttc_color(ctx, COL_RESET));
}

void ttc_print_field(const struct ttc_ctx* ctx, const char* key,
                     const char* value, bool last) {
  if (ctx->format == FMT_JSON) {
    printf("  \"%s\": \"%s\"%s\n", key, value, last ? "" : ",");
  } else {
    printf("  %-20s %s\n", key, value);
  }
}

/* Sparkline chars for a value 0..100 */
static char sparkchar(double pct) {
  static const char ascii[] = " .,:;|!I#";
  int idx = (int)(pct / 100.0 * 8.0);
  if (idx < 0)
    idx = 0;
  if (idx > 8)
    idx = 8;
  return ascii[idx];
}

void ttc_print_metrics(const struct ttc_ctx* ctx, const struct tt_metrics* m) {
  char ts[16], rx[16], tx[16], total[16], free_[16];
  ttc_fmt_ts(m->timestamp, ts, sizeof(ts));
  ttc_fmt_bytes(m->net_rx_bytes, rx, sizeof(rx));
  ttc_fmt_bytes(m->net_tx_bytes, tx, sizeof(tx));
  ttc_fmt_bytes(m->du_total_bytes, total, sizeof(total));
  ttc_fmt_bytes(m->du_free_bytes, free_, sizeof(free_));

  double cpu = m->cpu_usage_pct / 100.0;
  double mem = m->mem_usage_pct / 100.0;
  double disk =
      ((m->du_total_bytes - m->du_free_bytes) * 100 / m->du_total_bytes);
  double load = m->load_1min / 100.0;

  if (ctx->format == FMT_JSON) {
    printf("{\n");
    printf("  \"timestamp\": %llu,\n", (unsigned long long)m->timestamp);
    printf("  \"cpu\": %.2f,\n", cpu);
    printf("  \"mem\": %.2f,\n", mem);
    printf("  \"net_rx_bytes_bps\": %u,\n", m->net_rx_bytes);
    printf("  \"net_tx_bytes_bps\": %u,\n", m->net_tx_bytes);
    printf("  \"load_1m\": %.2f,\n", load);
    printf("  \"load_5m\": %.2f,\n", m->load_5min / 100.0);
    printf("  \"load_15m\": %.2f,\n", m->load_15min / 100.0);
    printf("  \"procs_running\": %u,\n", m->nr_running);
    printf("  \"procs_total\": %u,\n", m->nr_total);
    printf("  \"disk_pct\": %.2f,\n", disk);
    printf("  \"disk_total\": %llu,\n", (unsigned long long)m->du_total_bytes);
    printf("  \"disk_free\": %llu\n", (unsigned long long)m->du_free_bytes);
    printf("}\n");
    return;
  }

  if (ctx->format == FMT_COMPACT) {
    printf(
        "[%s] %sCPU%s:%5.1f%% %sMEM%s:%5.1f%% "
        "LOAD:%.2f NET:↓%s/s ↑%s/s\n",
        ts, ttc_pct_color(ctx, cpu, 70, 90), ttc_color(ctx, COL_RESET), cpu,
        ttc_pct_color(ctx, mem, 80, 95), ttc_color(ctx, COL_RESET), mem, load,
        rx, tx);
    return;
  }

  /* Table format */
  ttc_print_sep(ctx, 44);
  printf(" %s%s%s\n", ttc_color(ctx, COL_BOLD), ts, ttc_color(ctx, COL_RESET));
  ttc_print_sep(ctx, 44);

  printf(" %sCPU%s   %s%5.1f%%%s  %c\n", ttc_color(ctx, COL_BOLD),
         ttc_color(ctx, COL_RESET), ttc_pct_color(ctx, cpu, 70, 90), cpu,
         ttc_color(ctx, COL_RESET), sparkchar(cpu));
  printf(" %sMEM%s   %s%5.1f%%%s  %c\n", ttc_color(ctx, COL_BOLD),
         ttc_color(ctx, COL_RESET), ttc_pct_color(ctx, mem, 80, 95), mem,
         ttc_color(ctx, COL_RESET), sparkchar(mem));
  printf(" %sDISK%s  %s%5.1f%%%s  %s / %s\n", ttc_color(ctx, COL_BOLD),
         ttc_color(ctx, COL_RESET), ttc_pct_color(ctx, disk, 80, 95), disk,
         ttc_color(ctx, COL_RESET), free_, total);
  printf(" %sNET%s   ↓%s/s  ↑%s/s\n", ttc_color(ctx, COL_BOLD),
         ttc_color(ctx, COL_RESET), rx, tx);
  printf(" %sLOAD%s  %.2f  %.2f  %.2f\n", ttc_color(ctx, COL_BOLD),
         ttc_color(ctx, COL_RESET), load, m->load_5min / 100.0,
         m->load_15min / 100.0);
  printf(" %sPROC%s  %u/%u\n", ttc_color(ctx, COL_BOLD),
         ttc_color(ctx, COL_RESET), m->nr_running, m->nr_total);
}

void ttc_print_ring_level(const struct ttc_ctx* ctx, int level,
                          const struct ttr_meta* meta, const char* label) {
  char first[16], last[16];
  ttc_fmt_ts(meta->first_ts, first, sizeof(first));
  ttc_fmt_ts(meta->last_ts, last, sizeof(last));

  uint32_t filled = meta->head < meta->capacity ? meta->head : meta->capacity;
  double fill_pct =
      meta->capacity > 0 ? (double)filled / meta->capacity * 100.0 : 0.0;

  /* ASCII fill bar (20 chars wide) */
  char bar[21] = {0};
  int filled_chars = (int)(fill_pct / 100.0 * 20);
  for (int i = 0; i < 20; i++)
    bar[i] = i < filled_chars ? '#' : '.';

  printf(" %sL%d%s %-6s  [%s%s%s] %3.0f%%  %u/%u  %s→%s\n",
         ttc_color(ctx, COL_BOLD), level, ttc_color(ctx, COL_RESET), label,
         ttc_pct_color(ctx, fill_pct, 70, 90), bar, ttc_color(ctx, COL_RESET),
         fill_pct, filled, meta->capacity, first, last);
}
