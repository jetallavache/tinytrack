#include <stdlib.h>
#include <string.h>

#include "reduce.h"

/* struct ema_u16_ctx */

struct ema_u16_ctx {
  double ema;
  double alpha; /* 0..1, for example 0.3 */
  bool initialized;
};

struct tt_reduce_accumulator* tt_reduce_ema_u16_create(void) {
  struct ema_u16_ctx* acc = calloc(1, sizeof(struct ema_u16_ctx));
  acc->alpha = 0.3; /* default */
  return (struct tt_reduce_accumulator*)acc;
}

void tt_reduce_ema_u16_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out) {
  struct ema_u16_ctx* acc = (struct ema_u16_ctx*)acc_ptr;

  /* If this is the final challenge, just write the result and exit. */
  if (is_last) {
    if (out) {
      *(uint16_t*)out = (uint16_t)acc->ema + 0.5;
    }
    return;
  }

  if (!value)
    return;

  uint16_t val = *(const uint16_t*)value;

  if (!acc->initialized) {
    acc->ema = (double)val;
    acc->initialized = true;
  } else {
    /* Adaptive alpha based on count */
    if (index == 1) {
      acc->alpha = 2.0 / (double)(count + 1);
    }
    acc->ema = acc->alpha * (double)val + (1.0 - acc->alpha) * acc->ema;
  }

  if (is_last && out) {
    *(uint16_t*)out = (uint16_t)(acc->ema + 0.5);
  }
}

void tt_reduce_ema_u16_destroy(struct tt_reduce_accumulator* acc_ptr) {
  free(acc_ptr);
}

/* struct ema_u32_ctx */

struct ema_u32_ctx {
  double ema;
  double alpha; /* 0..1, for example 0.3 */
  bool initialized;
};

struct tt_reduce_accumulator* tt_reduce_ema_u32_create(void) {
  struct ema_u32_ctx* acc = calloc(1, sizeof(struct ema_u32_ctx));
  acc->alpha = 0.3; /* default */
  return (struct tt_reduce_accumulator*)acc;
}

void tt_reduce_ema_u32_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out) {
  struct ema_u32_ctx* acc = (struct ema_u32_ctx*)acc_ptr;

  /* If this is the final challenge, just write the result and exit. */
  if (is_last) {
    if (out) {
      *(uint32_t*)out = (uint32_t)acc->ema + 0.5;
    }
    return;
  }

  if (!value)
    return;

  uint32_t val = *(const uint32_t*)value;

  if (!acc->initialized) {
    acc->ema = (double)val;
    acc->initialized = true;
  } else {
    /* Adaptive alpha based on count */
    if (index == 1) {
      acc->alpha = 2.0 / (double)(count + 1);
    }
    acc->ema = acc->alpha * (double)val + (1.0 - acc->alpha) * acc->ema;
  }

  if (is_last && out) {
    *(uint32_t*)out = (uint32_t)(acc->ema + 0.5);
  }
}

void tt_reduce_ema_u32_destroy(struct tt_reduce_accumulator* acc_ptr) {
  free(acc_ptr);
}

/* struct ema_u64_ctx */

struct ema_u64_ctx {
  double ema;
  double alpha; /* 0..1, for example 0.3 */
  bool initialized;
};

struct tt_reduce_accumulator* tt_reduce_ema_u64_create(void) {
  struct ema_u64_ctx* acc = calloc(1, sizeof(struct ema_u64_ctx));
  acc->alpha = 0.3; /* default */
  return (struct tt_reduce_accumulator*)acc;
}

void tt_reduce_ema_u64_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out) {
  struct ema_u64_ctx* acc = (struct ema_u64_ctx*)acc_ptr;

  /* If this is the final challenge, just write the result and exit. */
  if (is_last) {
    if (out) {
      *(uint64_t*)out = (uint64_t)acc->ema + 0.5;
    }
    return;
  }

  if (!value)
    return;

  uint64_t val = *(const uint64_t*)value;

  if (!acc->initialized) {
    acc->ema = (double)val;
    acc->initialized = true;
  } else {
    /* Adaptive alpha based on count */
    if (index == 1) {
      acc->alpha = 2.0 / (double)(count + 1);
    }
    acc->ema = acc->alpha * (double)val + (1.0 - acc->alpha) * acc->ema;
  }

  if (is_last && out) {
    *(uint64_t*)out = (uint64_t)(acc->ema + 0.5);
  }
}

void tt_reduce_ema_u64_destroy(struct tt_reduce_accumulator* acc_ptr) {
  free(acc_ptr);
}