#include "runtime.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "common/event.h"
#include "common/log/log.h"
#include "common/timer.h"
#include "debug.h"
#include "mem.h"

#define tt_metrics tt_metrics_ex

static uint64_t now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// void check_and_emit_events(struct tt_metrics* current,
//                            struct tt_metrics* previous) {
//   /* Проверяем изменение состояния памяти */
//   if (current->mem_state_flags != previous->mem_state_flags) {
//     uint16_t changed_bits =
//         current->mem_state_flags ^ previous->mem_state_flags;

//     /* Определяем, какие аспекты изменились */
//     if (changed_bits & 0x0003) { /* availability изменилась */
//       tt_event_emit(TT_EVENT_STATE_CHANGE, COMPONENT_MEM,
//                     current->mem_state_flags & 0x0003);
//     }

// #define MEM_AVAIL_RED 3

//     /* Проверяем на критические события */
//     if ((current->mem_state_flags & 0x0003) == MEM_AVAIL_RED) {
//       tt_event_emit(TT_EVENT_MEM_PRESSURE_HIGH, COMPONENT_MEM,
//                     current->mem_usage_pct);
//     }
//   }

//   /* Детектируем утечку памяти (нужен контекст из нескольких измерений) */
//   // if (detect_memory_leak(last_n_measurements)) {
//   //     tt_event_emit(TT_EVENT_MEM_LEAK_DETECTED,
//   //               COMPONENT_MEM,
//   //               calculate_leak_rate());
//   // }

//   /* Детектируем swap thrashing */
//   // if (is_swap_thrashing(current, previous)) {
//   //     tt_event_emit(TT_EVENT_MEM_SWAP_THRASHING,
//   //               COMPONENT_MEM,
//   //               current->mem_usage_pct);
//   // }
// }

static void fetch_metrics(struct ttd_fetch* fch, struct tt_metrics* sample) {
  if (!fch || !sample) {
    tt_log_err("Invalid parameters to fetch_metrics");
    return;
  }

  int ret = 0;

  ret = ttd_fetch_cpu(fch);
  if (ret < 0) {
    tt_log_err("Failed to retrieve data from /proc/stat");
  } else
    sample->cpu_usage_pct = (uint16_t)(fch->state->pr_stat_pct.total_pct * 100);

  ret = ttd_fetch_memory(fch);
  if (ret < 0) {
    tt_log_err("Failed to retrieve data from /proc/meminfo");
  } else {
    sample->mem_usage_pct = (uint16_t)(((fch->pr_meminfo.mem_total -
                                         fch->pr_meminfo.mem_available) *
                                        100 / fch->pr_meminfo.mem_total) *
                                       100);
    sample->mem_state_flags = ttd_mem_state(fch);
  }

  ret = ttd_fetch_net(fch);
  if (ret < 0) {
    tt_log_err("Failed to retrieve data from /proc/net");
  } else {
    sample->net_rx_bytes = (uint64_t)fch->pr_net.rx_bytes;
    sample->net_tx_bytes = (uint64_t)fch->pr_net.tx_bytes;
  }

  ret = ttd_fetch_loadavg(fch);
  if (ret < 0) {
    tt_log_err("Failed to retrieve data from /proc/loadavg");
  } else {
    sample->load_1min = (uint16_t)(fch->pr_loadavg.load_1min * 100);
    sample->load_5min = (uint16_t)(fch->pr_loadavg.load_5min * 100);
    sample->load_15min = (uint16_t)(fch->pr_loadavg.load_15min * 100);
    sample->nr_running = (uint32_t)fch->pr_loadavg.nr_running;
    sample->nr_total = (uint32_t)fch->pr_loadavg.nr_total;
  }

  ret = ttd_fetch_disk(fch);
  if (ret < 0) {
    tt_log_err("Failed to retrieve data from statvfs");
  } else {
    sample->du_total_bytes = (uint64_t)fch->state->du_cached.total_bytes;
    sample->du_free_bytes = (uint64_t)fch->state->du_cached.free_bytes;
  }

  ret = ttd_fetch_oom_kills(fch);
  if (ret < 0) {
    tt_log_err("Failed to retrieve data from /proc/vmstat");
  } else {
    sample->oom_kill_count = (uint16_t)fch->pr_vmstat.oom_kill;
  }
}

