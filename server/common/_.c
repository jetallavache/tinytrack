// Вы абсолютно правы — извлечение каждого поля в отдельный массив было бы
// крайне неэффективно
// Нужно за один проход по данным применить разные стратегии агрегации
// к разным полям.

// 1. Обновлённая структура `ttr_classify_actions`

// common/classify_actions.h

/**
 * Контекст-аккумулятор для агрегации одного поля.
 * Каждая функция агрегации получает свой экземпляр.
 */
typedef struct ttr_field_accumulator ttr_field_accumulator;

/**
 * Функция агрегации с состоянием.
 * @param acc     - аккумулятор (создаётся один раз до цикла)
 * @param value   - указатель на текущее значение поля
 * @param count   - номер итерации (0-based) или общее количество
 * @param is_last - флаг последней итерации
 * @param out     - куда записать финальный результат (только при is_last=true)
 */
typedef void (*ttr_field_aggregate_fn)(struct ttr_field_accumulator* acc,
                                       const void* value, uint32_t count,
                                       uint32_t index, bool is_last, void* out);

/**
 * Функция для создания аккумулятора (выделяет память или инициализирует)
 */
typedef struct ttr_field_accumulator* (*ttr_acc_create_fn)(void);

/**
 * Функция для удаления аккумулятора
 */
typedef void (*ttr_acc_destroy_fn)(struct ttr_field_accumulator* acc);

/**
 * Описание стратегии агрегации для одного поля
 */
typedef struct {
  const char* name;                   // Для отладки
  size_t field_offset;                // offsetof(struct tt_metrics, поле)
  size_t field_size;                  // sizeof(поле)
  ttr_acc_create_fn create;           // Создать аккумулятор
  ttr_field_aggregate_fn accumulate;  // Агрегировать значение
  ttr_acc_destroy_fn destroy;         // Удалить аккумулятор
} ttr_field_aggregator;

/**
 * Коллекция агрегаторов для всех полей
 */
typedef struct {
  ttr_field_aggregator* aggregators;  // Массив агрегаторов
  uint32_t count;                     // Количество агрегаторов
} ttr_classify_actions;

// 2. Базовые реализации аккумуляторов

// common/field_aggregators.c

#include <stdlib.h>
#include <string.h>

#include "common/classify_actions.h"

/* ---------- EMA для uint16_t ---------- */
typedef struct {
  double ema;
  double alpha;  // 0..1, например 0.3
  bool initialized;
} ema_u16_acc_t;

static struct ttr_field_accumulator* ema_u16_create(void) {
  ema_u16_acc_t* acc = calloc(1, sizeof(ema_u16_acc_t));
  acc->alpha = 0.3;  // По умолчанию
  return (struct ttr_field_accumulator*)acc;
}

static void ema_u16_accumulate(struct ttr_field_accumulator* acc_ptr,
                               const void* value, uint32_t count,
                               uint32_t index, bool is_last, void* out) {
  ema_u16_acc_t* acc = (ema_u16_acc_t*)acc_ptr;
  uint16_t val = *(const uint16_t*)value;

  if (!acc->initialized) {
    acc->ema = (double)val;
    acc->initialized = true;
  } else {
    // Адаптивный alpha на основе count
    if (index == 1) {
      acc->alpha = 2.0 / (double)(count + 1);
    }
    acc->ema = acc->alpha * (double)val + (1.0 - acc->alpha) * acc->ema;
  }

  if (is_last && out) {
    *(uint16_t*)out = (uint16_t)(acc->ema + 0.5);
  }
}

static void ema_u16_destroy(struct ttr_field_accumulator* acc) {
  free(acc);
}

/* ---------- Среднее для uint16_t ---------- */
typedef struct {
  uint64_t sum;
  uint32_t count;
} avg_u16_acc_t;

static struct ttr_field_accumulator* avg_u16_create(void) {
  return (struct ttr_field_accumulator*)calloc(1, sizeof(avg_u16_acc_t));
}

