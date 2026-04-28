#ifndef CTC_DWELL_H
#define CTC_DWELL_H

#include "ctc_contract.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CTC_DWELL_MAX_WATCHES
#define CTC_DWELL_MAX_WATCHES 128
#endif

typedef enum ctc_dwell_type {
    CTC_DWELL_I32 = 0,
    CTC_DWELL_U32,
    CTC_DWELL_I64,
    CTC_DWELL_U64,
    CTC_DWELL_F32,
    CTC_DWELL_F64,
    CTC_DWELL_BOOL,
    CTC_DWELL_READER
} ctc_dwell_type;

typedef double (*ctc_dwell_reader_fn)(void *user);
typedef void (*ctc_dwell_event_fn)(const char *line, void *user);
typedef void (*ctc_dwell_sample_fn)(const char *key, double value, void *user);

typedef struct ctc_dwell_watch_rec {
    char key[CTC_MAX_NAME];
    ctc_dwell_type type;
    const volatile void *addr;
    ctc_dwell_reader_fn reader;
    void *reader_user;
    double last_value;
    unsigned long sample_count;
} ctc_dwell_watch_t;

typedef struct ctc_dwell_ctx {
    ctc_ctx contracts;
    ctc_dwell_watch_t watches[CTC_DWELL_MAX_WATCHES];
    size_t watch_count;
    unsigned long tick_count;
    unsigned long sample_count;
    ctc_dwell_event_fn event_cb;
    void *event_user;
    ctc_dwell_sample_fn sample_cb;
    void *sample_user;
} ctc_dwell_ctx;

void ctc_dwell_init(ctc_dwell_ctx *ctx);
void ctc_dwell_set_event_callback(ctc_dwell_ctx *ctx,
                                  ctc_dwell_event_fn fn,
                                  void *user);
void ctc_dwell_set_sample_callback(ctc_dwell_ctx *ctx,
                                   ctc_dwell_sample_fn fn,
                                   void *user);

int ctc_dwell_load_rules(ctc_dwell_ctx *ctx,
                         const char *path,
                         char *err,
                         size_t err_cap);
int ctc_dwell_add_rule(ctc_dwell_ctx *ctx,
                       const char *key,
                       ctc_op op,
                       double threshold,
                       ctc_level level,
                       const char *event_id);
int ctc_dwell_add_stale_rule(ctc_dwell_ctx *ctx,
                             const char *key,
                             double stale_seconds,
                             ctc_level level,
                             const char *event_id);

int ctc_dwell_watch(ctc_dwell_ctx *ctx,
                    const char *key,
                    ctc_dwell_type type,
                    const volatile void *addr);
int ctc_dwell_watch_reader(ctc_dwell_ctx *ctx,
                           const char *key,
                           ctc_dwell_reader_fn reader,
                           void *user);
int ctc_dwell_watch_i32(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile int32_t *addr);
int ctc_dwell_watch_u32(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint32_t *addr);
int ctc_dwell_watch_i64(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile int64_t *addr);
int ctc_dwell_watch_u64(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint64_t *addr);
int ctc_dwell_watch_f32(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile float *addr);
int ctc_dwell_watch_f64(ctc_dwell_ctx *ctx,
                        const char *key,
                        const volatile double *addr);
int ctc_dwell_watch_bool(ctc_dwell_ctx *ctx,
                         const char *key,
                         const volatile int *addr);

int ctc_dwell_sample_value(ctc_dwell_ctx *ctx,
                           const char *key,
                           double value);
int ctc_dwell_tick(ctc_dwell_ctx *ctx);
int ctc_dwell_find_watch(const ctc_dwell_ctx *ctx, const char *key);
const char *ctc_dwell_type_name(ctc_dwell_type type);

#ifdef __cplusplus
}
#endif

#endif /* CTC_DWELL_H */
