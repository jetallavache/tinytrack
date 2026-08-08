#include <stdlib.h>
#include <string.h>

#include "reduce.h"

/* struct latest_u16_ctx */

/* struct latest_u32_ctx */

struct latest_u64_ctx {
  uint64_t last_value;
  bool has_value;
};

struct tt_reduce_accumulator* tt_reduce_latest_u64_create(void) {
  return (struct tt_reduce_accumulator*)calloc(1,
                                               sizeof(struct latest_u64_ctx));
}

void tt_reduce_latest_u64_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                     const void* value, uint32_t count,
                                     uint32_t index, bool is_last, void* out) {
  struct latest_u64_ctx* acc = (struct latest_u64_ctx*)acc_ptr;

  /* If this is the final challenge, just write the result and exit. */
  if (is_last) {
    if (out) {
      *(uint64_t*)out = acc->last_value;
    }
    return;
  }

  if (!value)
    return;

  uint64_t val = *(const uint64_t*)value;

  /* We take the maximum timestamp (latest in time) */
  if (!acc->has_value || val > acc->last_value) {
    acc->last_value = val;
    acc->has_value = true;
  }

  if (is_last && out) {
    *(uint64_t*)out = acc->last_value;
  }
}

void tt_reduce_latest_u64_destroy(struct tt_reduce_accumulator* acc_ptr) {
  free(acc_ptr);
}