static void avg_u16_accumulate(struct ttr_field_accumulator* acc_ptr,
                               const void* value, uint32_t count,
                               uint32_t index, bool is_last, void* out) {
  avg_u16_acc_t* acc = (avg_u16_acc_t*)acc_ptr;
  acc->sum += *(const uint16_t*)value;
  acc->count++;

  if (is_last && out && acc->count > 0) {
    *(uint16_t*)out = (uint16_t)(acc->sum / acc->count);
  }
}

static void avg_u16_destroy(struct ttr_field_accumulator* acc) {
  free(acc);
}

/* ---------- Побитовое OR для флагов ---------- */
typedef struct {
  uint16_t flags;
} or_flags_acc_t;

static struct ttr_field_accumulator* or_flags_create(void) {
  return (struct ttr_field_accumulator*)calloc(1, sizeof(or_flags_acc_t));
}

static void or_flags_accumulate(struct ttr_field_accumulator* acc_ptr,
                                const void* value, uint32_t count,
                                uint32_t index, bool is_last, void* out) {
  or_flags_acc_t* acc = (or_flags_acc_t*)acc_ptr;
  acc->flags |= *(const uint16_t*)value;

  if (is_last && out) {
    *(uint16_t*)out = acc->flags;
  }
}

static void or_flags_destroy(struct ttr_field_accumulator* acc) {
  free(acc);
}

/* ---------- Последнее значение (для timestamp) ---------- */
typedef struct {
  uint64_t last_value;
  bool has_value;
} latest_u64_ctx;

static struct ttr_field_accumulator* latest_u64_create(void) {
  return (struct ttr_field_accumulator*)calloc(1, sizeof(latest_u64_acc_t));
}

static void latest_u64_accumulate(struct ttr_field_accumulator* acc_ptr,
                                  const void* value, uint32_t count,
                                  uint32_t index, bool is_last, void* out) {
  latest_u64_acc_t* acc = (latest_u64_acc_t*)acc_ptr;
  uint64_t val = *(const uint64_t*)value;

  // Берём максимальный timestamp (последний по времени)
  if (!acc->has_value || val > acc->last_value) {
    acc->last_value = val;
    acc->has_value = true;
  }

  if (is_last && out) {
    *(uint64_t*)out = acc->last_value;
  }
}

static void latest_u64_destroy(struct ttr_field_accumulator* acc) {
  free(acc);
}

// 3. Новая эффективная функция агрегации

// common/metrics.c

void tt_metrics_aggregate(const void* samples, uint32_t count, size_t cell_size,
                          void* out, const void* actions_ptr) {
  if (!samples || !out || count == 0)
    return;

  const ttr_classify_actions* actions =
      (const ttr_classify_actions*)actions_ptr;

  // Создаём аккумуляторы для каждого поля
  struct ttr_field_accumulator** accumulators = NULL;
  uint32_t num_fields = 0;

  if (actions && actions->aggregators && actions->count > 0) {
    num_fields = actions->count;
    accumulators = calloc(num_fields, sizeof(*accumulators));

    for (uint32_t i = 0; i < num_fields; i++) {
      if (actions->aggregators[i].create) {
        accumulators[i] = actions->aggregators[i].create();
      }
    }
  }

  // Единый проход по всем сэмплам
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* sample_base = (const uint8_t*)samples + i * cell_size;
    bool is_last = (i == count - 1);

    // Для каждого зарегистрированного поля вызываем его агрегатор
    for (uint32_t f = 0; f < num_fields; f++) {
      if (!accumulators[f])
        continue;

      const ttr_field_aggregator* agg = &actions->aggregators[f];
      const void* field_value = sample_base + agg->field_offset;

      agg->accumulate(accumulators[f], field_value, count, i, is_last, NULL);
    }
  }

  // Извлекаем результаты
  struct tt_metrics* result = (struct tt_metrics*)out;
  memset(result, 0, cell_size);

  for (uint32_t f = 0; f < num_fields; f++) {
    if (!accumulators[f])
      continue;

    const ttr_field_aggregator* agg = &actions->aggregators[f];
    void* result_field = (uint8_t*)result + agg->field_offset;

    // Вызываем последний раз с is_last=true для записи результата
    agg->accumulate(accumulators[f], NULL, count, count, true, result_field);
    agg->destroy(accumulators[f]);
  }

  free(accumulators);
}

