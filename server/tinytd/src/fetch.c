#include "fetch.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
static int g_vmstat_fd = -1;

void ttd_fetch_init(struct ttd_fetch* fch) {
  g_stat_fd = open(tt_sysfs_stat(), O_RDONLY | O_CLOEXEC);
  g_meminfo_fd = open(tt_sysfs_meminfo(), O_RDONLY | O_CLOEXEC);
  g_loadavg_fd = open(tt_sysfs_loadavg(), O_RDONLY | O_CLOEXEC);
  g_net_fd = open(tt_sysfs_net_dev(), O_RDONLY | O_CLOEXEC);
  g_vmstat_fd = open(tt_sysfs_vmstat(), O_RDONLY | O_CLOEXEC);

  memset(fch->state, 0, sizeof(*fch->state));
  memset(fch->trends, 0, sizeof(*fch->trends));
  fch->trends->history_idx = 0;
  fch->trends->warning_rate_kbps = 100;
  fch->trends->critical_rate_kbps = 500;
}

void ttd_fetch_cleanup(void) {
  if (g_stat_fd >= 0)
    close(g_stat_fd);
  if (g_meminfo_fd >= 0)
    close(g_meminfo_fd);
  if (g_loadavg_fd >= 0)
    close(g_loadavg_fd);
  if (g_net_fd >= 0)
    close(g_net_fd);
  if (g_vmstat_fd >= 0)
    close(g_vmstat_fd);
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
    if (p >= end)
      break;

    unsigned int hash = 0;
    unsigned int key_len = 0;
    char* key_start = p;

    while (p < end && *p != ':') {
      hash = hash * 31 + *p;
      key_len++;
      p++;
    }

    if (p >= end || *p != ':')
      break;
    p++;

    while (p < end && (*p == ' ' || *p == '\t'))
      p++;
    if (p >= end)
      break;

    unsigned long value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      p++;
    }

    while (p < end && *p != '\n')
      p++;
    if (p < end)
      p++;

    switch (hash) {
      case 3697542799:
        if (key_len == 8)
          pm->mem_total = value, parsed++;
        break;
      // case 2612712897:
      //   if (key_len == 7)
      //     pm->mem_free = value, parsed++;
      //   break;
      case 1570904212:
        if (key_len == 12)
          pm->mem_available = value, parsed++;
        break;
      // case 1892650003:
      //   if (key_len == 7)
      //     pm->buffers = value, parsed++;
      //   break;
      // case 2010787138:
      //   if (key_len == 6)
      //     pm->cached = value, parsed++;
      //   break;
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
      // case 2297473619:
      //   if (key_len == 6)
      //     pm->mapped = value, parsed++;
      //   break;
      // case 79858496:
      //   if (key_len == 5)
      //     pm->shmem = value, parsed++;
      //   break;
      case 2579546:
        if (key_len == 4)
          pm->slab = value, parsed++;
        break;
      // case 2128625776:
      //   if (key_len == 12)
      //     pm->s_reclaimable = value, parsed++;
      //   break;
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

  return (parsed == 14);
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

int ttd_fetch_cpu(struct ttd_fetch* fch) {
  if (!readpr_stat(&fch->pr_stat))
    return -1;

  struct proc_stat curr = fch->pr_stat;
  struct proc_stat prev = fch->state->pr_stat_prev;

  unsigned long prev_total = prev.user + prev.nice + prev.system + prev.idle +
                             prev.iowait + prev.irq + prev.softirq + prev.steal;

  unsigned long curr_total = curr.user + curr.nice + curr.system + curr.idle +
                             curr.iowait + curr.irq + curr.softirq + curr.steal;

  unsigned long total_diff = curr_total - prev_total;

  if (total_diff == 0) {
    memset(&fch->state->pr_stat_pct, 0, sizeof(fch->state->pr_stat_pct));
    return 0;
  } else {
    fch->state->pr_stat_pct.total_pct =
        (total_diff - ((curr.idle + curr.iowait) - (prev.idle + prev.iowait))) *
        100.0f / total_diff;
    fch->state->pr_stat_pct.user_pct =
        (curr.user - prev.user) * 100.0f / total_diff;
    fch->state->pr_stat_pct.nice_pct =
        (curr.nice - prev.nice) * 100.0f / total_diff;
    fch->state->pr_stat_pct.system_pct =
        (curr.system - prev.system) * 100.0f / total_diff;
    fch->state->pr_stat_pct.idle_pct =
        (curr.idle - prev.idle) * 100.0f / total_diff;
    fch->state->pr_stat_pct.iowait_pct =
        (curr.iowait - prev.iowait) * 100.0f / total_diff;
    fch->state->pr_stat_pct.irq_pct =
        (curr.irq - prev.irq) * 100.0f / total_diff;
    fch->state->pr_stat_pct.softirq_pct =
        (curr.softirq - prev.softirq) * 100.0f / total_diff;
    fch->state->pr_stat_pct.steal_pct =
        (curr.steal - prev.steal) * 100.0f / total_diff;
  }

  fch->state->pr_stat_prev = curr;

  return 0;
}

int ttd_fetch_memory(struct ttd_fetch* fch) {
  if (!readpr_meminf(&fch->pr_meminfo))
    return -1;

  time_t now = time(NULL);
  ttd_trends_memory(fch->trends, fch->pr_meminfo.swap_cached,
                    fch->pr_meminfo.s_unreclaim, now);

  return 0;
}

int ttd_fetch_net(struct ttd_fetch* fch) {
  if (!readpr_net(&fch->pr_net))
    return -1;

  return 0;
}

int ttd_fetch_loadavg(struct ttd_fetch* fch) {
  if (!readpr_loadavg(&fch->pr_loadavg))
    return -1;

  return 0;
}

int ttd_fetch_disk(struct ttd_fetch* fch) {
  time_t now = time(NULL);
  if (fch->state->du_last_update > 0 &&
      (now - fch->state->du_last_update) < fch->state->du_inval) {
    return 0;
  }

  struct statvfs vfs;
  if (direct_statvfs(tt_sysfs_rootfs(""), &vfs) != 0) {
    return -1;
  }

  unsigned long total = vfs.f_blocks * vfs.f_frsize;
  unsigned long free = vfs.f_bavail * vfs.f_frsize;

  fch->du.total_bytes = total;
  fch->du.free_bytes = free;
  fch->du.usage = (total > 0) ? (float)(total - free) * 100.0f / total : 0.0f;

  fch->state->du_cached = fch->du;
  fch->state->du_last_update = now;

  return 0;
}

bool readpr_vmstat(struct proc_vmstat* pvs) {
  if (g_vmstat_fd < 0)
    return false;

  lseek(g_vmstat_fd, 0, SEEK_SET);

  char buf[512];
  ssize_t n = read(g_vmstat_fd, buf, sizeof(buf) - 1);
  if (n <= 0)
    return false;

  buf[n] = '\0';

  char* p = strstr(buf, "\noom_kill ");
  if (!p && strncmp(buf, "oom_kill ", 9) == 0)
    p = buf;  // строка в самом начале

  if (p) {
    pvs->oom_kill = (uint16_t)atoi(p + 9);
    return true;
  }

  return false;
}

int ttd_fetch_oom_kills(struct ttd_fetch* fch) {
  if (!readpr_vmstat(&fch->pr_vmstat))
    return -1;

  return 0;
}

/* ... */

// // Сбор информации о главном потребителе CPU
// static void collect_top_cpu_process(struct tt_metrics* m) {
//   DIR* proc = opendir("/proc");
//   if (!proc)
//     return;

//   int max_cpu_pid = 0;
//   unsigned long max_cpu_ticks = 0;

//   struct dirent* entry;
//   while ((entry = readdir(proc)) != NULL) {
//     if (!isdigit(entry->d_name[0]))
//       continue;

//     int pid = atoi(entry->d_name);
//     char stat_path[64];
//     snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

//     FILE* f = fopen(stat_path, "r");
//     if (!f)
//       continue;

//     // Формат /proc/[pid]/stat: pid comm state ... utime stime cutime cstime
//     char comm[256];
//     char state;
//     unsigned long utime, stime;
//     long cutime, cstime;

//     int scanned = fscanf(
//         f, "%d %s %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %ld
//         %ld", &pid, comm, &state, &utime, &stime, &cutime, &cstime);
//     fclose(f);

//     if (scanned < 7)
//       continue;

//     unsigned long total_ticks = utime + stime + cutime + cstime;
//     if (total_ticks > max_cpu_ticks) {
//       max_cpu_ticks = total_ticks;
//       max_cpu_pid = pid;
//     }
//   }
//   closedir(proc);

//   m->top_consumer_pid = max_cpu_pid;
//   m->top_consumer_pct = 0;  // нужно считать относительно предыдущего замера
// }

// // Сбор информации о главном потребителе RAM
// static void collect_top_ram_process(struct tt_metrics* m) {
//   DIR* proc = opendir("/proc");
//   if (!proc)
//     return;

//   int max_rss_pid = 0;
//   unsigned long max_rss_kb = 0;

//   struct dirent* entry;
//   while ((entry = readdir(proc)) != NULL) {
//     if (!isdigit(entry->d_name[0]))
//       continue;

//     int pid = atoi(entry->d_name);
//     char statm_path[64];
//     snprintf(statm_path, sizeof(statm_path), "/proc/%d/statm", pid);

//     FILE* f = fopen(statm_path, "r");
//     if (!f)
//       continue;

//     // Формат: size resident shared text lib data dt
//     unsigned long size, resident;
//     if (fscanf(f, "%lu %lu", &size, &resident) == 2) {
//       // resident — это RSS в страницах (обычно 4KB)
//       unsigned long rss_kb = resident * 4;
//       if (rss_kb > max_rss_kb) {
//         max_rss_kb = rss_kb;
//         max_rss_pid = pid;
//       }
//     }
//     fclose(f);
//   }
//   closedir(proc);

//   m->top_rss_pid = max_rss_pid;
//   m->top_rss_mb = max_rss_kb / 1024;
// }

// // Сбор процессов в D-state и Z-state
// static void collect_process_states(struct tt_metrics* m) {
//   DIR* proc = opendir("/proc");
//   if (!proc)
//     return;

//   uint16_t blocked = 0, zombie = 0;

//   struct dirent* entry;
//   while ((entry = readdir(proc)) != NULL) {
//     if (!isdigit(entry->d_name[0]))
//       continue;

//     char stat_path[64];
//     snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", entry->d_name);

//     FILE* f = fopen(stat_path, "r");
//     if (!f)
//       continue;

//     // Третий символ в stat — состояние процесса
//     char state;
//     if (fscanf(f, "%*d %*s %c", &state) == 1) {
//       if (state == 'D')
//         blocked++;
//       if (state == 'Z')
//         zombie++;
//     }
//     fclose(f);
//   }
//   closedir(proc);

//   m->procs_blocked = blocked;
//   m->procs_zombie = zombie;
// }

// // Сбор file descriptor usage
// static void collect_fd_usage(struct tt_metrics* m) {
//   FILE* f = fopen("/proc/sys/fs/file-nr", "r");
//   if (!f)
//     return;

//   unsigned long allocated, unused, max;
//   if (fscanf(f, "%lu %lu %lu", &allocated, &unused, &max) == 3) {
//     if (max > 0) {
//       m->fd_usage_pct = (uint16_t)((allocated * 10000) / max);
//     }
//   }
//   fclose(f);
// }

// // Сбор inode usage
// static void collect_inode_usage(struct tt_metrics* m) {
//   struct du_stat buf;
//   if (statvfs("/", &buf) == 0) {
//     if (buf.f_files > 0) {
//       uint64_t used = buf.f_files - buf.f_ffree;
//       m->inode_usage_pct = (uint16_t)((used * 10000) / buf.f_files);
//     }
//   }
// }
