#include "reader.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "common/log/log.h"
#include "common/ringbuf.h"
#include "common/ringbuf/layout.h"
#include "common/sysfs.h"

int ttg_reader_open(struct ttg_reader* ctx, const char* path) {
  return ttr_reader_open(&ctx->ring, path);
}

int ttg_reader_get_latest(struct ttg_reader* ctx, struct tt_metrics* out) {
  return ttr_reader_get_latest(&ctx->ring, out, sizeof(*out));
}

int ttg_reader_get_history(struct ttg_reader* ctx, uint8_t level,
                           struct tt_metrics* out, int count) {
  /* ttr_reader_get_history uses int level: 1/2/3 matching RING_LEVEL_* */
  return ttr_reader_get_history(&ctx->ring, (int)level, out,
                                sizeof(struct tt_metrics), count);
}

int ttg_reader_get_stats(struct ttg_reader* ctx, uint8_t level,
                         struct tt_proto_ring_stat* out) {
  struct ttr_meta* meta;
  switch (level) {
    case RING_LEVEL_L1:
      meta = ctx->ring.l1_meta;
      break;
    case RING_LEVEL_L2:
      meta = ctx->ring.l2_meta;
      break;
    case RING_LEVEL_L3:
      meta = ctx->ring.l3_meta;
      break;
    default:
      return -1;
  }
  if (!meta)
    return -1;

  out->level = level;
  out->capacity = htonl(meta->capacity);
  out->head = htonl(meta->head);
  out->filled =
      htonl(meta->head < meta->capacity ? meta->head : meta->capacity);
  out->first_ts = meta->first_ts; /* already ms, no hton needed for uint64 —
                                     handled by caller */
  out->last_ts = meta->last_ts;
  return 0;
}

void ttg_reader_close(struct ttg_reader* ctx) {
  ttr_reader_close(&ctx->ring);
}

/**
 * Check if tinytd is alive by inspecting the mmap header.
 *
 * Strategy:
 *   1. Check writer_pid — if non-zero, verify the process exists via
 * kill(pid,0)
 *   2. Check last_update_ts — if older than 3 * interval_ms, daemon is stale
 *
 * @return 0  — daemon is alive and writing
 * @return -1  — mmap exists but daemon appears dead (stale or pid gone)
 */
int ttg_reader_check_liveness(struct ttg_reader* ctx) {
  const struct ttr_header* hdr = (const struct ttr_header*)ctx->ring.addr;
  if (!hdr)
    return -1;

  /* Check writer PID */
  if (hdr->writer_pid > 0) {
    if (kill((pid_t)hdr->writer_pid, 0) == 0) {
      tt_log_debug("liveness: tinytd pid=%u alive", hdr->writer_pid);
      return 0; /* process exists */
    }
    tt_log_warning("Daemon     tinytd pid=%u not found — mmap may be stale",
                   hdr->writer_pid);
    tt_log_warning(
        "           See https://tinytrack.dev/docs/troubleshooting#stale-mmap");
  }

  /* Fallback: check last_update_ts freshness */
  if (hdr->last_update_ts > 0) {
    uint64_t now_ms = (uint64_t)time(NULL) * 1000;
    uint32_t interval = hdr->interval_ms > 0 ? hdr->interval_ms : 1000;
    uint64_t age_ms =
        now_ms > hdr->last_update_ts ? now_ms - hdr->last_update_ts : 0;
    uint64_t threshold_ms = (uint64_t)interval * 3;

    if (age_ms < threshold_ms) {
      tt_log_debug("liveness: last_update_ts age=%llums < threshold=%llums",
                   (unsigned long long)age_ms,
                   (unsigned long long)threshold_ms);
      return 0; /* recently updated */
    }
    tt_log_warning(
        "Daemon     tinytd last update %llu ms ago (threshold %llu ms) — may "
        "be dead",
        (unsigned long long)age_ms, (unsigned long long)threshold_ms);
    tt_log_warning(
        "           See https://tinytrack.dev/docs/troubleshooting#stale-mmap");
    return -1;
  }

  /* No timestamp at all — brand new or corrupt file */
  tt_log_warning(
      "Daemon     mmap has no update timestamp — tinytd may not be running");
  tt_log_warning(
      "           See https://tinytrack.dev/docs/troubleshooting#stale-mmap");
  return -1;
}