// 4. Конфигурация в tinytd

// tinytd/writer.c

int ttd_writer_init(struct ttd_writer* ctx, struct ttd_config* cfg) {
  // Определяем массив агрегаторов для всех полей
  static ttr_field_aggregator metrics_aggregators[] = {
      {
          .name = "timestamp",
          .field_offset = offsetof(struct tt_metrics, timestamp),
          .field_size = sizeof(uint64_t),
          .create = latest_u64_create,
          .accumulate = latest_u64_accumulate,
          .destroy = latest_u64_destroy,
      },
      {
          .name = "cpu_usage_pct",
          .field_offset = offsetof(struct tt_metrics, cpu_usage_pct),
          .field_size = sizeof(uint16_t),
          .create = ema_u16_create,
          .accumulate = ema_u16_accumulate,
          .destroy = ema_u16_destroy,
      },
      {
          .name = "mem_usage_pct",
          .field_offset = offsetof(struct tt_metrics, mem_usage_pct),
          .field_size = sizeof(uint16_t),
          .create = avg_u16_create,
          .accumulate = avg_u16_accumulate,
          .destroy = avg_u16_destroy,
      },
      {
          .name = "cpu_state_flags",
          .field_offset = offsetof(struct tt_metrics, cpu_state_flags),
          .field_size = sizeof(uint16_t),
          .create = or_flags_create,
          .accumulate = or_flags_accumulate,
          .destroy = or_flags_destroy,
      },
      {
          .name = "mem_state_flags",
          .field_offset = offsetof(struct tt_metrics, mem_state_flags),
          .field_size = sizeof(uint16_t),
          .create = or_flags_create,
          .accumulate = or_flags_accumulate,
          .destroy = or_flags_destroy,
      },
      // ... все остальные поля
  };

  ttr_classify_actions field_actions = {
      .aggregators = metrics_aggregators,
      .count = sizeof(metrics_aggregators) / sizeof(metrics_aggregators[0]),
  };

  struct ttr_writer_config ring_cfg = {
      // ...
      .aggregate = tt_metrics_aggregate,
      .field_aggregators = &field_actions,
  };

  // ...
  return 0;
}

// Преимущества :

// 1. *
// *Один проход ** — все поля агрегируются одновременно 2. *
// *Независимость ** — `common / writer.h` не знает о `tt_metrics` 3. *
// *Расширяемость ** — новые поля добавляются только в конфигурацию 4. *
// *Эффективность ** — нет лишних копирований или циклов 5. *
// *Типобезопасность ** — каждый аккумулятор работает со своим типом
//     через `void *`

// Это сохраняет эффективность вашего исходного однопроходного алгоритма,
// но делает его полностью конфигурируемым.

//

/**
 * Converting counter values to rate (units/sec) based on timestamps
 */
// static uint32_t counter_to_rate_aggregate(const uint32_t* samples,
//                                           const uint64_t* timestamps,
//                                           uint32_t count) {
//   if (count < 2)
//     return 0;

//   uint64_t dt_sec = (timestamps[count - 1] - timestamps[0]) / 1000;
//   if (dt_sec == 0)
//     return 0;

//   uint32_t first = samples[0];
//   uint32_t last = samples[count - 1];
//   uint32_t delta;

//   if (last >= first) {
//     delta = last - first;
//   } else {
//     delta = (0xFFFFFFFF - first) + last + 1;
//   }

//   return (uint32_t)(delta / dt_sec);
// }

