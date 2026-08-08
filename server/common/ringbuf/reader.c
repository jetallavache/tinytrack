#include "reader.h"

#include <string.h>
#include <time.h>

#include "common/log/log.h"
#include "layout.h"
#include "seqlock.h"
#include "shm.h"

static void init_level_ptrs(struct ttr_reader* ctx) {
  uint8_t* base = (uint8_t*)ctx->addr;
  uint32_t l1cap = 0, cs = 0;

  ctx->l1_meta =
      (struct ttr_meta*)(base + TTR_HEADER_SIZE + TTR_CONSUMER_TABLE_SIZE);
  ctx->l1_data = base + ttr_layout_l1_offset();

  l1cap = ctx->l1_meta->capacity;
  cs = ctx->l1_meta->cell_size;

  ctx->l2_meta =
      (struct ttr_meta*)(base + ttr_layout_l2_meta_offset(l1cap, cs));
  ctx->l2_data = base + ttr_layout_l2_offset(l1cap, cs);

  ctx->l3_meta =
      (struct ttr_meta*)(base + ttr_layout_l3_meta_offset(
                                    l1cap, ctx->l2_meta->capacity, cs));
  ctx->l3_data = base + ttr_layout_l3_offset(l1cap, ctx->l2_meta->capacity, cs);

  ctx->le_meta = (struct ttr_meta*)(base + ttr_layout_le_meta_offset(
                                               l1cap, ctx->l2_meta->capacity,
                                               ctx->l3_meta->capacity, cs));
  ctx->le_data = base + ttr_layout_le_offset(l1cap, ctx->l2_meta->capacity,
                                             ctx->l3_meta->capacity, cs);
}

static int level_ptrs(const struct ttr_reader* ctx, int level,
                      struct ttr_meta** meta, uint8_t** data) {
  switch (level) {
    case 1:
      *meta = ctx->l1_meta;
      *data = ctx->l1_data;
      break;
    case 2:
      *meta = ctx->l2_meta;
      *data = ctx->l2_data;
      break;
    case 3:
      *meta = ctx->l3_meta;
      *data = ctx->l3_data;
      break;
    case 4:
      *meta = ctx->le_meta;
      *data = ctx->le_data;
      break;
    default:
      return TTR_READER_ERR_INVALID;
  }
  return *meta ? TTR_READER_OK : TTR_READER_ERR_INVALID;
}

int ttr_reader_open(struct ttr_reader* ctx, const char* path) {
  if (!ctx || !path) {
    tt_log_err("ttr_reader_open: NULL argument");
    return TTR_READER_ERR_INVALID;
  }
  if ((intptr_t)(ctx->addr = ttr_shm_read(path, &ctx->size)) < 0) {
    tt_log_err("Failed to read mmap (%s)",
               tt_shm_strerror((intptr_t)ctx->addr));
    return TTR_READER_ERR_READ;
  }

  const struct ttr_header* hdr = (const struct ttr_header*)ctx->addr;
  int err = (hdr->magic != TTR_MAGIC)       ? TTR_READER_ERR_MAGIC
            : (hdr->version != TTR_VERSION) ? TTR_READER_ERR_VERSION
                                            : TTR_READER_OK;
  if (err != TTR_READER_OK) {
    ttr_shm_dealloc(ctx->addr, ctx->size);
    return err;
  }

  init_level_ptrs(ctx);
  return TTR_READER_OK;
}

int ttr_reader_get_latest(struct ttr_reader* ctx, void* out, size_t out_size) {
  if (!ctx || !out || !ctx->l1_meta) {
    tt_log_err("ttr_reader_get_latest: invalid argument");
    return TTR_READER_ERR_INVALID;
  }

  const struct ttr_header* hdr = (const struct ttr_header*)ctx->addr;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  if (now_ms > hdr->last_update_ts && now_ms - hdr->last_update_ts > 3000)
    return TTR_READER_ERR_STALE;

  struct ttr_meta* meta = ctx->l1_meta;
  size_t cs = meta->cell_size;
  size_t copy_size = out_size < cs ? out_size : cs;

  uint32_t seq;
  do {
    seq = ttr_seqlock_read_begin(&meta->seq);
    uint32_t head = meta->head;
    /* head==0 with no data written yet (first_ts==0) means empty */
    if (head == 0 && meta->first_ts == 0)
      return TTR_READER_ERR_NODATA;
    /* latest sample is at (head - 1 + capacity) % capacity */
    uint32_t idx = (head == 0 ? meta->capacity : head) - 1;
    memcpy(out, ctx->l1_data + idx * cs, copy_size);
  } while (ttr_seqlock_read_retry(&meta->seq, seq));

  return TTR_READER_OK;
}

int ttr_reader_get_history(struct ttr_reader* ctx, int level, void* out,
                           size_t out_size, int count) {
  if (!ctx || !out || count <= 0) {
    tt_log_err("ttr_reader_get_history: invalid argument");
    return TTR_READER_ERR_INVALID;
  }

  struct ttr_meta* meta;
  uint8_t* data;
  int err = level_ptrs(ctx, level, &meta, &data);
  if (err != TTR_READER_OK)
    return err;

  size_t cs = meta->cell_size;
  size_t copy_size = out_size < cs ? out_size : cs;
  int actual;

  uint32_t seq;
  do {
    seq = ttr_seqlock_read_begin(&meta->seq);
    uint32_t head = meta->head;
    /* filled = number of valid entries: capacity once wrapped, head before */
    uint32_t filled = (meta->first_ts != 0 && head == 0)
                          ? meta->capacity
                          : (head < meta->capacity ? head : meta->capacity);
    if (filled == 0)
      return TTR_READER_ERR_NODATA;
    if ((uint32_t)count > filled)
      count = (int)filled;
    for (int i = 0; i < count; i++) {
      uint32_t idx = (head - count + i + meta->capacity) % meta->capacity;
      memcpy((uint8_t*)out + i * cs, data + idx * cs, copy_size);
    }
    actual = count;
  } while (ttr_seqlock_read_retry(&meta->seq, seq));

  return actual;
}

void ttr_reader_close(struct ttr_reader* ctx) {
  if (ctx && ctx->addr) {
    ttr_shm_dealloc(ctx->addr, ctx->size);
    ctx->addr = NULL;
  }
}

const char* ttr_reader_strerror(int errcode) {
  switch (errcode) {
    case TTR_READER_OK:
      return "Success";
    case TTR_READER_ERR_READ:
      return "Error opening mmap-area";
    case TTR_READER_ERR_MAGIC:
      return "Invalid magic number";
    case TTR_READER_ERR_VERSION:
      return "Unsupported version";
    case TTR_READER_ERR_NODATA:
      return "No data available";
    case TTR_READER_ERR_INVALID:
      return "Invalid parameters";
    case TTR_READER_ERR_STALE:
      return "Data is stale (daemon not running)";
    default:
      return "Unknown error";
  }
}
