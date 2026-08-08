
#include "watch.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

void find_oom_victim(int* pid, char* name, size_t name_len) {
  FILE* f = popen("dmesg | grep -i 'killed process' | tail -1", "r");
  if (!f)
    return;

  char line[256];
  if (fgets(line, sizeof(line), f)) {
    /*  For example: "Killed process 1234 (mysqld) total-vm:2048000kB" */
    int found_pid = 0;
    char found_name[64] = {0};

    if (sscanf(line, "%*s %*s %*s %d (%64[^)])", &found_pid, found_name) == 2) {
      *pid = found_pid;
      strncpy(name, found_name, name_len);
    }
  }
  pclose(f);
}

void check_oom_kill(struct ttd_watch* watch, struct tt_metrics* m,
                    struct ttd_writer* writer) {
  if (m->oom_kill_count > watch->last.oom_kill_count) {
    int victim_pid = 0;
    char victim_name[64] = "unknown";

    /* We’re looking for the latest OOM kill in dmesg */
    find_oom_victim(&victim_pid, victim_name, sizeof(victim_name));

    struct tt_event event = {
        .hdr.type = TT_CELL_EVENT,
        .timestamp = m->timestamp,
        .event_code = TT_EVENT_MEM_OOM_KILLED,
        .severity = TT_EVENT_CRITICAL,
        .component = COMPONENT_MEM,
        .prev_state = (watch->last.oom_kill_count << 16) | m->mem_state_flags,
        .new_state = (m->oom_kill_count << 16) | m->mem_state_flags,
        .metric_value = m->mem_usage_pct,
        .threshold =
            0, /* OOM — there is no threshold; it’s an absolute event */
        .process_pid = victim_pid,
        .duration_ms = 0,
        .correlation_id = 0,
    };

    /* Copy the process name to the reserved field (if it fits) */
    memcpy(event.reserved, victim_name,
           min(strlen(victim_name), sizeof(event.reserved)));

    ttd_writer_write_le(writer, &event);
  }
}

void ttd_watch_metrics(struct ttd_watch* watch, struct tt_metrics* current,
                       struct ttd_writer* writer) {
  check_oom_kill(watch, current, writer);

  /* Updating the latest values */
  watch->last.oom_kill_count = current->oom_kill_count;
  watch->last.net_rx_bytes = current->net_rx_bytes;
  watch->last.net_tx_bytes = current->net_tx_bytes;

  //   // 2. Проверка утечки памяти процесса
  //   check_memory_leak(watch, current, writer);

  //   // 3. Проверка swap thrashing
  //   check_swap_thrashing(watch, current, writer);

  //   // 4. Проверка inode exhaustion
  //   check_inode_exhaustion(watch, current, writer);

  //   // 5. Проверка FD leak
  //   check_fd_leak(watch, current, writer);

  //   // 6. Проверка процессов в D-state
  //   check_stuck_processes(watch, current, writer);

  //   // 7. Проверка zombie процессов
  //   check_zombie_processes(watch, current, writer);

  //   // 8. Проверка заполнения диска
  //   check_disk_full(watch, current, writer);

  //   // 9. Проверка kernel taint
  //   check_kernel_taint(watch, current, writer);

  //   // Добавляем семпл в историю для трендов
  //   add_to_history(watch, current);
}

void ttd_watch_init(struct ttd_watch* watch) {
  memset(watch->recent_samples, 0, sizeof(*watch->recent_samples));
}

void ttd_watch_cleanup() {}

// // 2. Детектор утечки памяти
// static void check_memory_leak(struct ttd_watch* watch, struct tt_metrics* m,
//                               struct ttd_writer* writer) {
//   // Проверяем главного потребителя RAM
//   if (m->top_rss_pid == 0)
//     return;

//   // Если процесс тот же, что и 5 минут назад — проверяем тренд
//   if (m->top_rss_pid == watch->leak_check_pid) {
//     uint64_t elapsed_ms = m->timestamp - watch->leak_check_start_ts;

//     // Проверяем каждые 5 минут
//     if (elapsed_ms > 300000) {
//       uint32_t rss_delta = m->top_rss_mb - watch->leak_check_rss_start;

//       // Если RSS вырос более чем на 50MB за 5 минут — утечка
//       if (rss_delta > 50) {
//         char proc_name[64] = {0};
//         get_process_name(m->top_rss_pid, proc_name, sizeof(proc_name));