// /**
//  * Aggregation of one 2-bit group of flags per period.
//  * Classification by duration of hysteresis states.
//  */
// static uint8_t aggregate_state_flags(const uint16_t* flags, uint32_t count,
//                                      uint8_t shift, uint32_t interval_sec) {
//   if (count == 0)
//     return STATE_OK;

//   uint32_t ok = 0, warn = 0, crit = 0, reserved = 0;

//   for (uint32_t i = 0; i < count; i++) {
//     uint8_t state = (flags[i] >> shift) & 0x3;
//     switch (state) {
//       case STATE_OK:
//         ok++;
//         break;
//       case STATE_WARNING:
//         warn++;
//         break;
//       case STATE_CRITICAL:
//         crit++;
//         break;
//       default:
//         reserved++;
//         break;
//     }
//   }

//   uint32_t total_valid = ok + warn + crit;
//   if (total_valid == 0)
//     return STATE_OK;

//   uint32_t crit_threshold, warn_threshold;

//   if (interval_sec <= 60) {
//     crit_threshold = 30;  // 30% времени в CRITICAL → CRITICAL
//     warn_threshold = 20;  // 20% времени в WARNING → WARNING
//   } else {
//     crit_threshold = 50;  // 50% времени в CRITICAL → CRITICAL
//     warn_threshold = 40;  // 40% времени в WARNING → WARNING
//   }

//   uint32_t crit_pct = crit * 100 / total_valid;
//   uint32_t warn_pct = warn * 100 / total_valid;

//   if (crit_pct >= crit_threshold)
//     return STATE_CRITICAL;
//   if (crit_pct > 0 && crit_pct >= crit_threshold / 2)
//     return STATE_WARNING;
//   if (warn_pct >= warn_threshold)
//     return STATE_WARNING;

//   return STATE_OK;
// }

// // EMA для uint16_t с адаптивным α
// static uint16_t ema_aggregate_u16(uint16_t current_ema, const uint16_t*
// samples,
//                                   uint32_t count, uint32_t interval_sec) {
//   // Адаптивный α: чем больше интервал, тем меньше коэффициент
//   // α = 2 / (N + 1), где N — эффективное окно в секундах
//   uint32_t effective_window = interval_sec * 10;         // эмпирически
//   uint32_t alpha_x1000 = 2000 / (effective_window + 1);  // 2/(N+1) * 1000
//   if (alpha_x1000 < 1)
//     alpha_x1000 = 1;
//   if (alpha_x1000 > 500)
//     alpha_x1000 = 500;  // максимум 50%

//   uint32_t ema = current_ema * 1000;

//   for (uint32_t i = 0; i < count; i++) {
//     ema = (alpha_x1000 * samples[i] + (1000 - alpha_x1000) * ema) / 1000;
//   }

//   return (uint16_t)ema;
// }

// EMA для uint64_t (для дисковых метрик)
// static uint64_t ema_aggregate_u64(uint64_t current_ema, const uint64_t*
// samples,
//                                   uint32_t count, uint32_t interval_sec) {
//   uint32_t effective_window = interval_sec * 10;
//   uint32_t alpha_x1000 = 2000 / (effective_window + 1);
//   if (alpha_x1000 < 1)
//     alpha_x1000 = 1;
//   if (alpha_x1000 > 500)
//     alpha_x1000 = 500;

//   uint64_t ema = current_ema * 1000;

//   for (uint32_t i = 0; i < count; i++) {
//     ema = (alpha_x1000 * samples[i] + (1000 - alpha_x1000) * ema) / 1000;
//   }

//   return ema;
// }

// // Агрегация всех флагов из одной маски
// static uint16_t aggregate_all_flags(const uint16_t* flags, uint32_t count,
//                                     uint32_t interval_sec) {
//   uint16_t result = 0;

//   // 8 групп по 2 бита
//   for (uint8_t shift = 0; shift < 16; shift += 2) {
//     uint8_t aggregated =
//         aggregate_state_flags(flags, count, shift, interval_sec);
//     bit2_set(&result, shift, aggregated);
//   }

