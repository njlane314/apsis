#ifndef APSIS_CONTRACT_H
#define APSIS_CONTRACT_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APSIS_MAX_RULES
#define APSIS_MAX_RULES 256
#endif

#ifndef APSIS_MAX_NAME
#define APSIS_MAX_NAME 128
#endif

#ifndef APSIS_LINE_MAX
#define APSIS_LINE_MAX 512
#endif

typedef enum apsis_level {
    APSIS_INFO = 0,
    APSIS_WARN = 1,
    APSIS_ERROR = 2
} apsis_level;

typedef enum apsis_op {
    APSIS_GT = 0,
    APSIS_GTE,
    APSIS_LT,
    APSIS_LTE,
    APSIS_EQ,
    APSIS_NEQ,
    APSIS_STALE
} apsis_op;

typedef struct apsis_rule {
    char key[APSIS_MAX_NAME];
    apsis_op op;
    double threshold;
    apsis_level level;
    char event_id[APSIS_MAX_NAME];
    double cooldown_seconds;
    double last_emit_time;
    int has_last_emit_time;
    double last_seen_time;
    int has_last_seen_time;
} apsis_rule;

typedef struct apsis_ctx {
    apsis_rule rules[APSIS_MAX_RULES];
    size_t rule_count;
    unsigned long samples_seen;
    unsigned long events_emitted;
    unsigned long info_count;
    unsigned long warn_count;
    unsigned long error_count;
} apsis_ctx;

typedef struct apsis_event {
    char key[APSIS_MAX_NAME];
    apsis_op op;
    double threshold;
    apsis_level level;
    char event_id[APSIS_MAX_NAME];
    double value;
    double now_seconds;
} apsis_event;

typedef int (*apsis_event_fn)(const apsis_event *event, void *user);

void apsis_init(apsis_ctx *ctx);
double apsis_now_seconds(void);
int apsis_add_rule(apsis_ctx *ctx, const char *key, apsis_op op, double threshold,
                 apsis_level level, const char *event_id);
int apsis_add_stale_rule(apsis_ctx *ctx, const char *key, double stale_seconds,
                       apsis_level level, const char *event_id);
int apsis_load_rules_file(apsis_ctx *ctx, const char *path, char *err, size_t err_cap);
int apsis_load_rules_stream(apsis_ctx *ctx, FILE *stream, const char *label,
                          char *err, size_t err_cap);
int apsis_parse_rule_line(apsis_ctx *ctx, const char *line, unsigned long line_no,
                        const char *label, char *err, size_t err_cap);
int apsis_sample_each(apsis_ctx *ctx, const char *key, double value,
                    double now_seconds, apsis_event_fn emit, void *user);
int apsis_sample(apsis_ctx *ctx, const char *key, double value,
               char *event, size_t event_cap);
int apsis_emit_stale_each(apsis_ctx *ctx, double now_seconds,
                        apsis_event_fn emit, void *user);
int apsis_emit_missing_stale(apsis_ctx *ctx, apsis_event_fn fn, void *user);
int apsis_format_event(const apsis_rule *rule, double value,
                     char *out, size_t out_cap);
int apsis_format_event_record(const apsis_event *event, char *out, size_t out_cap);

const char *apsis_level_name(apsis_level level);
const char *apsis_op_name(apsis_op op);
const char *apsis_rule_op_name(const apsis_rule *rule);
int apsis_parse_level(const char *s, apsis_level *out);
int apsis_parse_op(const char *s, apsis_op *out);
int apsis_parse_duration(const char *s, double *out);
int apsis_rule_matches(const apsis_rule *rule, double value);
int apsis_valid_name(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* APSIS_CONTRACT_H */
