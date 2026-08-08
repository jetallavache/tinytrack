#ifndef TTD_SCHEDULER_H
#define TTD_SCHEDULER_H

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

struct ttd_scheduler {
    uint64_t next_sample;
    uint64_t next_l2;
    uint64_t next_l3;
    uint64_t next_shadow;
};

void ttd_scheduler_poll();

#endif /* TTD_SCHEDULER_H */