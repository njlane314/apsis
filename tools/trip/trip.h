#ifndef TRIP_H
#define TRIP_H

/*
 * trip.h - tiny telemetry limit checker
 *
 * Public-domain-sized C API; intended for small native programs and shell
 * pipelines. The implementation is in trip.c. Compile with:
 *
 *   cc -std=c99 -Wall -Wextra -O2 trip.c -o trip
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef TRIP_MAX_RULES
#define TRIP_MAX_RULES 256
#endif

#ifndef TRIP_MAX_NAME
#define TRIP_MAX_NAME 64
#endif

typedef enum trip_level {
    TRIP_LEVEL_INFO = 0,
    TRIP_LEVEL_WARN = 1,
    TRIP_LEVEL_ERROR = 2
} trip_level;

typedef enum trip_op {
    TRIP_OP_GT = 0,
    TRIP_OP_GTE,
    TRIP_OP_LT,
    TRIP_OP_LTE,
    TRIP_OP_EQ,
    TRIP_OP_NEQ
} trip_op;

typedef enum trip_rule_kind {
    TRIP_RULE_LIMIT = 0,
    TRIP_RULE_STALE = 1
} trip_rule_kind;

typedef struct trip_rule {
    char key[TRIP_MAX_NAME];
    trip_rule_kind kind;
    trip_op op;
    double threshold;
    trip_level level;
    char event_id[TRIP_MAX_NAME];
    double cooldown_seconds;
    long long last_emit_time;
    int has_last_emit_time;
    int seen;
} trip_rule;

typedef struct trip_ctx {
    trip_rule rules[TRIP_MAX_RULES];
    size_t rule_count;
    unsigned long samples_seen;
    unsigned long events_emitted;
    unsigned long info_count;
    unsigned long warn_count;
    unsigned long error_count;
} trip_ctx;

void trip_init(trip_ctx *ctx);
int trip_add_rule(trip_ctx *ctx, const char *key, trip_op op, double threshold,
                 trip_level level, const char *event_id);
int trip_add_stale_rule(trip_ctx *ctx, const char *key, double stale_seconds,
                       trip_level level, const char *event_id);
int trip_load_rules_file(trip_ctx *ctx, const char *path, char *err, size_t err_cap);
int trip_sample(trip_ctx *ctx, const char *key, double value, char *out,
               size_t out_cap);

const char *trip_level_name(trip_level level);
const char *trip_op_name(trip_op op);
const char *trip_rule_op_name(const trip_rule *rule);
int trip_parse_level(const char *s, trip_level *out);
int trip_parse_op(const char *s, trip_op *out);
int trip_rule_matches(const trip_rule *rule, double value);

#ifdef __cplusplus
}
#endif

#endif /* TRIP_H */