/* Read a single trimmed line from path into buf[size]. Returns 0 on success. */
static int read_proc_line(const char* path, char* buf, size_t size) {
  FILE* f = fopen(path, "r");
  if (!f) {
    tt_log_debug("sysinfo: cannot open %s: %s", path, strerror(errno));
    return -1;
  }
  if (!fgets(buf, (int)size, f)) {
    tt_log_debug("sysinfo: empty read from %s", path);
    fclose(f);
    return -1;
  }
  fclose(f);
  size_t n = strlen(buf);
  if (n > 0 && buf[n - 1] == '\n')
    buf[n - 1] = '\0';
  return 0;
}

int ttg_reader_get_sysinfo(struct ttg_reader* ctx,
                           struct tt_proto_sysinfo* out) {
  memset(out, 0, sizeof(*out));

  /* hostname from /proc/sys/kernel/hostname — reflects the host when
   * /proc is bind-mounted; no fallback to gethostname() to avoid
   * silently returning the container's hostname instead. */
  if (read_proc_line(tt_sysfs_hostname(), out->hostname,
                     sizeof(out->hostname)) < 0)
    tt_log_warning("sysinfo: hostname unavailable (path=%s)",
                   tt_sysfs_hostname());
  else
    tt_log_debug("sysinfo: hostname=%s (from %s)", out->hostname,
                 tt_sysfs_hostname());

  /* OS type+release from /proc/sys/kernel/ostype and osrelease —
   * reflects the host kernel when /proc is bind-mounted.
   * uname() is NOT used as a fallback: it always returns the container's
   * (or host's) kernel as seen by the current namespace, which may differ
   * from what the bind-mounted /proc reports. */
  char ostype[32] = {0}, osrelease[64] = {0};
  int got_ostype = read_proc_line(tt_sysfs_ostype(), ostype, sizeof(ostype));
  int got_osrelease =
      read_proc_line(tt_sysfs_osrelease(), osrelease, sizeof(osrelease));

  if (got_ostype == 0 && got_osrelease == 0) {
    snprintf(out->os_type, sizeof(out->os_type), "%s %s", ostype, osrelease);
    tt_log_debug("sysinfo: os_type=%s (from %s + %s)", out->os_type,
                 tt_sysfs_ostype(), tt_sysfs_osrelease());
  } else {
    tt_log_warning("sysinfo: os_type unavailable (ostype=%s osrelease=%s)",
                   tt_sysfs_ostype(), tt_sysfs_osrelease());
  }

  /* uptime from /proc/uptime */
  char upbuf[64] = {0};
  if (read_proc_line(tt_sysfs_uptime(), upbuf, sizeof(upbuf)) == 0) {
    double up = 0;
    if (sscanf(upbuf, "%lf", &up) == 1) {
      out->uptime_sec = htobe64((uint64_t)up);
      tt_log_debug("sysinfo: uptime=%.0f s (from %s)", up, tt_sysfs_uptime());
    } else {
      tt_log_warning("sysinfo: cannot parse uptime from %s: '%s'",
                     tt_sysfs_uptime(), upbuf);
    }
  } else {
    tt_log_warning("sysinfo: uptime unavailable (path=%s)", tt_sysfs_uptime());
  }

  /* ring capacities from shm metadata */
  if (ctx->ring.l1_meta)
    out->slots_l1 = htonl(ctx->ring.l1_meta->capacity);
  if (ctx->ring.l2_meta)
    out->slots_l2 = htonl(ctx->ring.l2_meta->capacity);
  if (ctx->ring.l3_meta)
    out->slots_l3 = htonl(ctx->ring.l3_meta->capacity);
  tt_log_debug("sysinfo: slots l1=%u l2=%u l3=%u",
               ctx->ring.l1_meta ? ctx->ring.l1_meta->capacity : 0,
               ctx->ring.l2_meta ? ctx->ring.l2_meta->capacity : 0,
               ctx->ring.l3_meta ? ctx->ring.l3_meta->capacity : 0);

  /* intervals from ring header */
  const struct ttr_header* hdr = (const struct ttr_header*)ctx->ring.addr;
  out->interval_ms = hdr ? htonl(hdr->interval_ms) : htonl(1000);
  out->agg_l2_ms = hdr ? htonl(hdr->l2_agg_interval_ms) : htonl(60000);
  out->agg_l3_ms = hdr ? htonl(hdr->l3_agg_interval_ms) : htonl(3600000);
  tt_log_debug("sysinfo: interval_ms=%u agg_l2_ms=%u agg_l3_ms=%u%s",
               hdr ? hdr->interval_ms : 1000,
               hdr ? hdr->l2_agg_interval_ms : 60000,
               hdr ? hdr->l3_agg_interval_ms : 3600000,
               hdr ? "" : " (defaults, no shm header)");

  return 0;
}
