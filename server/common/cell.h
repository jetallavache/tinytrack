#ifndef TT_CELL_H
#define TT_CELL_H

#define TT_CELL_SIZE 128

#define TT_CELL_HEADER_SIZE 4

enum tt_cell_type {
  TT_CELL_METRICS_L1 = 0x01,
  TT_CELL_METRICS_L2 = 0x02,
  TT_CELL_METRICS_L3 = 0x03,
  TT_CELL_EVENT = 0x10,
};

/**
 * The cell title and its metadata
 * @note 8 bytes
 */
struct tt_cell_header {
  uint8_t type;    /* enum tt_cell_type */
  uint8_t version; /* Version of the data structure */
  uint16_t flags;
  uint32_t serial;
}; /* Total 8 bytes */

#endif /* TT_CELL_H */