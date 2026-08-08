#ifndef TTR_READER_H
#define TTR_READER_H

#include <stddef.h>
#include <stdint.h>

#include "layout.h"

enum {
  TTR_READER_OK = 0,
  TTR_READER_ERR_READ = -1,
  TTR_READER_ERR_MAGIC = -2,
  TTR_READER_ERR_VERSION = -3,
  TTR_READER_ERR_NODATA = -4,
  TTR_READER_ERR_INVALID = -5,
  TTR_READER_ERR_STALE = -6
};

struct ttr_reader {
  void* addr;
  size_t size;
  struct ttr_meta* l1_meta;
  struct ttr_meta* l2_meta;
  struct ttr_meta* l3_meta;
  struct ttr_meta* le_meta;
  uint8_t* l1_data;
  uint8_t* l2_data;
  uint8_t* l3_data;
  uint8_t* le_data;
};

int ttr_reader_open(struct ttr_reader* ctx, const char* path);
int ttr_reader_get_latest(struct ttr_reader* ctx, void* out, size_t out_size);
int ttr_reader_get_history(struct ttr_reader* ctx, int level, void* out,
                           size_t out_size, int count);
void ttr_reader_close(struct ttr_reader* ctx);
const char* ttr_reader_strerror(int errcode);

#endif /* TTR_READER_H */
