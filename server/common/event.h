#ifndef TT_EVENT_H
#define TT_EVENT_H

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "cell.h"

#define TT_EVENT_CELL_VERSION 1

/**
 * @brief
 *
 * @note
 */
struct tt_event {
  alignas(8) struct tt_cell_header hdr;
  alignas(8) uint64_t timestamp; /*  ms since epoch */

  /* What happened */
  alignas(2) uint16_t event_code; /* Event type (see enum below) */
  alignas(1) uint8_t severity;    /* 0=info, 1=warning, 2=critical */
  alignas(1) uint8_t component;   /* CPU=0, MEM=1, DISK=2, NET=3, PROC=4 */

  /* Context */
  alignas(4) uint32_t prev_state;   /* Bit mask of previous flags */
  alignas(4) uint32_t new_state;    /* Bit mask of new flags */
  alignas(2) uint16_t metric_value; /* Metric value at the time of the event */
  alignas(2) uint16_t threshold;    /* Threshold that was exceeded */

  /* Additional information */
  alignas(4) int32_t process_pid;     /* PID if related to a process */
  alignas(4) uint32_t duration_ms;    /* Duration of the anomaly (if known) */
  alignas(8) uint64_t correlation_id; /* For linking cascading events */
  alignas(4) uint32_t event_id;       /* Unique event ID */

  alignas(1) uint8_t reserved[76];
}; /* Total 128 bytes */

_Static_assert(sizeof(struct tt_event) == 128,
               "tt_event size must be 64 bytes");
_Static_assert(alignof(struct tt_event) == 8, "tt_event alignment must be 8");

enum tt_event_type {
  /* Memory events (0x01xx) */
  TT_EVENT_MEM_OOM_KILLED = 0x0101,     /* Process killed by OOM killer */
  TT_EVENT_MEM_SWAP_THRASHING = 0x0102, /* Swap thrashing has started */
  TT_EVENT_MEM_LEAK_DETECTED = 0x0103,  /* Memory leak has been detected */
  TT_EVENT_MEM_PRESSURE_HIGH = 0x0104,  /* High memory pressure */
  TT_EVENT_MEM_KERNEL_LEAK = 0x0105, /* Kernel leak (SUnreclaim is growing) */
  TT_EVENT_MEM_DIRTY_THROTTLE = 0x0106, /* Disk write is slow */

  /* CPU events (0x02xx) */
  TT_EVENT_CPU_THROTTLING = 0x0201,   /* CPU throttling */
  TT_EVENT_CPU_STEAL_HIGH = 0x0202,   /* High steal time */
  TT_EVENT_CPU_IO_WAIT_HIGH = 0x0203, /* High iowait */
  TT_EVENT_CPU_OVERLOAD = 0x0204,     /* CPU overload */

  /* State transition events (0x10xx) */
  TT_EVENT_STATE_CHANGE = 0x1000,     /* Component state change */
  TT_EVENT_THRESHOLD_CROSS = 0x1001,  /* Threshold crossing */
  TT_EVENT_ANOMALY_DETECTED = 0x1002, /* Anomaly detected */
};

enum tt_event_component {
  COMPONENT_CPU = 0,
  COMPONENT_MEM = 1,
};

enum tt_event_severity { TT_EVENT_INFO, TT_EVENT_WARNING, TT_EVENT_CRITICAL };

// void tt_event_ex_serialize(const struct tt_event* m, uint8_t* buf);
// void tt_event_ex_deserialize(const uint8_t* buf, struct tt_event* m);

void tt_event_emit(uint16_t event_code, uint8_t component, uint16_t val);

#endif /* TT_EVENT_H */
