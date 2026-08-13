#ifndef TTD_PIPELINE_H
#define TTD_PIPELINE_H

#include "fetch.h"
#include "watch.h"
#include "writer.h"

/**
 * pipeline
 *
 * Что сделать, когда scheduler сказал SAMPLE?
 *
 * Ответственность:
 * - collect;
 * - analyze;
 * - persist;
 * - порядок выполнения этапов.
 */

 struct ttd_pipeline {
    struct ttd_fetch *fch;
    struct ttd_watch *watch;
    struct ttd_writer *writer;
 };

#endif /* TTD_PIPELINE_H */