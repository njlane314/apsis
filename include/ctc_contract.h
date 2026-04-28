#ifndef CTC_CONTRACT_H
#define CTC_CONTRACT_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CTC_MAX_RULES
#define CTC_MAX_RULES 256
#endif

#ifndef CTC_MAX_NAME
#define CTC_MAX_NAME 128
#endif

#ifndef CTC_LINE_MAX
#define CTC_LINE_MAX 512
#endif

typedef enum ctc_level {
    CTC_INFO = 0,
    CTC_WARN = 1,
    CTC_ERROR = 2
} ctc_level;

typedef enum ctc_op {
    CTC_GT = 0,
    CTC_GTE,
    CTC_LT,
    CTC_LTE,
    CTC_EQ,
    CTC_NEQ,
    CTC_STALE
} ctc_op;

typedef struct ctc_rule {
    char key[CTC_MAX_NAME];
    ctc_op op;
    double threshold;
    ctc_level level;
    char event_id[CTC_MAX_NAME];
    double cooldown_seconds;
    long long last_emit_time;
    int has_last_emit_time;
    int seen;
} ctc_rule;

typedef struct ctc_ctx {
    ctc_rule rules[CTC_MAX_RULES];
    size_t rule_count;
    unsigned long samples_seen;
    unsigned long events_emitted;
    unsigned long info_count;
    unsigned long warn_count;
    unsigned long error_count;
} ctc_ctx;

typedef void (*ctc_event_fn)(const char *line, void *user);

void ctc_init(ctc_ctx *ctx);
int ctc_add_rule(ctc_ctx *ctx, const char *key, ctc_op op, double threshold,
                 ctc_level level, const char *event_id);
int ctc_add_stale_rule(ctc_ctx *ctx, const char *key, double stale_seconds,
                       ctc_level level, const char *event_id);
int ctc_load_rules_file(ctc_ctx *ctx, const char *path, char *err, size_t err_cap);
int ctc_load_rules_stream(ctc_ctx *ctx, FILE *stream, const char *label,
                          char *err, size_t err_cap);
int ctc_parse_rule_line(ctc_ctx *ctx, const char *line, unsigned long line_no,
                        const char *label, char *err, size_t err_cap);
int ctc_sample(ctc_ctx *ctx, const char *key, double value,
               char *event, size_t event_cap);
int ctc_emit_missing_stale(ctc_ctx *ctx, ctc_event_fn fn, void *user);
int ctc_format_event(const ctc_rule *rule, double value,
                     char *out, size_t out_cap);

const char *ctc_level_name(ctc_level level);
const char *ctc_op_name(ctc_op op);
const char *ctc_rule_op_name(const ctc_rule *rule);
int ctc_parse_level(const char *s, ctc_level *out);
int ctc_parse_op(const char *s, ctc_op *out);
int ctc_parse_duration(const char *s, double *out);
int ctc_rule_matches(const ctc_rule *rule, double value);
int ctc_valid_name(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* CTC_CONTRACT_H */