//   return result;
// }

// // Периодическая задача агрегации L1 → L2
// void aggregate_l1_to_l2(struct ring_buffer* l1, struct ring_buffer* l2) {
//   uint32_t l1_count = 60;  // l2_agg_interval_sec
//   struct tt_metrics_ex samples[60];
//   struct tt_metrics_ex prev_l2, new_l2;

//   // Читаем последние 60 сэмплов из L1
//   ring_buffer_read_last(l1, samples, l1_count);

//   // Получаем предыдущее значение L2 для EMA
//   ring_buffer_read_last(l2, &prev_l2, 1);

//   // Агрегируем
//   aggregate(samples, l1_count, 60, &prev_l2, &new_l2);

//   // Записываем в L2
//   ring_buffer_write(l2, &new_l2);
// }

// // Периодическая задача агрегации L2 → L3
// void aggregate_l2_to_l3(struct ring_buffer* l2, struct ring_buffer* l3) {
//   uint32_t l2_count =
//       60;  // l3_agg_interval_sec / l2_agg_interval_sec = 3600/60 = 60
//   struct tt_metrics_ex samples[60];
//   struct tt_metrics_ex prev_l3, new_l3;

//   ring_buffer_read_last(l2, samples, l2_count);
//   ring_buffer_read_last(l3, &prev_l3, 1);

//   aggregate(samples, l2_count, 3600, &prev_l3, &new_l3);

//   ring_buffer_write(l3, &new_l3);
// }

// // Агрегация массива сырых сэмплов в одну ячейку верхнего уровня
// void aggregate(
//     const struct tt_metrics_ex* samples, uint32_t count,
//     uint32_t agg_interval_sec,
//     struct tt_metrics_ex* prev_aggregated,  // может быть NULL
//     struct tt_metrics_ex* out) {
//   if (!samples || !out || count == 0)
//     return;

//   // Разделяем поля по типам для разной обработки
//   uint16_t* cpu_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* mem_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* load1_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* load5_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* load15_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* du_usage_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* nr_run_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* nr_total_arr = alloca(count * sizeof(uint16_t));
//   uint32_t* net_rx_arr = alloca(count * sizeof(uint32_t));
//   uint32_t* net_tx_arr = alloca(count * sizeof(uint32_t));
//   uint64_t* ts_arr = alloca(count * sizeof(uint64_t));
//   uint64_t* du_total_arr = alloca(count * sizeof(uint64_t));
//   uint64_t* du_free_arr = alloca(count * sizeof(uint64_t));
//   uint16_t* mem_flags_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* cpu_flags_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* net_flags_arr = alloca(count * sizeof(uint16_t));
//   uint16_t* io_flags_arr = alloca(count * sizeof(uint16_t));
//   uint8_t* crit_arr = alloca(count * sizeof(uint8_t));

//   for (uint32_t i = 0; i < count; i++) {
//     cpu_arr[i] = samples[i].cpu_usage;
//     mem_arr[i] = samples[i].mem_usage;
//     load1_arr[i] = samples[i].load_1min;
//     load5_arr[i] = samples[i].load_5min;
//     load15_arr[i] = samples[i].load_15min;
//     du_usage_arr[i] = samples[i].du_usage;
//     nr_run_arr[i] = samples[i].nr_running;
//     nr_total_arr[i] = samples[i].nr_total;
//     net_rx_arr[i] = samples[i].net_rx;
//     net_tx_arr[i] = samples[i].net_tx;
//     ts_arr[i] = samples[i].timestamp;
//     du_total_arr[i] = samples[i].du_total_bytes;
//     du_free_arr[i] = samples[i].du_free_bytes;
//     mem_flags_arr[i] = samples[i].mem_state_flags;
//     cpu_flags_arr[i] = samples[i].cpu_state_flags;
//     net_flags_arr[i] = samples[i].net_state_flags;
//     io_flags_arr[i] = samples[i].io_state_flags;
//     crit_arr[i] = samples[i].crit_count;
//   }

