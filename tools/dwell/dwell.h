#ifndef DWELL_H
#define DWELL_H

/*
 * dwell.h - tiny internal variable contract checker
 *
 * Drop dwell.h and dwell.c into a native program, register internal variables,
 * attach contracts, and call dwell_tick() at safe sampling points. The same
 * source also builds a small CLI compatible with trip-style key=value streams.
 *
 * C99, no external dependencies, no heap allocation in the core.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DWELL_MAX_WATCHES
#define DWELL_MAX_WATCHES 128
#endif

#ifndef DWELL_MAX_CONTRACTS
#define DWELL_MAX_CONTRACTS 256
#endif

#ifndef DWELL_MAX_NAME
#define DWELL_MAX_NAME 64
#endif

#ifndef DWELL_MAX_ERROR
#define DWELL_MAX_ERROR 160
#endif

#ifndef DWELL_MAX_LINE
#define DWELL_MAX_LINE 512
#endif

/* Backward-compatible alias for older callers that sized event buffers. */
#ifndef DWELL_MAX_JSON
#define DWELL_MAX_JSON DWELL_MAX_LINE
#endif

typedef enum dwell_type {
    DWELL_I32 = 0,
    DWELL_U32,
    DWELL_I64,
    DWELL_U64,
    DWELL_F32,
    DWELL_F64,
    DWELL_BOOL,
    DWELL_READER
} dwell_type;

typedef enum dwell_level {
    DWELL_INFO = 0,
    DWELL_WARN = 1,
    DWELL_ERROR = 2
} dwell_level;

typedef enum dwell_op {
    DWELL_GT = 0,
    DWELL_GTE,
    DWELL_LT,
    DWELL_LTE,
    DWELL_EQ,
    DWELL_NEQ
} dwell_op;

typedef double (*dwell_reader_fn)(void *user);

typedef struct dwell_event {
    unsigned long tick;
    dwell_level level;
    char id[DWELL_MAX_NAME];
    char key[DWELL_MAX_NAME];
    dwell_op op;
    double threshold;
    double value;
    unsigned long fired_count;
} dwell_event;

typedef void (*dwell_event_fn)(const dwell_event *event,
                               const char *line,
                               void *user);

typedef void (*dwell_sample_fn)(const char *key,
                                double value,
                                void *user);

typedef struct dwell_watch_rec {
    char name[DWELL_MAX_NAME];
    dwell_type type;
    const volatile void *addr;
    dwell_reader_fn reader;
    void *reader_user;
    double last_value;
    unsigned long sample_count;
} dwell_watch_t;

typedef struct dwell_contract_rec {
    char key[DWELL_MAX_NAME];
    dwell_op op;
    double threshold;
    dwell_level level;
    char event_id[DWELL_MAX_NAME];
    unsigned long fired_count;
} dwell_contract_t;

typedef struct dwell_ctx {
    dwell_watch_t watches[DWELL_MAX_WATCHES];
    size_t watch_count;

    dwell_contract_t contracts[DWELL_MAX_CONTRACTS];
    size_t contract_count;

    unsigned long tick_count;
    unsigned long sample_count;
    unsigned long event_count;
    unsigned long info_count;
    unsigned long warn_count;
    unsigned long error_count;

    dwell_event_fn event_cb;
    void *event_user;

    dwell_sample_fn sample_cb;
    void *sample_user;
} dwell_ctx;

void dwell_init(dwell_ctx *ctx);
void dwell_set_event_callback(dwell_ctx *ctx, dwell_event_fn fn, void *user);
void dwell_set_sample_callback(dwell_ctx *ctx, dwell_sample_fn fn, void *user);

int dwell_watch(dwell_ctx *ctx,
                const char *name,
                dwell_type type,
                const volatile void *addr);

int dwell_watch_reader(dwell_ctx *ctx,
                       const char *name,
                       dwell_reader_fn reader,
                       void *user);

int dwell_watch_i32(dwell_ctx *ctx, const char *name, const volatile int32_t *addr);
int dwell_watch_u32(dwell_ctx *ctx, const char *name, const volatile uint32_t *addr);
int dwell_watch_i64(dwell_ctx *ctx, const char *name, const volatile int64_t *addr);
int dwell_watch_u64(dwell_ctx *ctx, const char *name, const volatile uint64_t *addr);
int dwell_watch_f32(dwell_ctx *ctx, const char *name, const volatile float *addr);
int dwell_watch_f64(dwell_ctx *ctx, const char *name, const volatile double *addr);
int dwell_watch_bool(dwell_ctx *ctx, const char *name, const volatile int *addr);

int dwell_add_contract(dwell_ctx *ctx,
                       const char *key,
                       dwell_op op,
                       double threshold,
                       dwell_level level,
                       const char *event_id);

/* Parse: <key> <op> <number> <level> <event_id>
 * Example: imu.temperature_c > 85 warn imu.overtemp
 */
int dwell_contract(dwell_ctx *ctx,
                   const char *rule,
                   char *err,
                   size_t err_cap);

int dwell_load_contracts_file(dwell_ctx *ctx,
                              const char *path,
                              char *err,
                              size_t err_cap);

/* Sample all registered variables, evaluate matching contracts, and emit events.
 * Returns the number of events emitted by this tick, or a negative error code.
 */
int dwell_tick(dwell_ctx *ctx);

/* Helper for stream input, tests, or one-off checks. */
int dwell_sample_value(dwell_ctx *ctx, const char *key, double value);

/* Format helpers. Events use the same tab-separated field order as trip:
 * <level> <event_id> <key> <op> <threshold> <value>
 */
int dwell_format_event(const dwell_event *event, char *out, size_t out_cap);
int dwell_format_sample(const char *key, double value, char *out, size_t out_cap);

const char *dwell_type_name(dwell_type type);
const char *dwell_level_name(dwell_level level);
const char *dwell_op_name(dwell_op op);
int dwell_parse_level(const char *s, dwell_level *out);
int dwell_parse_op(const char *s, dwell_op *out);
int dwell_contract_matches(const dwell_contract_t *contract, double value);
int dwell_find_watch(const dwell_ctx *ctx, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DWELL_H */
