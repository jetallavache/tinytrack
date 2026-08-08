#include "mem.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/log/log_internal.h"

/**
 * @brief Set the state two bits
 * @note The state setting is always in the host byte order.
 * When transmitting over the network, the caller himself does htons().
 */
static inline void bit2_set(uint16_t* state, unsigned int shift,
                            uint8_t value) {
  uint16_t mask = (uint16_t)(0x3u << shift);
  *state = (*state & ~mask) | ((uint16_t)(value & 0x3) << shift);
}

static inline uint8_t bit2_get(uint16_t state, unsigned int shift) {
  return (uint8_t)((state >> shift) & 0x3u);
}

/* Set thresholds */
void thresholds_set(struct mem_state_thresholds* mt) {
  memset(mt, 0, sizeof(*mt));
  mt->avail_warning_pct = 20;
  mt->avail_critical_pct = 10;
  mt->active_warning_pct = 70;
  mt->active_critical_pct = 90;
  mt->swap_warning_pct = 30;
  mt->swap_critical_pct = 60;
  mt->dirty_warning_pct = 5;
  mt->dirty_critical_pct = 10;
  mt->kernel_warning_pct = 15;
  mt->kernel_critical_pct = 25;
  mt->swap_cached_warning_rate_kbps = 100;
  mt->swap_cached_critical_rate_kbps = 500;
  mt->s_unreclaim_warning_rate_kbps = 100;
  mt->s_unreclaim_critical_rate_kbps = 500;
}

static uint8_t rate(int64_t rate_kbps, int32_t warn, int32_t crit) {
  if (rate_kbps <= 0)
    return STATE_OK;
  if (rate_kbps < (int64_t)warn)
    return STATE_OK;
  if (rate_kbps < (int64_t)crit)
    return STATE_WARNING;
  return STATE_CRITICAL;
}

uint16_t ttd_mem_state(struct ttd_fetch* fch) {
  uint16_t state = 0;
  struct proc_meminfo pm = fch->pr_meminfo;
  struct ttd_trends* tr = fch->trends;
  unsigned long avail_pct, act_pct, swap_pct, anon_pct, dirty_pct, kern_pct;

  avail_pct = pm.mem_available * 100 / pm.mem_total;
  if (avail_pct > 20)
    bit2_set(&state, MEM_STATE_AVAILABILITY_SHIFT, STATE_OK);
  else if (avail_pct > 10)
    bit2_set(&state, MEM_STATE_AVAILABILITY_SHIFT, STATE_WARNING);
  else
    bit2_set(&state, MEM_STATE_AVAILABILITY_SHIFT, STATE_CRITICAL);

  unsigned long total_act = pm.active + pm.inactive;
  act_pct = total_act ? pm.active * 100 / total_act : 0;
  if (act_pct < 70)
    bit2_set(&state, MEM_STATE_PRESSURE_SHIFT, STATE_OK);
  else if (act_pct < 90)
    bit2_set(&state, MEM_STATE_PRESSURE_SHIFT, STATE_WARNING);
  else
    bit2_set(&state, MEM_STATE_PRESSURE_SHIFT, STATE_CRITICAL);

  if (pm.swap_total == 0) {
    bit2_set(&state, MEM_STATE_SWAP_SHIFT, STATE_OK);
  } else {
    swap_pct = (pm.swap_total - pm.swap_free) * 100 / pm.swap_total;
    if (swap_pct < 30)
      bit2_set(&state, MEM_STATE_SWAP_SHIFT, STATE_OK);
    else if (swap_pct < 60)
      bit2_set(&state, MEM_STATE_SWAP_SHIFT, STATE_WARNING);
    else
      bit2_set(&state, MEM_STATE_SWAP_SHIFT, STATE_CRITICAL);
  }

  anon_pct = pm.anon_pages * 100 / pm.mem_total;
  if (anon_pct < 50)
    bit2_set(&state, MEM_STATE_ANON_PAGES_SHIFT, STATE_OK);
  else if (anon_pct < 80)
    bit2_set(&state, MEM_STATE_ANON_PAGES_SHIFT, STATE_WARNING);
  else
    bit2_set(&state, MEM_STATE_ANON_PAGES_SHIFT, STATE_CRITICAL);

  dirty_pct = pm.dirty * 100 / pm.mem_total;
  if (pm.writeback == 0 && dirty_pct < 5)
    bit2_set(&state, MEM_STATE_DIRTY_IO_SHIFT, STATE_OK);
  else if (dirty_pct < 10)
    bit2_set(&state, MEM_STATE_DIRTY_IO_SHIFT, STATE_WARNING);
  else
    bit2_set(&state, MEM_STATE_DIRTY_IO_SHIFT, STATE_CRITICAL);

  kern_pct = (pm.slab + pm.kernel_stack + pm.page_tables) * 100 / pm.mem_total;
  if (kern_pct < 15)
    bit2_set(&state, MEM_STATE_KERNEL_SHIFT, STATE_OK);
  else if (kern_pct < 25)
    bit2_set(&state, MEM_STATE_KERNEL_SHIFT, STATE_WARNING);
  else
    bit2_set(&state, MEM_STATE_KERNEL_SHIFT, STATE_CRITICAL);

  tt_log_debug(
      "\n> avail=%llu, act=%llu, swap=%llu, anon=%llu, dirt=%llu, kern=%llu",
      avail_pct, act_pct, swap_pct, anon_pct, dirty_pct, kern_pct);

  uint8_t swap_trend = rate(tr->swap_cached_ema_rate, tr->warning_rate_kbps,
                            tr->critical_rate_kbps);
  uint8_t s_unreclaim_trend = rate(
      tr->s_unreclaim_ema_rate, tr->warning_rate_kbps, tr->critical_rate_kbps);

  bit2_set(&state, MEM_STATE_SWAP_CACHED_TREND_SHIFT, swap_trend);
  bit2_set(&state, MEM_STATE_S_UNRECLAIM_TREND_SHIFT, s_unreclaim_trend);

  tt_log_debug("\n> swap_cached=%llu, s_unreclaim=%llu", swap_trend,
               s_unreclaim_trend);

  return state;
}

