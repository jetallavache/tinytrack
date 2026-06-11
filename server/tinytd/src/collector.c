#include "collector.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define NEXT_LINE(ptr)             \
  while (*(ptr) && *(ptr) != '\n') \
    (ptr)++;                       \
  if (*(ptr))                      \
    (ptr)++;

/* Persistent file descriptors for /proc files.
 * NOTE: not thread-safe; only one collector instance per process is supported.
 */
static int g_stat_fd = -1;
static int g_meminfo_fd = -1;
static int g_loadavg_fd = -1;
static int g_net_fd = -1;

void ttd_collector_init(void) {
  g_stat_fd = open(tt_sysfs_stat(), O_RDONLY | O_CLOEXEC);
  g_meminfo_fd = open(tt_sysfs_meminfo(), O_RDONLY | O_CLOEXEC);
  g_loadavg_fd = open(tt_sysfs_loadavg(), O_RDONLY | O_CLOEXEC);
  g_net_fd = open(tt_sysfs_net_dev(), O_RDONLY | O_CLOEXEC);
}

void ttd_collector_cleanup(void) {
  if (g_stat_fd >= 0)
    close(g_stat_fd);
  if (g_meminfo_fd >= 0)
    close(g_meminfo_fd);
  if (g_loadavg_fd >= 0)
    close(g_loadavg_fd);
  if (g_net_fd >= 0)
    close(g_net_fd);
}

/* Use the libc statvfs() wrapper. A raw syscall(SYS_statvfs) would write
 * struct statfs (120 bytes) into a struct statvfs buffer (112 bytes),
 * overflowing 8 bytes and corrupting the stack. */
static inline int direct_statvfs(const char* path, struct statvfs* buf) {
  return statvfs(path, buf);
}

bool readpr_stat(struct proc_stat* ps) {
  if (g_stat_fd < 0)
    return false;

  char buf[512];
  lseek(g_stat_fd, 0, SEEK_SET);
  ssize_t n = read(g_stat_fd, buf, sizeof(buf) - 1);
  if (n <= 0)
    return false;
  buf[n] = '\0';

  int parsed = sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu %lu", &ps->user,
                      &ps->nice, &ps->system, &ps->idle, &ps->iowait, &ps->irq,
                      &ps->softirq, &ps->steal);
  return (parsed == 8);
}

bool readpr_meminf(struct proc_meminfo* pm) {
  if (g_meminfo_fd < 0)
    return false;

  char buf[2048];
  lseek(g_meminfo_fd, 0, SEEK_SET);
  ssize_t n = read(g_meminfo_fd, buf, sizeof(buf) - 1);
  if (n <= 0)
    return false;
  buf[n] = '\0';

  int parsed = 0;
  char* p = buf;
  char* end = buf + n;

  while (p < end && parsed < 20) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
      p++;
    if (p >= end) break;

    unsigned int hash = 0;
    unsigned int key_len = 0;
    char* key_start = p;

    while (p < end && *p != ':') {
      hash = hash * 31 + *p;
      key_len++;
      p++;
    }

    if (p >= end || *p != ':') break;
    p++;

    while (p < end && (*p == ' ' || *p == '\t'))
      p++;
    if (p >= end) break;

    unsigned long value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      p++;
    }

    while (p < end && *p != '\n')
      p++;
    if (p < end) p++;

    switch (hash) {
      case 3697542799:
        if (key_len == 8)
          pm->mem_total = value, parsed++;
        break;
      case 2612712897:
        if (key_len == 7)
          pm->mem_free = value, parsed++;
        break;
      case 1570904212:
        if (key_len == 12)
          pm->mem_available = value, parsed++;
        break;
      case 1892650003:
        if (key_len == 7)
          pm->buffers = value, parsed++;
        break;
      case 2010787138:
        if (key_len == 6)
          pm->cached = value, parsed++;
        break;
      case 411394229:
        if (key_len == 10)
          pm->swap_cached = value, parsed++;
        break;
      case 1955883814:
        if (key_len == 6)
          pm->active = value, parsed++;
        break;
      case 89309323:
        if (key_len == 8)
          pm->inactive = value, parsed++;
        break;
      case 722140497:
        if (key_len == 9)
          pm->swap_total = value, parsed++;
        break;
      case 4040752831:
        if (key_len == 8)
          pm->swap_free = value, parsed++;
        break;
      case 66040754:
        if (key_len == 5)
          pm->dirty = value, parsed++;
        break;
      case 598045990:
        if (key_len == 9)
          pm->writeback = value, parsed++;
        break;
      case 4164893752:
        if (key_len == 9)
          pm->anon_pages = value, parsed++;
        break;
      case 2297473619:
        if (key_len == 6)
          pm->mapped = value, parsed++;
        break;
      case 79858496:
        if (key_len == 5)
          pm->shmem = value, parsed++;
        break;
      case 2579546:
        if (key_len == 4)
          pm->slab = value, parsed++;
        break;
      case 2128625776:
        if (key_len == 12)
          pm->s_reclaimable = value, parsed++;
        break;
      case 1372541469:
        if (key_len == 10)
          pm->s_unreclaim = value, parsed++;
        break;
      case 1159547947:
        if (key_len == 11)
          pm->kernel_stack = value, parsed++;
        break;
      case 3291218420:
        if (key_len == 10)
          pm->page_tables = value, parsed++;
        break;
    }
  }

  return (parsed == 20);
}


