#include "apsis_dwell.h"

#include <string.h>

static void dwell_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

void apsis_dwell_init(apsis_dwell_ctx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    apsis_init(&ctx->contracts);
}

void apsis_dwell_set_event_callback(apsis_dwell_ctx *ctx,
                                  apsis_dwell_event_fn fn,
                                  void *user) {
    if (!ctx) return;
    ctx->event_cb = fn;
    ctx->event_user = user;
}

void apsis_dwell_set_sample_callback(apsis_dwell_ctx *ctx,
                                   apsis_dwell_sample_fn fn,
                                   void *user) {
    if (!ctx) return;
    ctx->sample_cb = fn;
    ctx->sample_user = user;
}

int apsis_dwell_load_rules(apsis_dwell_ctx *ctx,
                         const char *path,
                         char *err,
                         size_t err_cap) {
    if (!ctx) return -1;
    return apsis_load_rules_file(&ctx->contracts, path, err, err_cap);
}

int apsis_dwell_add_rule(apsis_dwell_ctx *ctx,
                       const char *key,
                       apsis_op op,
                       double threshold,
                       apsis_level level,
                       const char *event_id) {
    if (!ctx) return -1;
    return apsis_add_rule(&ctx->contracts, key, op, threshold, level, event_id);
}

int apsis_dwell_add_stale_rule(apsis_dwell_ctx *ctx,
                             const char *key,
                             double stale_seconds,
                             apsis_level level,
                             const char *event_id) {
    if (!ctx) return -1;
    return apsis_add_stale_rule(&ctx->contracts, key, stale_seconds,
                              level, event_id);
}

int apsis_dwell_find_watch(const apsis_dwell_ctx *ctx, const char *key) {
    size_t i;

    if (!ctx || !key) return -1;

    for (i = 0; i < ctx->watch_count; ++i) {
        if (strcmp(ctx->watches[i].key, key) == 0) return (int)i;
    }

    return -1;
}

int apsis_dwell_watch(apsis_dwell_ctx *ctx,
                    const char *key,
                    apsis_dwell_type type,
                    const volatile void *addr) {
    apsis_dwell_watch_t *watch;

    if (!ctx || !key || !addr) return -1;
    if (!apsis_valid_name(key)) return -1;
    if (ctx->watch_count >= APSIS_DWELL_MAX_WATCHES) return -2;
    if (apsis_dwell_find_watch(ctx, key) >= 0) return -3;

    watch = &ctx->watches[ctx->watch_count++];
    memset(watch, 0, sizeof(*watch));
    dwell_copy(watch->key, key, sizeof(watch->key));
    watch->type = type;
    watch->addr = addr;
    return 0;
}

int apsis_dwell_watch_reader(apsis_dwell_ctx *ctx,
                           const char *key,
                           apsis_dwell_reader_fn reader,
                           void *user) {
    apsis_dwell_watch_t *watch;

    if (!ctx || !key || !reader) return -1;
    if (!apsis_valid_name(key)) return -1;
    if (ctx->watch_count >= APSIS_DWELL_MAX_WATCHES) return -2;
    if (apsis_dwell_find_watch(ctx, key) >= 0) return -3;

    watch = &ctx->watches[ctx->watch_count++];
    memset(watch, 0, sizeof(*watch));
    dwell_copy(watch->key, key, sizeof(watch->key));
    watch->type = APSIS_DWELL_READER;
    watch->reader = reader;
    watch->reader_user = user;
    return 0;
}

int apsis_dwell_watch_i32(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile int32_t *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_I32, addr);
}

int apsis_dwell_watch_u32(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint32_t *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_U32, addr);
}

int apsis_dwell_watch_i64(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile int64_t *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_I64, addr);
}

int apsis_dwell_watch_u64(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint64_t *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_U64, addr);
}

int apsis_dwell_watch_f32(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile float *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_F32, addr);
}

int apsis_dwell_watch_f64(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile double *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_F64, addr);
}

int apsis_dwell_watch_bool(apsis_dwell_ctx *ctx,
                         const char *key,
                         const volatile int *addr) {
    return apsis_dwell_watch(ctx, key, APSIS_DWELL_BOOL, addr);
}

const char *apsis_dwell_type_name(apsis_dwell_type type) {
    switch (type) {
        case APSIS_DWELL_I32: return "i32";
        case APSIS_DWELL_U32: return "u32";
        case APSIS_DWELL_I64: return "i64";
        case APSIS_DWELL_U64: return "u64";
        case APSIS_DWELL_F32: return "f32";
        case APSIS_DWELL_F64: return "f64";
        case APSIS_DWELL_BOOL: return "bool";
        case APSIS_DWELL_READER: return "reader";
        default: return "unknown";
    }
}

static double apsis_dwell_read_watch(const apsis_dwell_watch_t *watch) {
    if (!watch) return 0.0;

    switch (watch->type) {
        case APSIS_DWELL_I32:
            return (double)(*(const volatile int32_t *)watch->addr);
        case APSIS_DWELL_U32:
            return (double)(*(const volatile uint32_t *)watch->addr);
        case APSIS_DWELL_I64:
            return (double)(*(const volatile int64_t *)watch->addr);
        case APSIS_DWELL_U64:
            return (double)(*(const volatile uint64_t *)watch->addr);
        case APSIS_DWELL_F32:
            return (double)(*(const volatile float *)watch->addr);
        case APSIS_DWELL_F64:
            return (double)(*(const volatile double *)watch->addr);
        case APSIS_DWELL_BOOL:
            return (*(const volatile int *)watch->addr) ? 1.0 : 0.0;
        case APSIS_DWELL_READER:
            return watch->reader ? watch->reader(watch->reader_user) : 0.0;
        default:
            return 0.0;
    }
}

static int apsis_dwell_emit_event(const apsis_event *event, void *user) {
    apsis_dwell_ctx *ctx = (apsis_dwell_ctx *)user;
    char line[APSIS_LINE_MAX];

    if (!event || !ctx) return -1;
    if (!ctx->event_cb) return 0;
    if (apsis_format_event_record(event, line, sizeof(line)) != 0) return -1;
    ctx->event_cb(line, ctx->event_user);
    return 0;
}

int apsis_dwell_sample_value(apsis_dwell_ctx *ctx,
                           const char *key,
                           double value) {
    apsis_event_fn emit = NULL;
    int emitted;

    if (!ctx || !key) return -1;

    ctx->sample_count++;
    if (ctx->sample_cb) ctx->sample_cb(key, value, ctx->sample_user);

    if (ctx->event_cb) emit = apsis_dwell_emit_event;
    emitted = apsis_sample_each(&ctx->contracts, key, value,
                              apsis_now_seconds(), emit, ctx);

    return emitted;
}

int apsis_dwell_tick(apsis_dwell_ctx *ctx) {
    size_t i;
    int emitted = 0;

    if (!ctx) return -1;

    ctx->tick_count++;

    for (i = 0; i < ctx->watch_count; ++i) {
        apsis_dwell_watch_t *watch = &ctx->watches[i];
        double value = apsis_dwell_read_watch(watch);
        int rc;

        watch->last_value = value;
        watch->sample_count++;

        rc = apsis_dwell_sample_value(ctx, watch->key, value);
        if (rc < 0) return rc;
        emitted += rc;
    }

    {
        apsis_event_fn emit = ctx->event_cb ? apsis_dwell_emit_event : NULL;
        int stale = apsis_emit_stale_each(&ctx->contracts,
                                        apsis_now_seconds(),
                                        emit,
                                        ctx);
        if (stale < 0) return stale;
        emitted += stale;
    }

    return emitted;
}
