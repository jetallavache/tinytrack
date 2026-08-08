#include "writer.h"

#include <alloca.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "common/log/log.h"
#include "layout.h"
#include "seqlock.h"
#include "shm.h"

static uint64_t get_timestamp_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Format Unix ms timestamp as "YYYY-MM-DD HH:MM:SS" into buf[20]. */
static void fmt_ts_ms(uint64_t ms, char* buf, size_t size) {
  if (ms == 0) {
    snprintf(buf, size, "(none)");
    return;
  }
  time_t sec = (time_t)(ms / 1000);
  struct tm tm;
  localtime_r(&sec, &tm);
  strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm);
}

/* Adler32 over buf[0..len), skipping the checksum field (bytes 8..11) */
static uint32_t adler32(const void* buf, size_t len) {
  const uint8_t* p = (const uint8_t*)buf;
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; i++) {
    if (i >= 8 && i < 12)
      continue;
    a = (a + p[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

static void mark_dirty(struct ttr_writer* ctx, size_t off, size_t len) {
  if (ctx->dirty_min > off)
    ctx->dirty_min = off;
  if (ctx->dirty_max < off + len)
    ctx->dirty_max = off + len;
}

static void init_meta(struct ttr_meta* m, uint32_t capacity,
                      uint32_t cell_size) {
  m->seq = m->head = m->_reserved = 0;
  m->capacity = capacity;
  m->cell_size = cell_size;
  m->first_ts = m->last_ts = 0;
}

static struct ttr_meta* level_meta(const struct ttr_writer* ctx, int level) {
  size_t cs = ctx->cfg.cell_size;
  uint8_t* base = (uint8_t*)ctx->live_addr;
  switch (level) {
    case 1:
      return (struct ttr_meta*)(base + TTR_HEADER_SIZE +
                                TTR_CONSUMER_TABLE_SIZE);
    case 2:
      return (struct ttr_meta*)(base + ttr_layout_l2_meta_offset(
                                           ctx->cfg.l1_capacity, cs));
    case 3:
      return (struct ttr_meta*)(base + ttr_layout_l3_meta_offset(
                                           ctx->cfg.l1_capacity,
                                           ctx->cfg.l2_capacity, cs));
    case 4:
      return (struct ttr_meta*)(base + ttr_layout_le_meta_offset(
                                           ctx->cfg.l1_capacity,
                                           ctx->cfg.l2_capacity,
                                           ctx->cfg.l3_capacity, cs));
    default:
      return NULL;
  }
}

static uint8_t* level_data(const struct ttr_writer* ctx, int level) {
  size_t cs = ctx->cfg.cell_size;
  uint8_t* base = (uint8_t*)ctx->live_addr;
  switch (level) {
    case 1:
      return base + ttr_layout_l1_offset();
    case 2:
      return base + ttr_layout_l2_offset(ctx->cfg.l1_capacity, cs);
    case 3:
      return base + ttr_layout_l3_offset(ctx->cfg.l1_capacity,
                                         ctx->cfg.l2_capacity, cs);
    case 4:
      return base + ttr_layout_le_offset(ctx->cfg.l1_capacity,
                                         ctx->cfg.l2_capacity,
                                         ctx->cfg.l3_capacity, cs);
    default:
      return NULL;
  }
}

static size_t level_meta_offset(const struct ttr_writer* ctx, int level) {
  size_t cs = ctx->cfg.cell_size;
  switch (level) {
    case 2:
      return ttr_layout_l2_meta_offset(ctx->cfg.l1_capacity, cs);
    case 3:
      return ttr_layout_l3_meta_offset(ctx->cfg.l1_capacity,
                                       ctx->cfg.l2_capacity, cs);
    default:
      return 0;
  }
}

static size_t level_data_offset(const struct ttr_writer* ctx, int level) {
  size_t cs = ctx->cfg.cell_size;
  switch (level) {
    case 2:
      return ttr_layout_l2_offset(ctx->cfg.l1_capacity, cs);
    case 3:
      return ttr_layout_l3_offset(ctx->cfg.l1_capacity, ctx->cfg.l2_capacity,
                                  cs);
    default:
      return 0;
  }
}

static int shadow_is_valid(const void* addr, size_t size) {
  const struct ttr_header* hdr = (const struct ttr_header*)addr;
  if (hdr->magic != TTR_MAGIC || hdr->version != TTR_VERSION ||
      hdr->last_update_ts == 0)
    return 0;
  if (hdr->checksum != 0 && hdr->checksum != adler32(addr, size)) {
    tt_log_err("Shadow checksum mismatch: stored=0x%08x computed=0x%08x",
               hdr->checksum, adler32(addr, size));
    return 0;
  }
  return 1;
}

int ttr_writer_recover_from_shadow(struct ttr_writer* ctx) {
  if (!ctx->live_addr || !ctx->shadow_addr)
    return 0;
  if (!shadow_is_valid(ctx->shadow_addr, ctx->total_size)) {
    tt_log_info("Shadow is not valid, skipping recovery");
    return 0;
  }
  memcpy(ctx->live_addr, ctx->shadow_addr, ctx->total_size);
  struct ttr_header* hdr = (struct ttr_header*)ctx->live_addr;
  hdr->writer_pid = getpid();
  hdr->last_update_ts = get_timestamp_ms();
  ctx->dirty_min = 0;
  ctx->dirty_max = ctx->total_size;
  {
    char ts_buf[20];
    fmt_ts_ms(hdr->last_shadow_sync_ts, ts_buf, sizeof(ts_buf));
    tt_log_notice("Recovered %zu bytes from shadow (last_sync=%s)",
                  ctx->total_size, ts_buf);
  }
  return 1;
}

int ttr_writer_init(struct ttr_writer* ctx,
                    const struct ttr_writer_config* cfg) {
  ctx->cfg = *cfg;
  ctx->dirty_min = SIZE_MAX;
  ctx->dirty_max = 0;
  ctx->total_size =
      tt_layout_total_size(cfg->l1_capacity, cfg->l2_capacity, cfg->l3_capacity,
                           cfg->le_capacity, cfg->cell_size);

  if ((intptr_t)(ctx->live_addr = ttr_shm_create(
                     cfg->live_path, ctx->total_size, cfg->file_mode)) < 0) {
    tt_log_err("Failed to create live mmap (%s)",
               tt_shm_strerror((intptr_t)ctx->live_addr));
    return TTR_WRITER_ERR_LIVE_CREATE;
  }
  if ((intptr_t)(ctx->shadow_addr = ttr_shm_create(
                     cfg->shadow_path, ctx->total_size, cfg->file_mode)) < 0) {
    tt_log_err("Failed to create shadow mmap (%s)",
               tt_shm_strerror((intptr_t)ctx->shadow_addr));
    ttr_shm_dealloc(ctx->live_addr, ctx->total_size);
    return TTR_WRITER_ERR_SHADOW_CREATE;
  }

  if (!cfg->auto_recover || !ttr_writer_recover_from_shadow(ctx)) {
    struct ttr_header* hdr = (struct ttr_header*)ctx->live_addr;
    hdr->magic = TTR_MAGIC;
    hdr->version = TTR_VERSION;
    hdr->writer_pid = getpid();
    hdr->num_consumers = 0;
    hdr->interval_ms = cfg->interval_ms;
    hdr->l2_agg_interval_ms = cfg->l2_agg_interval_ms;
    hdr->l3_agg_interval_ms = cfg->l3_agg_interval_ms;
    hdr->le_check_interval_ms = cfg->le_check_interval_ms;
    hdr->last_update_ts = hdr->last_shadow_sync_ts = get_timestamp_ms();

    init_meta(level_meta(ctx, 1), cfg->l1_capacity, cfg->cell_size);
    init_meta(level_meta(ctx, 2), cfg->l2_capacity, cfg->cell_size);
    init_meta(level_meta(ctx, 3), cfg->l3_capacity, cfg->cell_size);
    init_meta(level_meta(ctx, 4), cfg->le_capacity, cfg->cell_size);

    msync(ctx->live_addr, ctx->total_size, MS_SYNC);
    ctx->dirty_min = 0;
    ctx->dirty_max = ctx->total_size;
  }
  /* Hint kernel: L1 is accessed randomly (latest sample), L2/L3/LE
   * sequentially. madvise/MADV_* require _GNU_SOURCE; guard with #ifdef for
   * strict C11. */
#if defined(MADV_RANDOM) && defined(MADV_SEQUENTIAL)
  madvise(level_data(ctx, 1), cfg->l1_capacity * cfg->cell_size, MADV_RANDOM);
  madvise(level_data(ctx, 2), cfg->l2_capacity * cfg->cell_size,
          MADV_SEQUENTIAL);
  madvise(level_data(ctx, 3), cfg->l3_capacity * cfg->cell_size,
          MADV_SEQUENTIAL);
  madvise(level_data(ctx, 4), cfg->le_capacity * cfg->cell_size,
          MADV_SEQUENTIAL);
#endif

  {
    const struct ttr_header* hdr2 = (const struct ttr_header*)ctx->live_addr;
    char ts_buf[20];
    fmt_ts_ms(hdr2->last_shadow_sync_ts, ts_buf, sizeof(ts_buf));
    tt_log_info("Ring       size=%zu KB  crc=%s  last_sync=%s",
                ctx->total_size / 1024, cfg->enable_crc ? "on" : "off", ts_buf);
  }
  return TTR_WRITER_OK;
}

int ttr_writer_write_l1(struct ttr_writer* ctx, const void* sample) {
  if (!ctx || !ctx->live_addr || !sample) {
    tt_log_err("ttr_writer_write_l1: NULL argument");
    return TTR_WRITER_ERR_NULL;
  }
  size_t cs = ctx->cfg.cell_size;
  struct ttr_header* hdr = (struct ttr_header*)ctx->live_addr;
  hdr->last_update_ts = get_timestamp_ms();

  struct ttr_meta* meta = level_meta(ctx, 1);
  uint8_t* data = level_data(ctx, 1);

  ttr_seqlock_write_begin(&meta->seq);
  uint32_t head = meta->head;
  memcpy(data + head * cs, sample, cs);
  meta->last_ts = hdr->last_update_ts;
  if (meta->first_ts == 0)
    meta->first_ts = meta->last_ts;
  meta->head = (head + 1) % meta->capacity;
  ttr_seqlock_write_end(&meta->seq);

  mark_dirty(ctx, 0, TTR_HEADER_SIZE);
  mark_dirty(ctx, TTR_HEADER_SIZE + TTR_CONSUMER_TABLE_SIZE, TTR_META_SIZE);
  mark_dirty(ctx, ttr_layout_l1_offset() + head * cs, cs);

  /* Подумать над тем, как оптимизировать. Говорят, это излишне, вызывать каждый
   * раз */
  msync(ctx->live_addr, ctx->total_size, MS_ASYNC);
  return TTR_WRITER_OK;
}

/* Aggregate src_level -> dst_level using ctx->cfg.aggregate callback */
static int ring_aggregate(struct ttr_writer* ctx, int src_level,
                          int dst_level) {
  size_t cs = ctx->cfg.cell_size;
  struct ttr_meta* src = level_meta(ctx, src_level);
  uint8_t* src_data = level_data(ctx, src_level);
  struct ttr_meta* dst = level_meta(ctx, dst_level);
  uint8_t* dst_data = level_data(ctx, dst_level);

  uint32_t available = src->head;
  if (available == 0)
    return TTR_WRITER_ERR_NODATA;

  uint32_t n = available < src->capacity ? available : src->capacity;

  /* Use heap instead of VLA to avoid stack overflow under -O0/valgrind */
  uint8_t* tmp = malloc(n * cs);
  if (!tmp)
    return TTR_WRITER_ERR_NULL;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t idx = (src->head - n + i + src->capacity) % src->capacity;
    memcpy(tmp + i * cs, src_data + idx * cs, cs);
  }

  uint8_t agg[256]; /* cs is sizeof(tt_metrics) = 52, 256 is safe upper bound */
  if (cs > sizeof(agg)) {
    tt_log_err("ring_aggregate: cell_size %zu exceeds agg buffer", cs);
    free(tmp);
    return TTR_WRITER_ERR_NULL;
  }
  ctx->cfg.aggregate(tmp, n, cs, agg, ctx->cfg.reduce_actions);
  free(tmp);

  ttr_seqlock_write_begin(&dst->seq);
  uint32_t head = dst->head;
  memcpy(dst_data + head * cs, agg, cs);
  dst->last_ts = get_timestamp_ms();
  if (dst->first_ts == 0)
    dst->first_ts = dst->last_ts;
  dst->head = (head + 1) % dst->capacity;
  ttr_seqlock_write_end(&dst->seq);

  mark_dirty(ctx, level_meta_offset(ctx, dst_level), TTR_META_SIZE);
  mark_dirty(ctx, level_data_offset(ctx, dst_level) + head * cs, cs);
  return TTR_WRITER_OK;
}

int ttr_writer_aggregate_l2(struct ttr_writer* ctx) {
  if (!ctx || !ctx->live_addr) {
    tt_log_err("ttr_writer_aggregate_l2: NULL ctx");
    return TTR_WRITER_ERR_NULL;
  }
  return ring_aggregate(ctx, 1, 2);
}

int ttr_writer_aggregate_l3(struct ttr_writer* ctx) {
  if (!ctx || !ctx->live_addr) {
    tt_log_err("ttr_writer_aggregate_l3: NULL ctx");
    return TTR_WRITER_ERR_NULL;
  }
  return ring_aggregate(ctx, 2, 3);
}

int ttr_writer_write_le(struct ttr_writer* ctx, const void* sample) {
  if (!ctx || !ctx->live_addr || !sample) {
    tt_log_err("ttr_writer_write_le: NULL argument");
    return TTR_WRITER_ERR_NULL;
  }
  size_t cs = ctx->cfg.cell_size;
  struct ttr_header* hdr = (struct ttr_header*)ctx->live_addr;
  hdr->last_update_ts = get_timestamp_ms();

  struct ttr_meta* meta = level_meta(ctx, 4);
  uint8_t* data = level_data(ctx, 4);

  ttr_seqlock_write_begin(&meta->seq);
  uint32_t head = meta->head;
  memcpy(data + head * cs, sample, cs);
  meta->last_ts = hdr->last_update_ts;
  if (meta->first_ts == 0)
    meta->first_ts = meta->last_ts;
  meta->head = (head + 1) % meta->capacity;
  ttr_seqlock_write_end(&meta->seq);

  mark_dirty(ctx, 0, TTR_HEADER_SIZE);
  mark_dirty(
      ctx,
      ttr_layout_le_meta_offset(ctx->cfg.l1_capacity, ctx->cfg.l2_capacity,
                                ctx->cfg.l3_capacity, cs),
      TTR_META_SIZE);
  mark_dirty(ctx,
             ttr_layout_le_offset(ctx->cfg.l1_capacity, ctx->cfg.l2_capacity,
                                  ctx->cfg.l3_capacity, cs) +
                 head * cs,
             cs);

  /* Подумать над тем, как оптимизировать. Говорят, это излишне, вызывать каждый
   * раз */
  msync(ctx->live_addr, ctx->total_size, MS_ASYNC);
  return TTR_WRITER_OK;
}

int ttr_writer_shadow_sync(struct ttr_writer* ctx) {
  if (ctx->dirty_min >= ctx->dirty_max)
    return TTR_WRITER_OK;

  size_t off = ctx->dirty_min;
  size_t len = ctx->dirty_max - ctx->dirty_min;

  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  memcpy((uint8_t*)ctx->shadow_addr + off, (uint8_t*)ctx->live_addr + off, len);

  uint64_t now = get_timestamp_ms();
  ((struct ttr_header*)ctx->live_addr)->last_shadow_sync_ts = now;
  ((struct ttr_header*)ctx->shadow_addr)->last_shadow_sync_ts = now;

  ((struct ttr_header*)ctx->shadow_addr)->checksum =
      ctx->cfg.enable_crc ? adler32(ctx->shadow_addr, ctx->total_size) : 0;

  if (msync(ctx->shadow_addr, ctx->total_size, MS_SYNC) < 0)
    tt_log_err("shadow_sync: msync failed: %s", strerror(errno));

  gettimeofday(&t1, NULL);
  tt_log_debug("shadow_sync: copied %zu bytes in %ld us", len,
               (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec));

  ctx->dirty_min = ctx->total_size;
  ctx->dirty_max = 0;
  return TTR_WRITER_OK;
}

void ttr_writer_cleanup(struct ttr_writer* ctx) {
  if (ctx->live_addr)
    ttr_shm_dealloc(ctx->live_addr, ctx->total_size);
  if (ctx->shadow_addr)
    ttr_shm_dealloc(ctx->shadow_addr, ctx->total_size);
}