bool readpr_net(struct proc_net* pn) {
  if (g_net_fd < 0)
    return false;

  char buf[4096];
  lseek(g_net_fd, 0, SEEK_SET);
  ssize_t n = read(g_net_fd, buf, sizeof(buf) - 1);
  if (n <= 0)
    return false;
  buf[n] = '\0';

  pn->rx_bytes = 0;
  pn->tx_bytes = 0;

  char* line = buf;
  char* end = buf + n;

  /* Skip header lines */
  NEXT_LINE(line);
  NEXT_LINE(line);

  while (line < end && *line) {
    char iface[32];
    unsigned long rx, tx;
    unsigned long dummy[14];

    if (sscanf(line,
               "%31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
               "%lu %lu %lu",
               iface, &rx, &dummy[0], &dummy[1], &dummy[2], &dummy[3],
               &dummy[4], &dummy[5], &dummy[6], &tx, &dummy[7], &dummy[8],
               &dummy[9], &dummy[10], &dummy[11], &dummy[12],
               &dummy[13]) >= 10) {
      if (strcmp(iface, "lo") != 0) {
        pn->rx_bytes += rx;
        pn->tx_bytes += tx;
      }
    }
    NEXT_LINE(line);
  }
  return true;
}

bool readpr_loadavg(struct proc_loadavg* pl) {
  if (g_loadavg_fd < 0)
    return false;

  char buf[128];
  lseek(g_loadavg_fd, 0, SEEK_SET);
  ssize_t n = read(g_loadavg_fd, buf, sizeof(buf) - 1);
  if (n <= 0)
    return false;
  buf[n] = '\0';

  int parsed = sscanf(buf, "%f %f %f %d/%d", &pl->load_1min, &pl->load_5min,
                      &pl->load_15min, &pl->nr_running, &pl->nr_total);
  return (parsed == 5);
}

float ttd_collect_cpu(struct ttd_collector_state* st) {
  struct proc_stat curr;
  if (!readpr_stat(&curr))
    return 0.0f;

  unsigned long prev_total = st->stat_prev.user + st->stat_prev.nice +
                             st->stat_prev.system + st->stat_prev.idle +
                             st->stat_prev.iowait + st->stat_prev.irq +
                             st->stat_prev.softirq + st->stat_prev.steal;

  unsigned long curr_total = curr.user + curr.nice + curr.system + curr.idle +
                             curr.iowait + curr.irq + curr.softirq + curr.steal;

  unsigned long total_diff = curr_total - prev_total;

  if (total_diff == 0) {
    return 0.0f;
  } else {
    st->p.total_pct =
        (total_diff - ((curr.idle + curr.iowait) -
                       (st->stat_prev.idle + st->stat_prev.iowait))) *
        100.0f / total_diff;
    st->p.user_pct = (curr.user - st->stat_prev.user) * 100.0f / total_diff;
    st->p.nice_pct = (curr.nice - st->stat_prev.nice) * 100.0f / total_diff;
    st->p.system_pct =
        (curr.system - st->stat_prev.system) * 100.0f / total_diff;
    st->p.idle_pct = (curr.idle - st->stat_prev.idle) * 100.0f / total_diff;
    st->p.iowait_pct =
        (curr.iowait - st->stat_prev.iowait) * 100.0f / total_diff;
    st->p.irq_pct = (curr.irq - st->stat_prev.irq) * 100.0f / total_diff;
    st->p.softirq_pct =
        (curr.softirq - st->stat_prev.softirq) * 100.0f / total_diff;
    st->p.steal_pct = (curr.steal - st->stat_prev.steal) * 100.0f / total_diff;
  }

  st->stat_prev = curr;

  return st->p.total_pct;
}

float ttd_collect_memory(void) {
  struct proc_meminfo mem;
  if (!readpr_meminf(&mem))
    return 0.0f;

  if (mem.mem_total == 0)
    return 0.0f;

  return (float)(mem.mem_total - mem.mem_available) * 100.0f / mem.mem_total;
}

void ttd_collect_net(struct ttd_collector_state* st, unsigned long* rx,
                     unsigned long* tx) {
  struct proc_net curr;
  if (!readpr_net(&curr)) {
    *rx = *tx = 0;
    return;
  }

  /**
   * Calculate rate (per 1 sec!) of change rx/tx,
   * but we just need to get the raw data and store it in this form in the tsdb.
   */
  time_t now = time(NULL);
  time_t elapsed = now - st->net_time_prev;

  if (elapsed > 0 && st->net_time_prev > 0) {
    if (curr.rx_bytes >= st->net_prev.rx_bytes)
      *rx = (curr.rx_bytes - st->net_prev.rx_bytes) / elapsed;
    else
      *rx = 0; /* counter reset / wrap */
    if (curr.tx_bytes >= st->net_prev.tx_bytes)
      *tx = (curr.tx_bytes - st->net_prev.tx_bytes) / elapsed;
    else
      *tx = 0;
  } else {
    *rx = *tx = 0;
  }

  st->net_prev = curr;
  st->net_time_prev = now;
}

struct proc_loadavg ttd_collect_loadavg(void) {
  struct proc_loadavg load = {0};
  readpr_loadavg(&load);
  return load;
}

struct ttd_collector_du ttd_collect_disk(struct ttd_collector_state* st) {
  struct ttd_collector_du result = {0};

  time_t now = time(NULL);
  if (st->du_last_update > 0 && (now - st->du_last_update) < st->du_inval) {
    return st->du_cached;
  }

  struct statvfs vfs;
  if (direct_statvfs(tt_sysfs_rootfs(""), &vfs) != 0) {
    return result;
  }

  unsigned long total = vfs.f_blocks * vfs.f_frsize;
  unsigned long free = vfs.f_bavail * vfs.f_frsize;

  result.total_bytes = total;
  result.free_bytes = free;
  result.usage = (total > 0) ? (float)(total - free) * 100.0f / total : 0.0f;

  st->du_cached = result;
  st->du_last_update = now;

  return result;
}