//   memset(out, 0, sizeof(*out));
//   out->timestamp = samples[count - 1].timestamp;

//   // === Gauge метрики: EMA ===
//   uint16_t prev_cpu = prev_aggregated ? prev_aggregated->cpu_usage :
//   cpu_arr[0]; uint16_t prev_mem = prev_aggregated ?
//   prev_aggregated->mem_usage : mem_arr[0]; uint16_t prev_load1 =
//       prev_aggregated ? prev_aggregated->load_1min : load1_arr[0];
//   uint16_t prev_load5 =
//       prev_aggregated ? prev_aggregated->load_5min : load5_arr[0];
//   uint16_t prev_load15 =
//       prev_aggregated ? prev_aggregated->load_15min : load15_arr[0];
//   uint16_t prev_du =
//       prev_aggregated ? prev_aggregated->du_usage : du_usage_arr[0];
//   uint16_t prev_nr_run =
//       prev_aggregated ? prev_aggregated->nr_running : nr_run_arr[0];
//   uint16_t prev_nr_total =
//       prev_aggregated ? prev_aggregated->nr_total : nr_total_arr[0];
//   uint64_t prev_du_total =
//       prev_aggregated ? prev_aggregated->du_total_bytes : du_total_arr[0];
//   uint64_t prev_du_free =
//       prev_aggregated ? prev_aggregated->du_free_bytes : du_free_arr[0];

//   out->cpu_usage =
//       ema_aggregate_u16(prev_cpu, cpu_arr, count, agg_interval_sec);
//   out->mem_usage =
//       ema_aggregate_u16(prev_mem, mem_arr, count, agg_interval_sec);
//   out->load_1min =
//       ema_aggregate_u16(prev_load1, load1_arr, count, agg_interval_sec);
//   out->load_5min =
//       ema_aggregate_u16(prev_load5, load5_arr, count, agg_interval_sec);
//   out->load_15min =
//       ema_aggregate_u16(prev_load15, load15_arr, count, agg_interval_sec);
//   out->du_usage =
//       ema_aggregate_u16(prev_du, du_usage_arr, count, agg_interval_sec);
//   out->nr_running =
//       ema_aggregate_u16(prev_nr_run, nr_run_arr, count, agg_interval_sec);
//   out->nr_total =
//       ema_aggregate_u16(prev_nr_total, nr_total_arr, count,
//       agg_interval_sec);
//   out->du_total_bytes =
//       (uint64_t)ema_aggregate_u64((uint32_t)(prev_du_total >> 10),
//       du_total_arr,
//                                   count, agg_interval_sec)
//       << 10;
//   out->du_free_bytes =
//       (uint64_t)ema_aggregate_u64((uint32_t)(prev_du_free >> 10),
//       du_free_arr,
//                                   count, agg_interval_sec)
//       << 10;

//   // === Counter метрики: Weighted Rate ===
//   out->net_rx = counter_to_rate_aggregate(net_rx_arr, ts_arr, count);
//   out->net_tx = counter_to_rate_aggregate(net_tx_arr, ts_arr, count);

//   // === Флаги состояний: Длительность ===
//   out->mem_state_flags =
//       aggregate_all_flags(mem_flags_arr, count, agg_interval_sec);
//   out->cpu_state_flags =
//       aggregate_all_flags(cpu_flags_arr, count, agg_interval_sec);
//   out->net_state_flags =
//       aggregate_all_flags(net_flags_arr, count, agg_interval_sec);
//   out->io_state_flags =
//       aggregate_all_flags(io_flags_arr, count, agg_interval_sec);

//   // === Критические события: сумма ===
//   uint32_t total_crit = 0;
//   for (uint32_t i = 0; i < count; i++) {
//     total_crit += crit_arr[i];
//   }
//   out->crit_count =
//       (uint8_t)(total_crit / count);  // среднее количество за период
// }

