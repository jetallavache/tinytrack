#ifndef TTD_CPU_H
#define TTD_CPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ttd_cpu_state_fmt(const uint16_t* wire_data, char* buf, size_t len);

struct tt_reduce_accumulator* ttd_cpu_reduce_create(void);
void ttd_cpu_reduce_accumulate(struct tt_reduce_accumulator* acc_ptr,
                               const void* value, uint32_t count,
                               uint32_t index, bool is_last, void* out);
void ttd_cpu_reduce_destroy(struct tt_reduce_accumulator* acc_ptr);

#endif /* TTD_CPU_H */