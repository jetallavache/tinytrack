#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>

void ttd_cpu_state_fmt(const uint16_t* wire_data, char* buf, size_t len) {
  uint16_t state = *wire_data;
  snprintf(buf, len, "<still in dev>");
}

/**
 * @private
 */
struct flags_u16_ctx {
  uint16_t flags;
};

struct tt_reduce_accumulator* ttd_cpu_reduce_create(void) {
  return (struct tt_reduce_accumulator*)calloc(1, sizeof(struct flags_u16_ctx));
}

void ttd_cpu_reduce_accumulate(struct tt_reduce_accumulator* acc_ptr,
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

void ttd_cpu_reduce_destroy(struct tt_reduce_accumulator* acc_ptr) {
  free(acc_ptr);
}

/* struct flags_u32_ctx */

/* struct flags_u64_ctx */