/**
 * EMA with adaptive coefficient depending on the level of aggregation
 *
 * The basic smoothing coefficient
 * For L2 (60 seconds): α = 0.15 — moderate smoothing
 * For L3 (3600 sec): α = 0.05 — strong smoothing
 */
// static uint16_t ema_aggregate_u16(uint16_t current_ema, const uint16_t*
// samples,
//                                   uint32_t count, uint32_t interval_sec) {
//   uint32_t alpha = 150 / interval_sec;
//   if (alpha < 1)
//     alpha = 1;
//   if (alpha > 50)
//     alpha = 50; /* max 50% */

//   uint32_t ema = current_ema * 100;

//   for (uint32_t i = 0; i < count; i++) {
//     ema = (alpha * samples[i] + (100 - alpha) * ema) / 100;
//   }

//   return (uint16_t)((ema + 50) / 100);
// }

// static uint64_t ema_u64(uint64_t sample) {

// }
/************************** */

// void agg_2(const void* samples, uint32_t count, size_t cell_size, void* out,
//            const ttr_classify_actions* actions) {
//   struct tt_metrics* result = (struct tt_metrics*)out;
//   const struct tt_metrics* metrics = (const struct tt_metrics*)samples;

//   // timestamp: берем последний
//   result->timestamp = metrics[count - 1].timestamp;

//   // cpu_usage_pct: используем универсальную функцию
//   if (actions && actions->avg_u16) {
//     actions->avg_u16(samples, count, offsetof(struct tt_metrics,
//     cpu_usage_pct),
//                      sizeof(uint16_t), &result->cpu_usage_pct);
//   } else {
//     // fallback: простое среднее
//     uint32_t sum = 0;
//     for (uint32_t i = 0; i < count; i++)
//       sum += metrics[i].cpu_usage_pct;
//     result->cpu_usage_pct = (uint16_t)(sum / count);
//   }

//   // mem_state_flags: используем специальный агрегатор для флагов
//   if (actions && actions->mode_flags) {
//     actions->mode_flags(samples, count,
//                         offsetof(struct tt_metrics, mem_state_flags),
//                         sizeof(uint16_t), &result->mem_state_flags);
//   }
//   // ... остальные поля
// }

/* Старая функция avg-агрегации
void tt_metrics_ex_agg_avg(const void* samples, uint32_t count,
                                       size_t cell_size, void* out) {
  if (!samples || !out || count == 0)
    return;

  uint64_t cpu = 0, mem = 0, net_rx = 0, net_tx = 0;
  uint64_t load1 = 0, load5 = 0, load15 = 0;
  uint64_t du_usage = 0, du_total = 0, du_free = 0;
  uint64_t last_ts = 0;
  uint32_t nr_running = 0, nr_total = 0;
  uint8_t crit_count = 0;

  uint16_t net_state_flags = 0, cpu_state_flags = 0, mem_state_flags = 0;

  for (uint32_t i = 0; i < count; i++) {
    const struct tt_metrics_ex* s =
        (const struct tt_metrics_ex*)((const uint8_t*)samples +
                                            i * cell_size);
    cpu += s->cpu_usage_pct;
    mem += s->mem_usage_pct;
    net_rx += s->net_rx_bytes;
    net_tx += s->net_tx_bytes;
    load1 += s->load_1min;
    load5 += s->load_5min;
    load15 += s->load_15min;
    nr_running += s->nr_running;
    nr_total += s->nr_total;
    // du_usage += s->du_usage;
    du_total += s->du_total_bytes;
    du_free += s->du_free_bytes;
    if (s->timestamp > last_ts)
      last_ts = s->timestamp;
    crit_count += s->crit_count;
  }

  struct tt_metrics_ex* agg = (struct tt_metrics_ex*)out;
  memset(agg, 0, sizeof(*agg));
  agg->timestamp = last_ts;
  agg->cpu_usage_pct = (uint16_t)(cpu / count);
  agg->mem_usage_pct = (uint16_t)(mem / count);
  agg->net_rx_bytes = (uint32_t)(net_rx / count);
  agg->net_tx_bytes = (uint32_t)(net_tx / count);
  agg->load_1min = (uint16_t)(load1 / count);
  agg->load_5min = (uint16_t)(load5 / count);
  agg->load_15min = (uint16_t)(load15 / count);
  agg->nr_running = nr_running / count;
  agg->nr_total = nr_total / count;
  //   agg->du_usage = (uint16_t)(du_usage / count);
  agg->du_total_bytes = du_total / count;
  agg->du_free_bytes = du_free / count;
  agg->crit_count = crit_count;
}
*/

