#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/log/log.h"
#include "common/sysfs.h"
#include "config.h"
#include "http.h"
#include "net.h"
#include "reader.h"
#include "session.h"
#include "tls.h"
#include "url.h"

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
  chdir("/");

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
    tt_log_err("Unknown group: %s", group);
    return -1;
  }
  if (setgid(gr->gr_gid) < 0) {
    tt_log_err("setgid failed");
    return -1;
  }
  setgroups(0, NULL); /* drop supplementary groups inherited from root */

  struct passwd* pw = getpwnam(user);
  if (!pw) {
    tt_log_err("Unknown user: %s", user);
    return -1;
  }
  if (setuid(pw->pw_uid) < 0) {
    tt_log_err("setuid failed");
    return -1;
  }

  return 0;
}

int main(int argc, char** argv) {
  const char* config_path = NULL;
  const char* hostname_override = NULL;
  const char* shm_override = NULL;
  uint16_t port_override = 0;
  int do_daemonize = 1;

  static const struct option long_opts[] = {
      {"config", required_argument, NULL, 'c'},
      {"port", required_argument, NULL, 'p'},
      {"hostname", required_argument, NULL, 'H'},
      {"shm", required_argument, NULL, 's'},
      {"daemon", no_argument, NULL, 'd'},
      {"no-daemon", no_argument, NULL, 'n'},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "c:p:H:s:dnh", long_opts, NULL)) !=
         -1) {
    switch (opt) {
      case 'c':
        config_path = optarg;
        break;
      case 'H':
        hostname_override = optarg;
        break;
      case 's':
        shm_override = optarg;
        break;
      case 'd':
        do_daemonize = 1;
        break;
      case 'n':
        do_daemonize = 0;
        break;
      case 'p': {
        int p = atoi(optarg);
        if (p > 0 && p < 65536)
          port_override = (uint16_t)p;
        break;
      }
      case 'h':
        printf(
            "Usage: tinytrack [-d] [-c CONFIG] [-p PORT] [-H HOST] [-s "
            "SHM_PATH]\n\n"
            "Options:\n"
            "  -d, --daemon        Run as daemon (background, default)\n"
            "  -n, --no-daemon     Run in foreground\n"
            "  -c, --config FILE   Path to configuration file\n"
            "  -p, --port PORT     Listen port (overrides config "
            "gateway.port)\n"
            "  -H, --hostname HOST Bind address (overrides config "
            "gateway.hostname)\n"
            "  -s, --shm PATH      Path to tinytd live mmap file\n"
            "  -h, --help          Show this help and exit\n\n"
            "Signals:\n"
            "  SIGTERM/SIGINT  Graceful shutdown\n");
        return 0;
      default:
        fprintf(stderr, "Try 'tinytrack --help' for usage.\n");
        return 1;
    }
  }

  struct ttg_config cfg;
  memset(&cfg, 0, sizeof(cfg));
  ttg_config_load(&cfg, config_path, hostname_override, port_override,
                  shm_override);

  /* Daemonize before tt_log_init (closes all fds) */
  if (do_daemonize)
    daemonize();

  /* Init sysfs paths (env vars TT_PROC_ROOT/TT_ROOTFS_PATH override defaults)
   */
  tt_sysfs_init();

  struct tt_log_config log_cfg = {
      .backend = cfg.log_backend,
      .min_level = cfg.log_level,
      .ident = "tinytrack",
      .async = false,
  };
  tt_log_init(&log_cfg);

  /* ── Startup ─────────────────────────────────────────────────────── */
  tt_log_notice("tinytrack gateway starting (config=%s)",
                config_path ? config_path : "(default)");

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  /* ── Storage ─────────────────────────────────────────────────────── */
  static struct ttg_reader reader;
  if (ttg_reader_open(&reader, cfg.shm_path) != 0) {
    tt_log_err("Cannot open shared memory: %s", cfg.shm_path);
    tt_log_err(
        "  Is tinytd running?  See "
        "https://tinytrack.dev/docs/troubleshooting#no-mmap");
    return 1;
  }
  tt_log_info("Storage    mmap=%s", cfg.shm_path);

  /* Non-fatal liveness check: warn if tinytd appears dead */
  if (ttg_reader_check_liveness(&reader) != 0)
    tt_log_warning(
        "Storage    gateway will serve stale data until tinytd restarts");

  if (write_pid_file(cfg.pid_file) < 0)
    tt_log_warning("PID file   cannot write %s (non-fatal)", cfg.pid_file);

  if (getuid() == 0) {
    if (drop_privileges(cfg.user, cfg.group) < 0) {
      tt_log_err("Privileges cannot drop to %s:%s", cfg.user, cfg.group);
      tt_log_err(
          "  See https://tinytrack.dev/docs/troubleshooting#drop-privileges");
      ttg_reader_close(&reader);
      unlink(cfg.pid_file);
      return 1;
    }
    tt_log_info("Privileges user=%s  group=%s", cfg.user, cfg.group);
  }

  ttg_session_init(&reader);
  ttg_session_set_auth(cfg.auth_token[0] ? cfg.auth_token : NULL,
                       cfg.auth_timeout_ms);
  ttg_session_set_cors(cfg.cors_origins[0] ? cfg.cors_origins : NULL);

  bool use_tls = ((ttg_url_is_ssl(cfg.listen)) != 0);
  if (use_tls && (cfg.tls_cert[0] == '\0' || cfg.tls_key[0] == '\0')) {
    tt_log_err(
        "TLS        cert or key not set (tls=true requires tls_cert + "
        "tls_key)");
    tt_log_err("  See https://tinytrack.dev/docs/troubleshooting#tls-config");
    ttg_reader_close(&reader);
    unlink(cfg.pid_file);
    return 1;
  }

  struct ttg_tls_cfg tls_cfg = {
      .cert_file = cfg.tls_cert[0] ? cfg.tls_cert : NULL,
      .key_file = cfg.tls_key[0] ? cfg.tls_key : NULL,
      .ca_file = cfg.tls_ca[0] ? cfg.tls_ca : NULL,
  };

  struct ttg_mgr mgr;
  ttg_net_mgr_init(&mgr, use_tls ? &tls_cfg : NULL);
  mgr.max_connections = cfg.max_connections;
  mgr.header_timeout_ms = cfg.header_timeout_ms ? cfg.header_timeout_ms : 10000;
  mgr.idle_timeout_ms = cfg.idle_timeout_ms;
  mgr.max_uri_size = cfg.max_uri_size ? cfg.max_uri_size : 8192;
  mgr.max_headers_size = cfg.max_headers_size ? cfg.max_headers_size : 16384;

  if (use_tls && !mgr.tls_ctx) {
    tt_log_err("TLS        context init failed — check cert/key files");
    tt_log_err("  See https://tinytrack.dev/docs/troubleshooting#tls-init");
    ttg_reader_close(&reader);
    unlink(cfg.pid_file);
    return 1;
  }

  ttg_net_timer_add(&mgr, 500, TIMER_REPEAT, ttg_session_timer_fn, &mgr);

  /* ── Endpoints ───────────────────────────────────────────────────── */
  tt_log_info("WebSocket  %s/v1/stream  (legacy: /websocket)", cfg.listen);
  tt_log_info("Metrics    %s/v1/metrics  (?format=json|csv|xml|prometheus)",
              cfg.listen);
  tt_log_info("Sysinfo    %s/v1/sysinfo  (?format=json|csv|xml)", cfg.listen);
  tt_log_info("Status     %s/v1/status  (public, no auth)", cfg.listen);

  /* ── Security ────────────────────────────────────────────────────── */
  tt_log_info("TLS        %s", use_tls ? "enabled" : "disabled");
  tt_log_info("Auth       %s",
              (cfg.auth_enable == true)
                  ? (cfg.auth_token[0] == '\0')
                        ? (cfg.auth_token[0] == '\1')
                              ? "error (Couldn't read value from token "
                                "configuration file)"
                              : "error (AUTH_TOKEN_PATH is not set)"
                        : "enabled (Bearer header / CMD_AUTH)"
                  : "disabled");
  if (cfg.cors_origins[0])
    tt_log_info("CORS       %s", strcmp(cfg.cors_origins, "*") == 0
                                     ? "* (all origins — dev mode only)"
                                     : cfg.cors_origins);
  else
    tt_log_info("CORS       disabled");

  /* ── Ready ───────────────────────────────────────────────────────── */
  tt_log_notice("tinytrack ready  listen=%s  pid=%d", cfg.listen,
                (int)getpid());

  ttg_http_listen(&mgr, cfg.listen, ttg_session_event_fn, NULL);

  while (running)
    ttg_net_mgr_poll(&mgr, 500);

  ttg_net_mgr_free(&mgr);
  ttg_reader_close(&reader);
  unlink(cfg.pid_file);
  tt_log_notice("tinytrack gateway shutting down...");
  tt_log_shutdown();

  return 0;
}
