#ifndef TT_SYSFS_H
#define TT_SYSFS_H

/*
 * tt_sysfs — configurable paths to host system resources.
 *
 * By default all paths point to the local /proc and /.
 * In a Docker container that bind-mounts the host's /proc and / :
 *
 *   docker run \
 *     -v /proc:/host/proc:ro \
 *     -v /:/host/rootfs:ro   \
 *     -e TT_PROC_ROOT=/host/proc \
 *     -e TT_ROOTFS_PATH=/host/rootfs \
 *     tinytrack
 *
 * tt_sysfs_init() reads TT_PROC_ROOT and TT_ROOTFS_PATH from the
 * environment once at startup; all subsequent calls to tt_sysfs_path()
 * return pre-built paths without further env lookups.
 */

/* Call once at program startup (reads env vars). */
void tt_sysfs_init(void);

/*
 * Override paths programmatically (e.g. from config file).
 * Must be called before tt_sysfs_init(), or after to override env.
 */
void tt_sysfs_set_proc_root(const char* path);
void tt_sysfs_set_rootfs_path(const char* path);

/* Return full path: proc_root + "/" + rel  (e.g. "stat" -> "/proc/stat"). */
const char* tt_sysfs_proc(const char* rel);

/* Return full path: rootfs_path + "/" + rel  (e.g. "" -> "/"). */
const char* tt_sysfs_rootfs(const char* rel);

/* Convenience accessors for fixed /proc files used by the collector. */
const char* tt_sysfs_stat(void);
const char* tt_sysfs_meminfo(void);
const char* tt_sysfs_net_dev(void);
const char* tt_sysfs_loadavg(void);
const char* tt_sysfs_vmstat(void);
const char* tt_sysfs_uptime(void);
const char* tt_sysfs_hostname(void);  /* /proc/sys/kernel/hostname */
const char* tt_sysfs_ostype(void);    /* /proc/sys/kernel/ostype   */
const char* tt_sysfs_osrelease(void); /* /proc/sys/kernel/osrelease */

#endif /* TT_SYSFS_H */
