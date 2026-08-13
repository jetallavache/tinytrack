#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/config.h"
#include "common/log/log.h"
#include "common/sysfs.h"
#include "config.h"
#include "fetch.h"
#include "runtime.h"
#include "pipeline.h"
#include "scheduler.h"
#include "watch.h"
#include "writer.h"

/*
                         Linux Kernel
                              │
           ┌──────────────────┼──────────────────┐
           │                  │                  │
           ▼                  ▼                  ▼
        /proc                /sys             netlink
           │                  │                  │
           └──────────────────┼──────────────────┘
                              │
                              ▼
                           fetch
                              │
                              ▼
                           metrics
                              │
                              ▼
                           analyzer
                              │
                     ┌────────┴────────┐
                     ▼                 ▼
                   state             events
                     │                 │
                     └────────┬────────┘
                              ▼
                            writer

                       Linux event sources
                              │
                              ▼
                            epoll
                              │
                              ▼
                           runtime
                              │
                              ▼
                          scheduler
                              │
                              ▼
                          pipeline
*/

/*
| Задача                    | Механизм        |
| ------------------------- | --------------- |
| ждать несколько событий   | `epoll`         |
| периодический sampling    | `timerfd`       |
| graceful shutdown         | `signalfd`      |
| IPC / пробуждение loop    | `eventfd`       |
| network state changes     | `netlink`       |
| CPU/memory statistics     | `/proc`         |
| kernel/system information | `/sys`          |
| filesystem statistics     | `statvfs`       |
| process information       | `/proc/<pid>`   |
| high-resolution time      | `clock_gettime` |
*/

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
  (void)sig;
  running = 0;
}

static void daemonize(void) {
  pid_t pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS);

  if (setsid() < 0)
    exit(EXIT_FAILURE);

  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS);

  umask(0);
  if (chdir("/") != 0) { /* best-effort, ignore error in daemon */
  }

  for (int fd = sysconf(_SC_OPEN_MAX); fd >= 3; fd--)
    close(fd);

  /* Redirect stdin/stdout/stderr to /dev/null so fd 0-2 stay reserved */
  int devnull = open("/dev/null", O_RDWR);
  if (devnull >= 0) {
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (devnull > 2)
      close(devnull);
  }
}

static int write_pid_file(const char* path) {
  FILE* f = fopen(path, "w");
  if (!f)
    return -1;
  fprintf(f, "%d\n", getpid());
  fclose(f);
  return 0;
}

static int drop_privileges(const char* user, const char* group) {
  struct group* gr = getgrnam(group);
  if (!gr) {
    tt_log_err("Privileges unknown group: %s", group);
    tt_log_err(
        "           See "
        "https://tinytrack.dev/docs/troubleshooting#drop-privileges");
    return -1;
  }
  if (setgid(gr->gr_gid) < 0) {
    tt_log_err("Privileges setgid failed: %s", strerror(errno));
    return -1;
  }
  if (setgroups(0, NULL) < 0)
    tt_log_warning("Privileges setgroups failed: %s (non-fatal)",
                   strerror(errno));

  struct passwd* pw = getpwnam(user);
  if (!pw) {
    tt_log_err("Privileges unknown user: %s", user);
    tt_log_err(
        "           See "
        "https://tinytrack.dev/docs/troubleshooting#drop-privileges");
    return -1;
  }
  if (setuid(pw->pw_uid) < 0) {
    tt_log_err("Privileges setuid failed: %s", strerror(errno));
    return -1;
  }
  return 0;
}

static void cleanup(struct ttd_runtime* rt, struct ttd_fetch* fch,
                    struct ttd_watch* watch, struct ttd_writer* writer,
                    const char* pid_file) {
  (void)fch;
  (void)watch;
  ttd_runtime_free(rt);
  ttd_fetch_cleanup();
  ttd_watch_cleanup();
  ttd_writer_cleanup(writer);
  if (pid_file)
    unlink(pid_file);
}