/*
 * The recipient always results in a host byte order
 * Use htons() for wire_data
 */
void ttd_mem_state_fmt(const uint16_t* wire_data, char* buf, size_t len) {
  uint16_t state = *wire_data;

  uint8_t avail = bit2_get(state, MEM_STATE_AVAILABILITY_SHIFT);
  uint8_t press = bit2_get(state, MEM_STATE_PRESSURE_SHIFT);
  uint8_t swap = bit2_get(state, MEM_STATE_SWAP_SHIFT);
  uint8_t anon_pages = bit2_get(state, MEM_STATE_ANON_PAGES_SHIFT);
  uint8_t dirty_io = bit2_get(state, MEM_STATE_DIRTY_IO_SHIFT);
  uint8_t kernel_structs = bit2_get(state, MEM_STATE_KERNEL_SHIFT);
  uint8_t swap_cached = bit2_get(state, MEM_STATE_SWAP_CACHED_TREND_SHIFT);
  uint8_t s_unreclaim = bit2_get(state, MEM_STATE_S_UNRECLAIM_TREND_SHIFT);

  const char* labels[] = {"ok", "warn", "crit", "???"};
  snprintf(buf, len,
           "avail=%s, act=%s, swap=%s, anon=%s, dirt=%s, kern=%s, "
           "swap_cached=%s, s_unreclaim=%s",
           labels[avail], labels[press], labels[swap], labels[anon_pages],
           labels[dirty_io], labels[kernel_structs], labels[swap_cached],
           labels[s_unreclaim]);
}

/**
 * @private
 */
struct flags_u16_ctx {
  uint16_t flags;
};

struct tt_reduce_accumulator* ttd_mem_reduce_create(void) {
  return (struct tt_reduce_accumulator*)calloc(1, sizeof(struct flags_u16_ctx));
}

void ttd_mem_reduce_accumulate(struct tt_reduce_accumulator* acc_ptr,
                               const void* value, uint32_t count,
                               uint32_t index, bool is_last, void* out) {
  struct flags_u16_ctx* acc = (struct flags_u16_ctx*)acc_ptr;

  /* If this is the final challenge, just write the result and exit. */
  if (is_last) {
    if (out) {
      *(uint16_t*)out = acc->flags;
    }
    return;
  }

  if (!value)
    return;

  acc->flags |= *(const uint16_t*)value;

  if (is_last && out) {
    *(uint16_t*)out = acc->flags;
  }
}

void ttd_mem_reduce_destroy(struct tt_reduce_accumulator* acc_ptr) {
  free(acc_ptr);
}

/* struct flags_u32_ctx */

/* struct flags_u64_ctx */
