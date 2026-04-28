#include "ctc.h"
#include "telemetry_ids.h"

#include <stdint.h>
#include <stdio.h>

static void print_event(const char *line, void *user) {
    (void)user;
    puts(line);
}

int main(void) {
    ctc_dwell_ctx ctx;
    volatile uint32_t queue_depth = 1201;
    int emitted;

    ctc_dwell_init(&ctx);
    ctc_dwell_set_event_callback(&ctx, print_event, NULL);

    if (ctc_dwell_add_rule(&ctx,
                           CTC_TEL_WORKER_QUEUE_DEPTH,
                           CTC_GT,
                           1000.0,
                           CTC_ERROR,
                           "queue.backpressure") != 0) {
        return 1;
    }

    if (ctc_dwell_watch_u32(&ctx,
                            CTC_TEL_WORKER_QUEUE_DEPTH,
                            &queue_depth) != 0) {
        return 1;
    }

    emitted = ctc_dwell_tick(&ctx);
    return emitted == 1 ? 0 : 1;
}
