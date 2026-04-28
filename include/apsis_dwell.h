#ifndef APSIS_DWELL_H
#define APSIS_DWELL_H

#include "apsis_contract.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APSIS_DWELL_MAX_WATCHES
#define APSIS_DWELL_MAX_WATCHES 128
#endif

typedef enum apsis_dwell_type {
    APSIS_DWELL_I32 = 0,
    APSIS_DWELL_U32,
    APSIS_DWELL_I64,
    APSIS_DWELL_U64,
    APSIS_DWELL_F32,
    APSIS_DWELL_F64,
    APSIS_DWELL_BOOL,
    APSIS_DWELL_READER
} apsis_dwell_type;

typedef double (*apsis_dwell_reader_fn)(void *user);
typedef void (*apsis_dwell_event_fn)(const char *line, void *user);
typedef void (*apsis_dwell_sample_fn)(const char *key, double value, void *user);

typedef struct apsis_dwell_watch_rec {
    char key[APSIS_MAX_NAME];
    apsis_dwell_type type;
    const volatile void *addr;
    apsis_dwell_reader_fn reader;
    void *reader_user;
    double last_value;
    unsigned long sample_count;
} apsis_dwell_watch_t;

typedef struct apsis_dwell_ctx {
    apsis_ctx contracts;
    apsis_dwell_watch_t watches[APSIS_DWELL_MAX_WATCHES];
    size_t watch_count;
    unsigned long tick_count;
    unsigned long sample_count;
    apsis_dwell_event_fn event_cb;
    void *event_user;
    apsis_dwell_sample_fn sample_cb;
    void *sample_user;
} apsis_dwell_ctx;

void apsis_dwell_init(apsis_dwell_ctx *ctx);
void apsis_dwell_set_event_callback(apsis_dwell_ctx *ctx,
                                  apsis_dwell_event_fn fn,
                                  void *user);
void apsis_dwell_set_sample_callback(apsis_dwell_ctx *ctx,
                                   apsis_dwell_sample_fn fn,
                                   void *user);

int apsis_dwell_load_rules(apsis_dwell_ctx *ctx,
                         const char *path,
                         char *err,
                         size_t err_cap);
int apsis_dwell_add_rule(apsis_dwell_ctx *ctx,
                       const char *key,
                       apsis_op op,
                       double threshold,
                       apsis_level level,
                       const char *event_id);
int apsis_dwell_add_stale_rule(apsis_dwell_ctx *ctx,
                             const char *key,
                             double stale_seconds,
                             apsis_level level,
                             const char *event_id);

int apsis_dwell_watch(apsis_dwell_ctx *ctx,
                    const char *key,
                    apsis_dwell_type type,
                    const volatile void *addr);
int apsis_dwell_watch_reader(apsis_dwell_ctx *ctx,
                           const char *key,
                           apsis_dwell_reader_fn reader,
                           void *user);
int apsis_dwell_watch_i32(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile int32_t *addr);
int apsis_dwell_watch_u32(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint32_t *addr);
int apsis_dwell_watch_i64(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile int64_t *addr);
int apsis_dwell_watch_u64(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile uint64_t *addr);
int apsis_dwell_watch_f32(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile float *addr);
int apsis_dwell_watch_f64(apsis_dwell_ctx *ctx,
                        const char *key,
                        const volatile double *addr);
int apsis_dwell_watch_bool(apsis_dwell_ctx *ctx,
                         const char *key,
                         const volatile int *addr);

int apsis_dwell_sample_value(apsis_dwell_ctx *ctx,
                           const char *key,
                           double value);
int apsis_dwell_tick(apsis_dwell_ctx *ctx);
int apsis_dwell_find_watch(const apsis_dwell_ctx *ctx, const char *key);
const char *apsis_dwell_type_name(apsis_dwell_type type);

#ifdef __cplusplus
}
#endif

#endif /* APSIS_DWELL_H */
