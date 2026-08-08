#ifndef TTD_RUNTIME_H
#define TTD_RUNTIME_H

#include <stdint.h>

#include "config.h"
#include "fetch.h"
#include "watch.h"
#include "writer.h"

/**
 * runtime - event loop
 *
 * Как ждать события?
 *
 * 1. epoll
 * 2. timerfd
 * 3. eventfd:
 *
 *    fetch worker
 *    analysis worker
 *    network worker
 *
 * 4. signals/signalfd
 * 5. file descriptors
 * 6. shutdown/lifecycle:
 *
 *    SIGTERM
 *       │
 *    runtime
 *       │
 *    STOPPING
 *       │
 *       ├── stop scheduler
 *       ├── flush pipeline
 *       ├── sync writer
 *       ├── close resources
 *       └── exit
 *    (lifecycle.c)
 *    (lifecycle.h)
 *
 * 7. netlink
 *
 * polling + event-driven observation
 *
 * (netlink.c)
 * (netlink.h)
 *
 * 8. dispatch
 */

/**
 * Можно сделать:
 *
 * ttd_runtime_run(&runtime);
 *
 * А внутри:
 *
 * run()
 *  └── event_loop()
 *        ├── timer event
 *        ├── signal event
 *        ├── netlink event
 *        └── other events
 */

enum ttd_runtime_state {
    TTD_RUNNING,
    TTD_STOPPING,
    TTD_STOPPED,
};

/* Убрать volatile sig_atomic_t running = 1; */

struct ttd_runtime {
  int epoll_fd;
  int timer_fd;
  struct ttd_config* cfg;
  struct ttd_fetch* fch;  /* Не эта зона ответственности */
  struct ttd_writer* writer;  /* Не эта зона ответственности */
  struct ttd_watch* watch;  /* Не эта зона ответственности */
  uint64_t next_l2;
  uint64_t next_l3;
  uint64_t next_le;
  uint64_t next_shadow;
};

int ttd_runtime_init(struct ttd_runtime* rt, struct ttd_config* cfg,
                     struct ttd_fetch* fch, struct ttd_watch* watch,
                     struct ttd_writer* writer);
void ttd_runtime_poll(struct ttd_runtime* rt, int timeout_ms);
void ttd_runtime_free(struct ttd_runtime* rt);

#endif /* TTD_RUNTIME_H */
