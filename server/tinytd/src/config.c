#include "config.h"

#include <stdio.h>
#include <string.h>

#include "common/config.h"

int ttd_config_load(const char* path, struct ttd_config* cfg) {
  if (!path)
    path = tt_config_file_path();

  FILE* f = fopen(path, "r");
  if (!f)
    return -1;
  fclose(f);

  /* [tinytd] */
  tt_config_read_str(path, "tinytd.user", cfg->user, sizeof(cfg->user),
                     "tinytd");
  tt_config_read_str(path, "tinytd.group", cfg->group, sizeof(cfg->group),
                     "tinytd");
  tt_config_read_str(path, "tinytd.pid_file", cfg->pid_file,
                     sizeof(cfg->pid_file), "/var/run/tinytd.pid");
  char log_backend_str[32];
  if (tt_config_read_str(path, "tinytd.log_backend", log_backend_str,
                         sizeof(log_backend_str), "auto") == 0)
    cfg->log_backend = tt_config_parse_log_backend(log_backend_str);
  char log_level_str[32];
  if (tt_config_read_str(path, "tinytd.log_level", log_level_str,
                         sizeof(log_level_str), "info") == 0)
    cfg->log_level = tt_config_parse_log_level(log_level_str);

  /* [collection] */
  cfg->interval_ms = tt_config_read_int(path, "collection.interval_ms", 1000);
  cfg->du_interval_sec =
      tt_config_read_int(path, "collection.du_interval_sec", 30);
  tt_config_read_str(path, "collection.proc_root", cfg->proc_root,
                     sizeof(cfg->proc_root), "/proc");
  tt_config_read_str(path, "collection.rootfs_path", cfg->rootfs_path,
                     sizeof(cfg->rootfs_path), "/");

  /* [storage] */
  tt_config_read_str(path, "storage.live_path", cfg->live_path,
                     sizeof(cfg->live_path), "/dev/shm/tinytd-live.dat");
  tt_config_read_str(path, "storage.shadow_path", cfg->shadow_path,
                     sizeof(cfg->shadow_path),
                     "/var/lib/tinytrack/tinytd-shadow.dat");
  cfg->shadow_sync_interval_sec =
      tt_config_read_int(path, "storage.shadow_sync_interval_sec", 60);
  cfg->file_mode = tt_config_read_int(path, "storage.file_mode", 0640);

  /* [ringbuffer] */
  cfg->l1_capacity = tt_config_read_int(path, "ringbuffer.l1_capacity", 3600);
  cfg->l2_capacity = tt_config_read_int(path, "ringbuffer.l2_capacity", 1440);
  cfg->l2_agg_interval_sec =
      tt_config_read_int(path, "ringbuffer.l2_agg_interval_sec", 60);
  /* L3: 1 sample/hour × 24h × 31 days = 744 slots */
  cfg->l3_capacity = tt_config_read_int(path, "ringbuffer.l3_capacity", 744);
  cfg->l3_agg_interval_sec =
      tt_config_read_int(path, "ringbuffer.l3_agg_interval_sec", 3600);

  /* LE */
  cfg->le_capacity = tt_config_read_int(path, "ringbuffer.le_capacity", 1000);
  cfg->le_check_interval_sec =
      tt_config_read_int(path, "ringbuffer.le_check_interval_sec", 300);

  /* [recovery] */
  cfg->enable_crc = tt_config_read_bool(path, "recovery.enable_crc", true);
  cfg->auto_recover = tt_config_read_bool(path, "recovery.auto_recover", true);

  return 0;
}
