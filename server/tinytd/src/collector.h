#ifndef TTD_COLLECTOR_H
#define TTD_COLLECTOR_H

#include <errno.h>
#include <stdbool.h>
#include <time.h>

#include "common/sysfs.h"

#define TTD_STAT_BSIZE 100
#define TTD_MEMINFO_BSIZE 100
#define TTD_NET_BSIZE 1000
#define TTD_LOADAVG_BSIZE 30

/* Paths are now resolved at runtime via tt_sysfs_*() — see common/sysfs.h */

/* Structure of the /proc/stat data unit */
struct proc_stat {
  unsigned long user;
  unsigned long nice;
  unsigned long system;
  unsigned long idle;
  unsigned long iowait;
  unsigned long irq;
  unsigned long softirq;
  unsigned long steal;
};

/* Structure of the /proc/meminfo data unit */
struct proc_meminfo {
  unsigned long mem_total;
  unsigned long mem_free;
  unsigned long mem_available;

  unsigned long buffers;
  unsigned long cached;

  unsigned long active;
  unsigned long inactive;

  unsigned long swap_cached;
  unsigned long swap_total;
  unsigned long swap_free;

  unsigned long dirty;
  unsigned long writeback;

  unsigned long anon_pages;
  unsigned long mapped;
  unsigned long shmem;

  unsigned long slab;
  unsigned long s_reclaimable;
  unsigned long s_unreclaim;

  unsigned long page_tables;

  unsigned long kernel_stack;
};

/* Structure of the /proc/net data unit */
struct proc_net {
  unsigned long rx_bytes; /* Received bytes */
  unsigned long tx_bytes; /* Transmitted bytes */
};

/* Structure of the /proc/loadavg data unit */
struct proc_loadavg {
  float load_1min;
  float load_5min;
  float load_15min;
  int nr_running;
  int nr_total;
};

/* Structure of the direct_statvfs data unit */
struct ttd_collector_du {
  float usage;               /* Usage percentage */
  unsigned long total_bytes; /* Total space in bytes */
  unsigned long free_bytes;  /* Available space in bytes */
};

/* */
struct ttd_collector_cpu_pct {
  float user_pct;
  float nice_pct;
  float system_pct;
  float idle_pct;
  float iowait_pct;
  float irq_pct;
  float softirq_pct;
  float steal_pct;
  float total_pct;
};

/* State of collector node */
struct ttd_collector_state {
  struct proc_stat stat_prev;
  struct ttd_collector_cpu_pct p;

  struct proc_net net_prev;
  time_t net_time_prev;
  struct ttd_collector_du du_cached;
  time_t du_last_update;
  time_t du_inval;
};

float ttd_collect_cpu(struct ttd_collector_state* st);
float ttd_collect_memory(void);
void ttd_collect_net(struct ttd_collector_state* st, unsigned long* rx,
                     unsigned long* tx);
struct proc_loadavg ttd_collect_loadavg(void);
struct ttd_collector_du ttd_collect_disk(struct ttd_collector_state* st);

/* Initialize/cleanup persistent file descriptors */
void ttd_collector_init(void);
void ttd_collector_cleanup(void);

#endif /* TTD_COLLECTOR_H */