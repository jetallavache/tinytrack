#ifndef TTD_CONFIG_H
#define TTD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "common/log/log.h"

struct ttd_config {
  /* Daemon */
  char user[32];
  char group[32];
  char pid_file[256];
  enum tt_log_backend log_backend;
  enum tt_log_level log_level;

  /* Collection */
  uint32_t interval_ms;
  uint32_t du_interval_sec;
  char proc_root[256];   /* /proc root; override for container: /host/proc */
  char rootfs_path[256]; /* rootfs root; override for container: /host/rootfs */

  /* Storage */
  char live_path[256];
  char shadow_path[256];
  uint32_t shadow_sync_interval_sec;
  int file_mode;

  /* Ringbuffer */
  uint32_t l1_capacity;
  uint32_t l2_capacity;
  uint32_t l3_capacity;
  uint32_t le_capacity;
  uint32_t l2_agg_interval_sec;
  uint32_t l3_agg_interval_sec;
  uint32_t le_check_interval_sec;

  /* Recovery */
  bool enable_crc;
  bool auto_recover;
};

int ttd_config_load(const char* path, struct ttd_config* cfg);

#endif /* TTD_CONFIG_H */