//         struct tt_event_cell event = {
//             .timestamp = m->timestamp,
//             .event_code = TT_EVENT_MEM_LEAK_DETECTED,
//             .severity = SEVERITY_WARNING,
//             .component = COMPONENT_MEM,
//             .process_pid = m->top_rss_pid,
//             .metric_value = m->top_rss_mb,
//             .threshold = rss_delta,
//             .duration_ms = elapsed_ms,
//         };
//         memcpy(event.reserved, proc_name,
//                min(strlen(proc_name), sizeof(event.reserved)));

//         ttd_writer_write_event(writer, &event);
//       }

//       // Сбрасываем для следующего цикла
//       watch->leak_check_start_ts = m->timestamp;
//       watch->leak_check_rss_start = m->top_rss_mb;
//     }
//   } else {
//     // Новый процесс стал главным потребителем — начинаем отслеживать
//     watch->leak_check_pid = m->top_rss_pid;
//     watch->leak_check_start_ts = m->timestamp;
//     watch->leak_check_rss_start = m->top_rss_mb;
//   }
// }

// // 3. Детектор swap thrashing
// static void check_swap_thrashing(struct ttd_watch* watch, struct tt_metrics*
// m,
//                                  struct ttd_writer* writer) {
//   // Swap thrashing = интенсивный swap in/out при высокой загрузке CPU iowait
//   uint16_t swap_activity = (m->swap_in_kb + m->swap_out_kb);

//   // Если swap активность > 1MB/сек и iowait > 20% — это thrashing
//   if (swap_activity > 1024) {
//     struct tt_event_cell event = {
//         .timestamp = m->timestamp,
//         .event_code = TT_EVENT_MEM_SWAP_THRASHING,
//         .severity = SEVERITY_WARNING,
//         .component = COMPONENT_MEM,
//         .metric_value = swap_activity,
//         .threshold = 1024,  // 1MB/сек
//     };
//     ttd_writer_write_event(writer, &event);
//   }
// }

// // 4. Детектор inode exhaustion
// static void check_inode_exhaustion(struct ttd_watch* watch,
//                                    struct tt_metrics* m,
//                                    struct ttd_writer* writer) {
//   static uint16_t last_inode_pct = 0;

//   // Пороги: 80%, 90%, 95%
//   if (m->inode_usage_pct >= 9500 && last_inode_pct < 9500) {
//     emit_threshold_event(writer, m, TT_EVENT_DISK_INODE_CRITICAL,
//                          SEVERITY_CRITICAL, m->inode_usage_pct, 9500);
//   } else if (m->inode_usage_pct >= 9000 && last_inode_pct < 9000) {
//     emit_threshold_event(writer, m, TT_EVENT_DISK_INODE_HIGH,
//     SEVERITY_WARNING,
//                          m->inode_usage_pct, 9000);
//   } else if (m->inode_usage_pct >= 8000 && last_inode_pct < 8000) {
//     emit_threshold_event(writer, m, TT_EVENT_DISK_INODE_WARNING,
//     SEVERITY_INFO,
//                          m->inode_usage_pct, 8000);
//   }

//   last_inode_pct = m->inode_usage_pct;
// }

// // 5. Детектор утечки файловых дескрипторов
// static void check_fd_leak(struct ttd_watch* watch, struct tt_metrics* m,
//                           struct ttd_writer* writer) {
//   static uint16_t last_fd_pct = 0;
//   static uint64_t fd_growth_start_ts = 0;
//   static uint16_t fd_growth_start_pct = 0;

//   // Если FD usage растет
//   if (m->fd_usage_pct > last_fd_pct + 100) {  // +1%
//     if (fd_growth_start_ts == 0) {
//       fd_growth_start_ts = m->timestamp;
//       fd_growth_start_pct = last_fd_pct;
//     }

//     uint64_t elapsed = m->timestamp - fd_growth_start_ts;
//     if (elapsed > 300000) {  // 5 минут непрерывного роста
//       uint16_t growth = m->fd_usage_pct - fd_growth_start_pct;
//       if (growth > 500) {  // выросло на 5%
//         struct tt_event_cell event = {
//             .timestamp = m->timestamp,
//             .event_code = TT_EVENT_PROC_FD_LEAK,
//             .severity = SEVERITY_WARNING,
//             .component = COMPONENT_PROC,
//             .metric_value = m->fd_usage_pct,
//             .threshold = 100,
//             .duration_ms = elapsed,
//         };
//         ttd_writer_write_event(writer, &event);
//       }
//       fd_growth_start_ts = 0;  // сброс
//     }
//   } else {
//     fd_growth_start_ts = 0;  // тренд прервался
//   }

