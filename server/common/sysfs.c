#include "sysfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base paths are limited to 128 bytes so that suffixes (up to ~24 bytes)
 * always fit in the 256-byte result buffers without truncation. */
#define BASE_MAX_LEN 128
#define PATH_MAX_LEN 256

static char g_proc_root[BASE_MAX_LEN] = "/proc";
static char g_rootfs_path[BASE_MAX_LEN] = "/";

/* Pre-built paths — filled by tt_sysfs_init() */
static char p_stat[PATH_MAX_LEN];
static char p_meminfo[PATH_MAX_LEN];
static char p_net_dev[PATH_MAX_LEN];
static char p_loadavg[PATH_MAX_LEN];
static char p_vmstat[PATH_MAX_LEN];
static char p_fs[PATH_MAX_LEN];
static char p_uptime[PATH_MAX_LEN];
static char p_hostname[PATH_MAX_LEN];
static char p_ostype[PATH_MAX_LEN];
static char p_osrelease[PATH_MAX_LEN];

static void build_paths(void) {
  snprintf(p_stat, sizeof(p_stat), "%s/stat", g_proc_root);
  snprintf(p_meminfo, sizeof(p_meminfo), "%s/meminfo", g_proc_root);
  snprintf(p_net_dev, sizeof(p_net_dev), "%s/net/dev", g_proc_root);
  snprintf(p_loadavg, sizeof(p_loadavg), "%s/loadavg", g_proc_root);
  snprintf(p_vmstat, sizeof(p_vmstat), "%s/vmstat", g_proc_root);
  snprintf(p_fs, sizeof(p_fs), "%s/sys/fs/file-nr", g_proc_root);
  snprintf(p_uptime, sizeof(p_uptime), "%s/uptime", g_proc_root);
  snprintf(p_hostname, sizeof(p_hostname), "%s/sys/kernel/hostname",
           g_proc_root);
  snprintf(p_ostype, sizeof(p_ostype), "%s/sys/kernel/ostype", g_proc_root);
  snprintf(p_osrelease, sizeof(p_osrelease), "%s/sys/kernel/osrelease",
           g_proc_root);
}

void tt_sysfs_init(void) {
  const char* env;
  if ((env = getenv("TT_PROC_ROOT")) != NULL)
    strncpy(g_proc_root, env, BASE_MAX_LEN - 1);
  if ((env = getenv("TT_ROOTFS_PATH")) != NULL)
    strncpy(g_rootfs_path, env, BASE_MAX_LEN - 1);
  build_paths();
}

void tt_sysfs_set_proc_root(const char* path) {
  strncpy(g_proc_root, path, BASE_MAX_LEN - 1);
  build_paths();
}

void tt_sysfs_set_rootfs_path(const char* path) {
  strncpy(g_rootfs_path, path, BASE_MAX_LEN - 1);
}

const char* tt_sysfs_proc(const char* rel) {
  static char buf[PATH_MAX_LEN];
  snprintf(buf, sizeof(buf), "%s/%s", g_proc_root, rel);
  return buf;
}

const char* tt_sysfs_rootfs(const char* rel) {
  static char buf[PATH_MAX_LEN];
  if (rel && rel[0])
    snprintf(buf, sizeof(buf), "%s/%s", g_rootfs_path, rel);
  else
    snprintf(buf, sizeof(buf), "%s", g_rootfs_path);
  return buf;
}

const char* tt_sysfs_stat(void) {
  return p_stat;
}
const char* tt_sysfs_meminfo(void) {
  return p_meminfo;
}
const char* tt_sysfs_net_dev(void) {
  return p_net_dev;
}
const char* tt_sysfs_loadavg(void) {
  return p_loadavg;
}
const char* tt_sysfs_vmstat(void) {
  return p_vmstat;
}
const char* tt_sysfs_fs(void) {
  return p_fs;
}
const char* tt_sysfs_uptime(void) {
  return p_uptime;
}
const char* tt_sysfs_hostname(void) {
  return p_hostname;
}
const char* tt_sysfs_ostype(void) {
  return p_ostype;
}
const char* tt_sysfs_osrelease(void) {
  return p_osrelease;
}