int main(int argc, char** argv) {
  struct ttd_config cfg = {0};
  struct ttd_writer writer = {0};
  struct ttd_fetch fch = {0};
  struct ttd_watch watch = {0};
  struct ttd_pipeline pip = {0};
  struct ttd_scheduler schema = {0};

  struct ttd_runtime rt = {0};

  int do_daemonize = 1;
  const char* config_path = NULL;

  static const struct option long_opts[] = {
      {"config", required_argument, NULL, 'c'},
      {"daemon", no_argument, NULL, 'd'},
      {"no-daemon", no_argument, NULL, 'n'},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "dc:nh", long_opts, NULL)) != -1) {
    switch (opt) {
      case 'd':
        do_daemonize = 1;
        break;
      case 'n':
        do_daemonize = 0;
        break;
      case 'c':
        config_path = optarg;
        break;
      case 'h':
        printf(
            "Usage: tinytd [-d] [-c CONFIG]\n\n"
            "Options:\n"
            "  -d, --daemon      Run as daemon (background, default)\n"
            "  -n, --no-daemon   Run in foreground\n"
            "  -c, --config CONFIG  Path to configuration file\n"
            "                    Default: /etc/tinytrack/tinytrack.conf\n"
            "  -h, --help        Show this help and exit\n\n"
            "Signals:\n"
            "  SIGTERM/SIGINT  Graceful shutdown\n");
        return 0;
      default:
        fprintf(stderr, "Try 'tinytd --help' for usage.\n");
        return 1;
    }
  }

  if (!config_path)
    config_path = tt_config_file_path();

  if (ttd_config_load(config_path, &cfg) < 0) {
    fprintf(stderr, "tinytd: cannot open config file: %s\n", config_path);
    perror("");
    return 1;
  }

  /* Daemonize before tt_log_init (closes all fds) */
  if (do_daemonize)
    daemonize();

  /* Init sysfs paths from config (env vars TT_PROC_ROOT/TT_ROOTFS_PATH
   * take precedence over config file values if set) */
  tt_sysfs_set_proc_root(cfg.proc_root);
  tt_sysfs_set_rootfs_path(cfg.rootfs_path);
  tt_sysfs_init();

  struct tt_log_config log_cfg = {.backend = cfg.log_backend,
                                  .min_level = cfg.log_level,
                                  .ident = "tinytd",
                                  .async = false};
  tt_log_init(&log_cfg);

  /* ── Startup ─────────────────────────────────────────────────────── */
  tt_log_notice("tinytd starting (config=%s)", config_path);

  /* Remove stale live file (shadow is kept for recovery) */
  unlink(cfg.live_path);

  if (ttd_writer_init(&writer, &cfg) < 0) {
    tt_log_err("Storage    cannot initialize ring buffer");
    tt_log_err(
        "           See https://tinytrack.dev/docs/troubleshooting#no-mmap");
    return 1;
  }

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  struct ttd_fetch_state fch_state = {0};
  struct ttd_trends fch_trends = {0};
  fch.state = &fch_state;
  fch.trends = &fch_trends;
  ttd_fetch_init(&fch);
  fch.state->du_inval = cfg.du_interval_sec;

  struct tt_metrics watch_metrics = {0};
  watch.recent_samples = &watch_metrics;
  ttd_watch_init(&watch);

  if (ttd_runtime_init(&rt, &cfg, &fch, &watch, &writer) < 0) {
    ttd_fetch_cleanup();
    ttd_watch_cleanup();
    ttd_writer_cleanup(&writer);
    return 1;
  }


  /* ......... */

  // ttd_pipeline_init(
  //       &pipeline,
  //       &fetch,
  //       &analyzer,
  //       &writer
  //   );

  //   ttd_runtime_init(
  //       &runtime,
  //       &scheduler,
  //       &pipeline
  //   );

  //   ttd_runtime_run(&runtime);

  //   ttd_runtime_free(&runtime);
  //   ttd_pipeline_free(&pipeline);
  //   ttd_analyzer_free(&analyzer);
  //   ttd_fetch_cleanup();
  //   ttd_writer_cleanup(&writer);

  /* ........... */

  if (write_pid_file(cfg.pid_file) < 0)
    tt_log_warning("PID file   cannot write %s (non-fatal)", cfg.pid_file);

  if (getuid() == 0) {
    struct passwd* pw = getpwnam(cfg.user);
    struct group* gr = getgrnam(cfg.group);
    if (pw && gr) {
      if (chown(cfg.live_path, pw->pw_uid, gr->gr_gid) != 0)
        tt_log_warning("Storage    chown %s failed: %s", cfg.live_path,
                       strerror(errno));
      if (chown(cfg.shadow_path, pw->pw_uid, gr->gr_gid) != 0)
        tt_log_warning("Storage    chown %s failed: %s", cfg.shadow_path,
                       strerror(errno));
    }
    if (drop_privileges(cfg.user, cfg.group) < 0) {
      tt_log_err("Privileges cannot drop to %s:%s", cfg.user, cfg.group);
      tt_log_err(
          "           See "
          "https://tinytrack.dev/docs/troubleshooting#drop-privileges");
      cleanup(&rt, &fch, &watch, &writer, cfg.pid_file);
      return 1;
    }
    tt_log_info("Privileges user=%s  group=%s", cfg.user, cfg.group);
  }

  /* ── Storage ─────────────────────────────────────────────────────── */
  tt_log_info("Storage    live=%s", cfg.live_path);
  tt_log_info("Storage    shadow=%s  sync=%us", cfg.shadow_path,
              cfg.shadow_sync_interval_sec);

  /* ── Collection ──────────────────────────────────────────────────── */
  tt_log_info("Collection interval=%u ms  du_interval=%u s", cfg.interval_ms,
              cfg.du_interval_sec);
  tt_log_info(
      "Ring       L1=%u slots (1s/1h)  L2=%u slots (1min/24h)  L3=%u slots "
      "(1h/30d)",
      cfg.l1_capacity, cfg.l2_capacity, cfg.l3_capacity);

  /* ── Ready ───────────────────────────────────────────────────────── */
  tt_log_notice("tinytd ready  pid=%d", (int)getpid());

  while (running)
    ttd_runtime_poll(&rt, 1000);

  tt_log_notice("tinytd shutting down...");
  /**
   * После получения сигнала о завершении работы необходимо безопасно закончить работу всех модулей:
   * stop scheduler - остановить планировщик
   * flush pipeline - выполнить последний запланированный конвейер
   * sync writer - выполнить синхронизацию данных (shadow sync)
   * close resources - закрыть все ресурсы (файловые дескрипторы, очистить память)
   * exit
   */
  cleanup(&rt, &fch, &watch, &writer, cfg.pid_file);
  tt_log_shutdown();

  return 0;
}
