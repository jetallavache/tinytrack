#ifndef TTD_SCHEDULER_H
#define TTD_SCHEDULER_H

#include <stdint.h>

/**
 * scheduler
 *
 * Что и когда нужно выполнить?
 *
 * sampling        1 sec
 * L2 aggregation  1 min
 * L3 aggregation  1 hour
 * shadow sync     10 sec
 */

struct ttd_interval {
    uint32_t sample_ms;
    uint32_t l2_ms;
};

enum ttd_job {
    TTD_JOB_NONE,
    TTD_JOB_SAMPLE,
    TTD_JOB_L2,
    TTD_JOB_L3,
    TTD_JOB_SHADOW,
};

struct ttd_scheduler {
    uint64_t next_sample;
    uint64_t next_l2;
    uint64_t next_l3;
    uint64_t next_shadow;
};

// void ttd_scheduler_init(struct ttd_scheduler *s,
//                     struct ttd_interval interval);
void ttd_scheduler_poll();

#endif /* TTD_SCHEDULER_H */