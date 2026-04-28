#include "ctc_dwell.h"

#include <string.h>

static void dwell_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

void ctc_dwell_init(ctc_dwell_ctx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctc_init(&ctx->contracts);
}

void ctc_dwell_set_event_callback(ctc_dwell_ctx *ctx,
                                  ctc_dwell_event_fn fn,
                                  void *user) {
    if (!ctx) return;
    ctx->event_cb = fn;
    ctx->event_user = user;
}

void ctc_dwell_set_sample_callback(ctc_dwell_ctx *ctx,
                                   ctc_dwell_sample_fn fn,
                                   void *user) {
    if (!ctx) return;
    ctx->sample_cb = fn;
    ctx->sample_user = user;
}

int ctc_dwell_load_rules(ctc_dwell_ctx *ctx,
                         const char *path,
                         char *err,
                         size_t err_cap) {
    if (!ctx) return -1;
    return ctc_load_rules_file(&ctx->contracts, path, err, err_cap);
}

int ctc_dwell_add_rule(ctc_dwell_ctx *ctx,
                       const char *key,
                       ctc_op op,
                       double threshold,
                       ctc_level level,
                       const char *event_id) {
    if (!ctx) return -1;
    return ctc_add_rule(&ctx->contracts, key, op, threshold, level, event_id);
}

int ctc_dwell_add_stale_rule(ctc_dwell_ctx *ctx,
                             const char *key,
                             double stale_seconds,
                             ctc_level level,
                             const char *event_id) {
    if (!ctx) return -1;
    return ctc_add_stale_rule(&ctx->contracts, key, stale_seconds,
                              level, event_id);
}

int ctc_dwell_find_watch(const ctc_dwell_ctx *ctx, const char *key) {
    size_t i;

    if (!ctx || !key) return -1;

    for (i = 0; i < ctx->watch_count; ++i) {
        if (strcmp(ctx->watches[i].key, key) == 0) return (int)i;
    }

    return -1;
}

int ctc_dwell_watch(ctc_dwell_ctx *ctx,
                    const char *key,
                    ctc_dwell_type type,
                    const volatile void *addr) {
    ctc_dwell_watch_t *watch;

    if (!ctx || !key || !addr) return -1;
    if (!ctc_valid_name(key)) return -1;
    if (ctx->watch_count >= CTC_DWELL_MAX_WATCHES) return -2;
    if (ctc_dwell_find_watch(ctx, key) >= 0) return -3;

    watch = &ctx->watches[ctx->watch_count++];
    memset(watch, 0, sizeof(*watch));
    dwell_copy(watch->key, key, sizeof(watch->key));
    watch->type = type;
    watch->addr = addr;
    return 0;
}

int ctc_dwell_watch_reader(ctc_dwell_ctx *ctx,
                           const char *key,
                           ctc_dwell_reader_fn reader,
                           void *user) {
    ctc_dwell_watch_t *watch;

    if (!ctx || !key || !reader) return -1;
    if (!ctc_valid_name(key)) return -1;
    if (ctx->watch_count >= CTC_DWELL_MAX_WATCHES) return -2;
    if (ctc_dwell_find_watch(ctx, key) >= 0) return -3;

    watch = &ctx->watches[ctx->watch_count++];
    memset(watch, 0, sizeof(*watch));
    dwell_copy(watch->key, key, sizeof(watch->key));
    watch->type = CTC_DWELL_READER;
    watch->reader = reader;
    watch->reader_user = user;
    return 0;
}

int ctc_dwell_watch_i32(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile int32_t *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_I32, addr);
}

int ctc_dwell_watch_u32(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint32_t *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_U32, addr);
}

int ctc_dwell_watch_i64(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile int64_t *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_I64, addr);
}

int ctc_dwell_watch_u64(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint64_t *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_U64, addr);
}

int ctc_dwell_watch_f32(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile float *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_F32, addr);
}

int ctc_dwell_watch_f64(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile double *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_F64, addr);
}

int ctc_dwell_watch_bool(ctc_dwell_ctx *ctx,
                         const char *key,
                         const volatile int *addr) {
    return ctc_dwell_watch(ctx, key, CTC_DWELL_BOOL, addr);
}

const char *ctc_dwell_type_name(ctc_dwell_type type) {
    switch (type) {
        case CTC_DWELL_I32: return "i32";
        case CTC_DWELL_U32: return "u32";
        case CTC_DWELL_I64: return "i64";
        case CTC_DWELL_U64: return "u64";
        case CTC_DWELL_F32: return "f32";
        case CTC_DWELL_F64: return "f64";
        case CTC_DWELL_BOOL: return "bool";
        case CTC_DWELL_READER: return "reader";
        default: return "unknown";
    }
}

static double ctc_dwell_read_watch(const ctc_dwell_watch_t *watch) {
    if (!watch) return 0.0;

    switch (watch->type) {
        case CTC_DWELL_I32:
            return (double)(*(const volatile int32_t *)watch->addr);
        case CTC_DWELL_U32:
            return (double)(*(const volatile uint32_t *)watch->addr);
        case CTC_DWELL_I64:
            return (double)(*(const volatile int64_t *)watch->addr);
        case CTC_DWELL_U64:
            return (double)(*(const volatile uint64_t *)watch->addr);
        case CTC_DWELL_F32:
            return (double)(*(const volatile float *)watch->addr);
        case CTC_DWELL_F64:
            return (double)(*(const volatile double *)watch->addr);
        case CTC_DWELL_BOOL:
            return (*(const volatile int *)watch->addr) ? 1.0 : 0.0;
        case CTC_DWELL_READER:
            return watch->reader ? watch->reader(watch->reader_user) : 0.0;
        default:
            return 0.0;
    }
}

int ctc_dwell_sample_value(ctc_dwell_ctx *ctx,
                           const char *key,
                           double value) {
    char event[CTC_LINE_MAX];
    int emitted;

    if (!ctx || !key) return -1;

    ctx->sample_count++;
    if (ctx->sample_cb) ctx->sample_cb(key, value, ctx->sample_user);

    emitted = ctc_sample(&ctx->contracts, key, value, event, sizeof(event));
    if (emitted > 0 && event[0] && ctx->event_cb) {
        ctx->event_cb(event, ctx->event_user);
    }

    return emitted;
}

int ctc_dwell_tick(ctc_dwell_ctx *ctx) {
    size_t i;
    int emitted = 0;

    if (!ctx) return -1;

    ctx->tick_count++;

    for (i = 0; i < ctx->watch_count; ++i) {
        ctc_dwell_watch_t *watch = &ctx->watches[i];
        double value = ctc_dwell_read_watch(watch);
        int rc;

        watch->last_value = value;
        watch->sample_count++;

        rc = ctc_dwell_sample_value(ctx, watch->key, value);
        if (rc < 0) return rc;
        emitted += rc;
    }

    return emitted;
}