/*

uint64_t cpu_pct = 0, mem_pct = 0, net_rx = 0, net_tx = 0;
  uint16_t cpu_flags = 0, mem_flags = 0, net_flags;
  uint64_t load1 = 0, load5 = 0, load15 = 0;
  uint64_t du_total = 0, du_free = 0;
  uint64_t last_ts = 0;
  uint32_t nr_running = 0, nr_total = 0;
  uint8_t crit_count = 0;


  uint16_t cpu_pct_ema = 0;
  uint16_t alpha = 1000 / count;
  if (alpha < 1)
    alpha = 1;
  if (alpha > 50)
    alpha = 50;


  for (uint32_t i = 0; i < count; i++) {
    const struct tt_metrics_ex* s =
        (const struct tt_metrics_ex*)((const uint8_t*)samples + i * cell_size);

    if (actions && actions->ema_u16) {
      actions->ema_u16(samples, count,
                       offsetof(struct tt_metrics_ex, s->cpu_usage_pct),
                       sizeof(uint16_t), &cpu_pct);
    }

    if (s->timestamp > last_ts)
      last_ts = s->timestamp;

    du_total += s->du_total_bytes;
    du_free += s->du_free_bytes;

    nr_running += s->nr_running;
    nr_total += s->nr_total;

    net_rx += s->net_rx_bytes;
    net_tx += s->net_tx_bytes;
    net_flags = s->net_state_flags;

    cpu_pct += s->cpu_usage_pct;
    cpu_flags = s->cpu_state_flags;

  // EMA for cpu usage, percent
    if (i == 0)
      cpu_pct_ema = s->cpu_usage_pct;
    cpu_pct_ema =
        (alpha * s->cpu_usage_pct + (100 - alpha) * cpu_pct_ema) / 100;


    mem_pct += s->mem_usage_pct;
    mem_flags = s->mem_state_flags;

    load1 += s->load_1min;
    load5 += s->load_5min;
    load15 += s->load_15min;

    crit_count += s->crit_count;

    printf("cpu %6hu | accumulate %8llu | ema %6llu\n", s->cpu_usage_pct,
           cpu_pct, cpu_pct_ema);
  }

  struct tt_metrics_ex* agg = (struct tt_metrics_ex*)out;
  memset(agg, 0, sizeof(*agg));
  agg->timestamp = last_ts;

  agg->du_total_bytes = du_total / count;
  agg->du_free_bytes = du_free / count;

  agg->nr_running = nr_running / count;
  agg->nr_total = nr_total / count;

  agg->net_rx_bytes = (uint32_t)(net_rx / count);
  agg->net_tx_bytes = (uint32_t)(net_tx / count);
  agg->net_state_flags = net_flags;

  agg->cpu_usage_pct = (uint16_t)(cpu_pct / count);
  agg->cpu_state_flags = cpu_flags;
  printf("cpu avg %3.2f% | ema %3.2f%\n", ((float)agg->cpu_usage_pct / 100),
         (((float)cpu_pct_ema + 50.0f) / 100));

  agg->mem_usage_pct = (uint16_t)(mem_pct / count);
  agg->mem_state_flags = mem_flags;

  agg->load_1min = (uint16_t)(load1 / count);
  agg->load_5min = (uint16_t)(load5 / count);
  agg->load_15min = (uint16_t)(load15 / count);

  agg->crit_count = crit_count;
  */