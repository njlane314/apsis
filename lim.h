#ifndef LIM_H
#define LIM_H

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

#ifndef LIM_MAX_RULES
#define LIM_MAX_RULES 256
#endif

#ifndef LIM_MAX_NAME
#define LIM_MAX_NAME 64
#endif

typedef enum lim_level {
    LIM_LEVEL_INFO = 0,
    LIM_LEVEL_WARN = 1,
    LIM_LEVEL_ERROR = 2
} lim_level;

typedef enum lim_op {
    LIM_OP_GT = 0,
    LIM_OP_GTE,
    LIM_OP_LT,
    LIM_OP_LTE,
    LIM_OP_EQ,
    LIM_OP_NEQ
} lim_op;

typedef enum lim_rule_kind {
    LIM_RULE_LIMIT = 0,
    LIM_RULE_STALE = 1
} lim_rule_kind;

typedef enum lim_format {
    LIM_FORMAT_TEXT = 0,
    LIM_FORMAT_JSON = 1
} lim_format;

typedef struct lim_rule {
    char key[LIM_MAX_NAME];
    lim_rule_kind kind;
    lim_op op;
    double threshold;
    lim_level level;
    char event_id[LIM_MAX_NAME];
    double cooldown_seconds;
    long long last_emit_time;
    int has_last_emit_time;
    int seen;
} lim_rule;

typedef struct lim_ctx {
    lim_rule rules[LIM_MAX_RULES];
    size_t rule_count;
    unsigned long samples_seen;
    unsigned long events_emitted;
    unsigned long info_count;
    unsigned long warn_count;
    unsigned long error_count;
} lim_ctx;

void lim_init(lim_ctx *ctx);
int lim_add_rule(lim_ctx *ctx, const char *key, lim_op op, double threshold,
                 lim_level level, const char *event_id);
int lim_add_stale_rule(lim_ctx *ctx, const char *key, double stale_seconds,
                       lim_level level, const char *event_id);
int lim_load_rules_file(lim_ctx *ctx, const char *path, char *err, size_t err_cap);
int lim_sample(lim_ctx *ctx, const char *key, double value, char *out,
               size_t out_cap);
int lim_sample_format(lim_ctx *ctx, const char *key, double value,
                      lim_format format, char *out, size_t out_cap);

const char *lim_level_name(lim_level level);
const char *lim_op_name(lim_op op);
const char *lim_rule_op_name(const lim_rule *rule);
int lim_parse_level(const char *s, lim_level *out);
int lim_parse_op(const char *s, lim_op *out);
int lim_rule_matches(const lim_rule *rule, double value);

#ifdef __cplusplus
}
#endif

#endif /* LIM_H */
