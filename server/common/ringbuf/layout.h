#ifndef TTR_LAYOUT_H
#define TTR_LAYOUT_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* Magic number for validation */
#define TTR_MAGIC 0x544D5452 /* "TMTR" */
#define TTR_VERSION 1

/* Block sizes */
#define TTR_HEADER_SIZE 256
#define TTR_CONSUMER_TABLE_SIZE 2048
#define TTR_META_SIZE 64
#define TTR_MAX_CONSUMERS 32

/* Main file header - 256 bytes */
struct ttr_header {
  uint32_t magic;    /* TTR_MAGIC */
  uint32_t version;  /* TTR_VERSION */
  uint32_t checksum; /* Adler32 of the entire shadow file (0 = disabled) */
  uint64_t last_update_ts;       /* Timestamp of last update (heartbeat) */
  uint64_t last_shadow_sync_ts;  /* Timestamp of last shadow sync */
  uint32_t writer_pid;           /* PID of the writer process */
  uint32_t num_consumers;        /* Number of active consumers */
  uint32_t interval_ms;          /* L1 collection interval (ms) */
  uint32_t l2_agg_interval_ms;   /* L1→L2 aggregation interval (ms) */
  uint32_t l3_agg_interval_ms;   /* L2→L3 aggregation interval (ms) */
  uint32_t le_check_interval_ms; /* LE check interval (ms) */
  uint8_t padding[204];          /* Padding to 256 bytes */
};

/* Consumer record - 64 bytes */
struct ttr_consumer {
  uint32_t consumer_id;   /* ID consumer */
  uint32_t pid;           /* Process PID */
  uint32_t read_index_l1; /* Read position in L1 */
  uint32_t read_index_l2; /* Read position in L2 */
  uint32_t read_index_l3; /* Read position in L3 */
  uint32_t read_index_le; /* Read position in LE */
  uint64_t last_seen_ts;  /* Last activity timestamp */
  uint32_t flags;         /* Flags */
  uint8_t padding[28];    /* Padding to 64 bytes */
};

/* Consumer table */
struct ttr_consumer_table {
  struct ttr_consumer entries[TTR_MAX_CONSUMERS];
};

/* Ring buffer metadata - 64 bytes */
struct ttr_meta {
  _Atomic uint32_t seq;  /* Sequence counter for seqlock */
  _Atomic uint32_t head; /* Write position (next slot to write) */
  uint32_t _reserved;    /* Reserved; always 0. Writer uses head-based indexing;
                            no per-consumer read cursor is maintained. */
  uint32_t capacity;     /* Capacity in elements */
  uint32_t cell_size;    /* Size of one element */
  uint64_t first_ts;     /* Timestamp of the first element */
  uint64_t last_ts;      /* Timestamp of the last element */
  uint32_t flags;        /* Flags */
  uint8_t padding[24];   /* Padding to 64 bytes */
};

/* Offset calculation */
static inline size_t ttr_layout_l1_offset(void) {
  return TTR_HEADER_SIZE + TTR_CONSUMER_TABLE_SIZE + TTR_META_SIZE;
}

static inline size_t ttr_layout_l2_meta_offset(size_t l1_capacity,
                                               size_t cell_size) {
  return ttr_layout_l1_offset() + l1_capacity * cell_size;
}

static inline size_t ttr_layout_l2_offset(size_t l1_capacity,
                                          size_t cell_size) {
  return ttr_layout_l2_meta_offset(l1_capacity, cell_size) + TTR_META_SIZE;
}

static inline size_t ttr_layout_l3_meta_offset(size_t l1_capacity,
                                               size_t l2_capacity,
                                               size_t cell_size) {
  return ttr_layout_l2_offset(l1_capacity, cell_size) + l2_capacity * cell_size;
}

static inline size_t ttr_layout_l3_offset(size_t l1_capacity,
                                          size_t l2_capacity,
                                          size_t cell_size) {
  return ttr_layout_l3_meta_offset(l1_capacity, l2_capacity, cell_size) +
         TTR_META_SIZE;
}

static inline size_t ttr_layout_le_meta_offset(size_t l1_capacity,
                                               size_t l2_capacity,
                                               size_t l3_capacity,
                                               size_t cell_size) {
  return ttr_layout_l3_offset(l1_capacity, l2_capacity, cell_size) +
         l3_capacity * cell_size;
}

static inline size_t ttr_layout_le_offset(size_t l1_capacity,
                                          size_t l2_capacity,
                                          size_t l3_capacity,
                                          size_t cell_size) {
  return ttr_layout_le_meta_offset(l1_capacity, l2_capacity, l3_capacity,
                                   cell_size) +
         TTR_META_SIZE;
}

static inline size_t tt_layout_total_size(size_t l1_cap, size_t l2_cap,
                                          size_t l3_cap, size_t le_cap,
                                          size_t cell_size) {
  return ttr_layout_le_offset(l1_cap, l2_cap, l3_cap, cell_size) +
         le_cap * cell_size;
}

#endif /* TTR_LAYOUT_H */