//   last_fd_pct = m->fd_usage_pct;
// }

// // 6. Детектор stuck процессов (D-state > 30 секунд)
// static void check_stuck_processes(struct ttd_watch* watch, struct tt_metrics*
// m,
//                                   struct ttd_writer* writer) {
//   static uint64_t stuck_start_ts = 0;

//   if (m->procs_blocked > 5) {  // больше 5 процессов в D-state
//     if (stuck_start_ts == 0) {
//       stuck_start_ts = m->timestamp;
//     }

//     if (m->timestamp - stuck_start_ts > 30000) {  // 30 секунд
//       struct tt_event_cell event = {
//           .timestamp = m->timestamp,
//           .event_code = TT_EVENT_DISK_STUCK_PROCESSES,
//           .severity = SEVERITY_WARNING,
//           .component = COMPONENT_DISK,
//           .metric_value = m->procs_blocked,
//           .threshold = 5,
//           .duration_ms = m->timestamp - stuck_start_ts,
//       };
//       ttd_writer_write_event(writer, &event);
//       stuck_start_ts = 0;
//     }
//   } else {
//     stuck_start_ts = 0;
//   }
// }

// // 7. Детектор zombie процессов
// static void check_zombie_processes(struct ttd_watch* watch,
//                                    struct tt_metrics* m,
//                                    struct ttd_writer* writer) {
//   if (m->procs_zombie > 10) {
//     struct tt_event_cell event = {
//         .timestamp = m->timestamp,
//         .event_code = TT_EVENT_PROC_ZOMBIE,
//         .severity = SEVERITY_WARNING,
//         .component = COMPONENT_PROC,
//         .metric_value = m->procs_zombie,
//         .threshold = 10,
//     };
//     ttd_writer_write_event(writer, &event);
//   }
// }

// // 8. Проверка заполнения диска
// static void check_disk_full(struct ttd_watch* watch, struct tt_metrics* m,
//                             struct ttd_writer* writer) {
//   if (m->du_total_bytes == 0)
//     return;

//   uint16_t usage_pct =
//       (uint16_t)(((m->du_total_bytes - m->du_free_bytes) * 10000) /
//                  m->du_total_bytes);

//   static uint16_t last_usage = 0;

//   if (usage_pct >= 9500 && last_usage < 9500) {
//     emit_threshold_event(writer, m, TT_EVENT_DISK_FULL_CRITICAL,
//                          SEVERITY_CRITICAL, usage_pct, 9500);
//   } else if (usage_pct >= 9000 && last_usage < 9000) {
//     emit_threshold_event(writer, m, TT_EVENT_DISK_FULL_HIGH,
//     SEVERITY_WARNING,
//                          usage_pct, 9000);
//   }

//   last_usage = usage_pct;
// }

// // 9. Проверка kernel taint
// static void check_kernel_taint(struct ttd_watch* watch, struct tt_metrics* m,
//                                struct ttd_writer* writer) {
//   if (m->kernel_tainted != 0) {
//     struct tt_event_cell event = {
//         .timestamp = m->timestamp,
//         .event_code = TT_EVENT_SYS_KERNEL_TAINT,
//         .severity = SEVERITY_WARNING,
//         .component = COMPONENT_SYS,
//         .metric_value = m->kernel_tainted,
//     };
//     ttd_writer_write_event(writer, &event);
//   }
// }

// // Универсальная функция получения имени процесса
// static int get_process_name(int pid, char* name, size_t len) {
//   char path[64];
//   snprintf(path, sizeof(path), "/proc/%d/comm", pid);

//   FILE* f = fopen(path, "r");
//   if (!f)
//     return -1;

//   if (fgets(name, len, f)) {
//     // Убираем перевод строки
//     size_t slen = strlen(name);
//     if (slen > 0 && name[slen - 1] == '\n') {
//       name[slen - 1] = '\0';
//     }
//   }
//   fclose(f);
//   return 0;
// }

// // Функция поиска процесса по PID и получения его cmdline
// static int get_process_cmdline(int pid, char* cmdline, size_t len) {
//   char path[64];
//   snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

//   FILE* f = fopen(path, "r");
//   if (!f)
//     return -1;

//   size_t n = fread(cmdline, 1, len - 1, f);
//   if (n > 0) {
//     cmdline[n] = '\0';
//     // Заменяем \0 на пробелы для читаемости
//     for (size_t i = 0; i < n - 1; i++) {
//       if (cmdline[i] == '\0')
//         cmdline[i] = ' ';
//     }
//   }
//   fclose(f);
//   return 0;
// }