int ttd_runtime_init(struct ttd_runtime* rt, struct ttd_config* cfg,
                     struct ttd_fetch* fch, struct ttd_watch* watch,
                     struct ttd_writer* writer) {
  if (!rt || !cfg || !fch || !writer) {
    tt_log_err("Invalid parameters to ttd_runtime_init");
    return -1;
  }

  rt->epoll_fd = -1;
  rt->timer_fd = -1;
  rt->cfg = cfg;
  rt->fch = fch;
  rt->watch = watch;
  rt->writer = writer;
  rt->next_l2 = 0;
  rt->next_l3 = 0;
  rt->next_le = 0;
  rt->next_shadow = 0;

  tt_log_debug("Runtime init: rt=%p, cfg=%p, fch=%p, writer=%p", (void*)rt,
               (void*)cfg, (void*)fch, (void*)writer);

  /* Create epoll */
  rt->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (rt->epoll_fd < 0) {
    tt_log_err("Runtime    epoll_create1 failed: %s", strerror(errno));
    return -1;
  }

  /* Create timer */
  rt->timer_fd = tt_timerfd_create(cfg->interval_ms);
  if (rt->timer_fd < 0) {
    tt_log_err("Runtime    timerfd_create failed: %s", strerror(errno));
    close(rt->epoll_fd);
    return -1;
  }

  /* Add timer to epoll */
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = rt->timer_fd};
  if (epoll_ctl(rt->epoll_fd, EPOLL_CTL_ADD, rt->timer_fd, &ev) < 0) {
    tt_log_err("Runtime    epoll_ctl failed: %s", strerror(errno));
    close(rt->timer_fd);
    close(rt->epoll_fd);
    return -1;
  }

  return 0;
}

void ttd_runtime_poll(struct ttd_runtime* rt, int timeout_ms) {
  struct epoll_event events[1];

  if (!rt || !rt->writer) {
    tt_log_err("Invalid runtime state: rt=%p, writer=%p", (void*)rt,
               rt ? (void*)rt->writer : NULL);
    return;
  }

  int nfds = epoll_wait(rt->epoll_fd, events, 1, timeout_ms);
  if (nfds < 0 && errno != EINTR) {
    tt_log_err("epoll_wait failed: %s", strerror(errno));
    return;
  }

  uint64_t now = now_ms();

  /* Handle timer event */
  if (nfds > 0 && events[0].data.fd == rt->timer_fd) {
    uint64_t expirations = 0;
    if (read(rt->timer_fd, &expirations, sizeof(expirations)) < 0)
      expirations = 0;

    /* tt_log_debug("Timer fired, collecting metrics (rt=%p, writer=%p)",
                 (void*)rt, (void*)rt->writer); */

    struct tt_metrics m = {0};
    m.timestamp = (uint64_t)time(NULL) * 1000;
    fetch_metrics(rt->fch, &m);

    ttd_writer_write_l1(rt->writer, &m);

    ttd_debug_dump_l1(rt->writer->ring.live_addr, rt->cfg->l1_capacity);

    if (tt_timer_expired(&rt->next_le, rt->cfg->le_check_interval_sec * 1000,
                         now)) {
      ttd_watch_metrics(rt->watch, &m, rt->writer);
      ttd_debug_dump_le(rt->writer->ring.live_addr, rt->cfg);
    }
  }

  /* Periodic tasks */
  if (tt_timer_expired(&rt->next_l2, rt->cfg->l2_agg_interval_sec * 1000,
                       now)) {
    ttd_writer_aggregate_l2(rt->writer);
  }

  if (tt_timer_expired(&rt->next_l3, rt->cfg->l3_agg_interval_sec * 1000,
                       now)) {
    ttd_writer_aggregate_l3(rt->writer);
  }

  if (tt_timer_expired(&rt->next_shadow,
                       rt->cfg->shadow_sync_interval_sec * 1000, now)) {
    ttd_writer_shadow_sync(rt->writer);
    ttd_debug_dump_rusage();
  }
}

void ttd_runtime_free(struct ttd_runtime* rt) {
  if (rt->timer_fd >= 0) {
    close(rt->timer_fd);
    rt->timer_fd = -1;
  }
  if (rt->epoll_fd >= 0) {
    close(rt->epoll_fd);
    rt->epoll_fd = -1;
  }
}
