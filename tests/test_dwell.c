#include "ctc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct test_events {
    unsigned long count;
    char last[CTC_LINE_MAX];
} test_events;

static void on_event(const char *line, void *user) {
    test_events *events = (test_events *)user;
    events->count++;
    snprintf(events->last, sizeof(events->last), "%s", line ? line : "");
}

int main(void) {
    ctc_dwell_ctx ctx;
    test_events events;
    volatile uint32_t queue_depth = 1201;
    volatile double frame_ms = 12.5;
    int rc;

    memset(&events, 0, sizeof(events));
    ctc_dwell_init(&ctx);
    ctc_dwell_set_event_callback(&ctx, on_event, &events);

    if (ctc_dwell_add_rule(&ctx,
                           "worker.queue.depth",
                           CTC_GT,
                           1000.0,
                           CTC_ERROR,
                           "queue.backpressure") != 0) {
        return 1;
    }
    if (ctc_dwell_add_rule(&ctx,
                           "renderer.frame.ms",
                           CTC_GT,
                           16.6,
                           CTC_WARN,
                           "frame.slow") != 0) {
        return 2;
    }

    if (ctc_dwell_watch_u32(&ctx,
                            "worker.queue.depth",
                            &queue_depth) != 0) {
        return 3;
    }
    if (ctc_dwell_watch_f64(&ctx,
                            "renderer.frame.ms",
                            &frame_ms) != 0) {
        return 4;
    }

    rc = ctc_dwell_tick(&ctx);
    if (rc != 1) return 5;
    if (events.count != 1) return 6;
    if (strstr(events.last, "queue.backpressure") == NULL) return 7;
    if (ctx.contracts.error_count != 1) return 8;
    if (ctx.contracts.warn_count != 0) return 9;

    frame_ms = 20.0;
    rc = ctc_dwell_tick(&ctx);
    if (rc != 2) return 10;
    if (events.count != 3) return 11;
    if (ctx.contracts.error_count != 2) return 12;
    if (ctx.contracts.warn_count != 1) return 13;

    return 0;
}
