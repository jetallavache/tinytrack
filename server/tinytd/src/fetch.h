#ifndef TTD_FETCH_H
#define TTD_FETCH_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "common/sysfs.h"
#include "trends.h"

/**
 * Ответственность:
 * - `/proc`;
 * - `/sys`;
 * - `statvfs`;
 * - возможно netlink-derived observations;
 * - преобразование Linux данных → `tt_metrics`.
 */

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
  unsigned long mem_total; /* + */
  // unsigned long mem_free;
  unsigned long mem_available; /* + */
  // unsigned long buffers;
  // unsigned long cached;
  unsigned long active; /* + */
  unsigned long inactive;
  unsigned long swap_cached; /* + */
  unsigned long swap_total;  /* + */
  unsigned long swap_free;   /* + */
  unsigned long dirty;       /* + */
  unsigned long writeback;
  unsigned long anon_pages; /* + */
  // unsigned long mapped;
  // unsigned long shmem;
  unsigned long slab; /* + */
  // unsigned long s_reclaimable;
  unsigned long s_unreclaim;  /* + */
  unsigned long page_tables;  /* + */
  unsigned long kernel_stack; /* + */
};

/* Structure of the /proc/net data unit */
struct proc_net_dev {
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

struct proc_vmstat {
  int oom_kill;
};

/* The calculated percentage for the values in /proc/stat. */
struct proc_stat_pct {
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

struct proc_sys_fs_filenr {
  unsigned long allocated;
  unsigned long unused;
  unsigned long max;
};

/* Structure of the direct_statvfs data unit */
struct du_stat {
  float usage;               /* Usage percentage */
  unsigned long total_bytes; /* Total space in bytes */
  unsigned long free_bytes;  /* Available space in bytes */
  float inodes_usage;
};

/* State of collector node */
struct ttd_fetch_state {
  struct proc_stat pr_stat_prev;
  struct proc_stat_pct pr_stat_pct;
  struct proc_net_dev pr_net_prev;
  time_t net_time_prev;
  struct du_stat du_cached;
  time_t du_last_update;
  time_t du_inval;
};

struct ttd_fetch {
  struct proc_stat pr_stat;
  struct proc_meminfo pr_meminfo;
  struct proc_net_dev pr_net;
  struct proc_loadavg pr_loadavg;
  struct proc_vmstat pr_vmstat;
  struct proc_sys_fs_filenr pr_fs;
  struct du_stat du;

  struct ttd_fetch_state* state;
  struct ttd_trends* trends;
};

int ttd_fetch_cpu(struct ttd_fetch* fch);
int ttd_fetch_memory(struct ttd_fetch* fch);
int ttd_fetch_net(struct ttd_fetch* fch);
int ttd_fetch_loadavg(struct ttd_fetch* fch);
int ttd_fetch_disk(struct ttd_fetch* fch);

/*...*/
int ttd_fetch_oom_kills(struct ttd_fetch* fch);
int ttd_fetch_fs(struct ttd_fetch* fch);

/* Initialize/cleanup persistent file descriptors */
void ttd_fetch_init(struct ttd_fetch* fch);
void ttd_fetch_cleanup(void);

#endif /* TTD_FETCH_H */