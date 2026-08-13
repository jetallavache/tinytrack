#include "pipeline.h"

#include "common/metrics.h"

int ttd_pipeline_sample(struct ttd_pipeline *pip)
{
    struct tt_metrics m = {0};

    // if (ttd_fetch_collect(pip->fch, &m) < 0)
    //     return -1;

    // ttd_watch_metrics(pip->watch, &m);

    // if (ttd_writer_write_l1(pip->writer, &m) < 0)
    //     return -1;

    return 0;
}