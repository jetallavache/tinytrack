#ifndef TT_REDUCE_H
#define TT_REDUCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Accumulator context for aggregating a single field.
 * Each aggregation function gets its own instance.
 */
typedef struct tt_reduce_accumulator tt_reduce_accumulator;

/**
 * A generalized aggregation function with state for any field of the structure.
 *
 * @param acc_ptr accumulator (created once before the cycle)
 * @param value pointer to the current value of the field
 * @param count iteration number (0-based) or total number
 * @param index
 * @param is_last flag of the last iteration
 * @param out where to write the final result (only if is_last=true)
 */
typedef void (*aggregate_fn)(struct tt_reduce_accumulator* acc_ptr,
                             const void* value, uint32_t count, uint32_t index,
                             bool is_last, void* out);

/**
 * A function for creating an accumulator (allocates memory or initializes)
 */
typedef struct tt_reduce_accumulator* (*create_fn)(void);

/**
 * A function for removing an accumulator
 *
 * @param acc_ptr accumulator
 */
typedef void (*destroy_fn)(struct tt_reduce_accumulator* acc_ptr);

/**
 * Description of the aggregation strategy for a single field
 */
struct tt_reduce_aggregator {
  const char* name;        /* For debug */
  size_t offset;           /* offsetof(struct tt_metrics, <field>) */
  size_t size;             /* sizeof(<field>) */
  create_fn create;        /* Create accumulator*/
  aggregate_fn accumulate; /* Aggregate value */
  destroy_fn destroy;      /* Remove accumulator */
};

/**
 * A collection of aggregation functions for different types of fields.
 * The ring buffer does not know which fields are in the structure,
 * but it knows how to aggregate numbers, flags, etc.
 */
struct tt_reduce_actions {
  const struct tt_reduce_aggregator* reducers; /* Array */
  uint32_t count;                              /* Count */
};

/* ema */
struct tt_reduce_accumulator* tt_reduce_ema_u16_create(void);
void tt_reduce_ema_u16_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out);
void tt_reduce_ema_u16_destroy(struct tt_reduce_accumulator* acc_ptr);
struct tt_reduce_accumulator* tt_reduce_ema_u32_create(void);
void tt_reduce_ema_u32_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out);
void tt_reduce_ema_u32_destroy(struct tt_reduce_accumulator* acc_ptr);
struct tt_reduce_accumulator* tt_reduce_ema_u64_create(void);
void tt_reduce_ema_u64_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out);
void tt_reduce_ema_u64_destroy(struct tt_reduce_accumulator* acc_ptr);

/* latest */
struct tt_reduce_accumulator* tt_reduce_latest_u64_create(void);
void tt_reduce_latest_u64_accumulate(struct tt_reduce_accumulator* acc_ptr,
                                     const void* value, uint32_t count,
                                     uint32_t index, bool is_last, void* out);
void tt_reduce_latest_u64_destroy(struct tt_reduce_accumulator* acc_ptr);

#endif /* TT_REDUCE_H */