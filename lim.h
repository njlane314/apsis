#ifndef TL_H
#define TL_H

/*
 * lim.h - tiny telemetry limit checker
 *
 * Public-domain-sized C API; intended for small native programs and shell
 * pipelines. The implementation is in lim.c. Compile with:
 *
 *   cc -std=c99 -Wall -Wextra -O2 lim.c -o lim
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef TL_MAX_RULES
#define TL_MAX_RULES 256
#endif

#ifndef TL_MAX_NAME
#define TL_MAX_NAME 64
#endif

typedef enum tl_level {
    TL_LEVEL_INFO = 0,
    TL_LEVEL_WARN = 1,
    TL_LEVEL_ERROR = 2
} tl_level;

typedef enum tl_op {
    TL_OP_GT = 0,
    TL_OP_GTE,
    TL_OP_LT,
    TL_OP_LTE,
    TL_OP_EQ,
    TL_OP_NEQ
} tl_op;

typedef enum tl_rule_kind {
    TL_RULE_LIMIT = 0,
    TL_RULE_STALE = 1
} tl_rule_kind;

typedef struct tl_rule {
    char key[TL_MAX_NAME];
    tl_rule_kind kind;
    tl_op op;
    double threshold;
    tl_level level;
    char event_id[TL_MAX_NAME];
    double cooldown_seconds;
    long long last_emit_time;
    int has_last_emit_time;
    int seen;
} tl_rule;

typedef struct tl_ctx {
    tl_rule rules[TL_MAX_RULES];
    size_t rule_count;
    unsigned long samples_seen;
    unsigned long events_emitted;
    unsigned long info_count;
    unsigned long warn_count;
    unsigned long error_count;
} tl_ctx;

void tl_init(tl_ctx *ctx);
int tl_add_rule(tl_ctx *ctx, const char *key, tl_op op, double threshold,
                 tl_level level, const char *event_id);
int tl_add_stale_rule(tl_ctx *ctx, const char *key, double stale_seconds,
                       tl_level level, const char *event_id);
int tl_load_rules_file(tl_ctx *ctx, const char *path, char *err, size_t err_cap);
int tl_sample(tl_ctx *ctx, const char *key, double value, char *out,
               size_t out_cap);

const char *tl_level_name(tl_level level);
const char *tl_op_name(tl_op op);
const char *tl_rule_op_name(const tl_rule *rule);
int tl_parse_level(const char *s, tl_level *out);
int tl_parse_op(const char *s, tl_op *out);
int tl_rule_matches(const tl_rule *rule, double value);

#ifdef __cplusplus
}
#endif

#endif /* TL_H */
