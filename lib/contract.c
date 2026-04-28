#define _POSIX_C_SOURCE 200809L

#include "apsis_contract.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void apsis_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static char *apsis_ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static void apsis_rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        --n;
    }
}

static void apsis_strip_comment(char *s) {
    char *p = strchr(s, '#');
    if (p) *p = '\0';
}

static void apsis_set_err(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap) apsis_copy(err, msg, err_cap);
}

static const char *apsis_label(const char *label) {
    return (label && *label) ? label : "rules";
}

double apsis_now_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    }
#endif
    return (double)time(NULL);
}

int apsis_valid_name(const char *s) {
    size_t n;
    size_t i;

    if (!s || !*s) return 0;
    n = strlen(s);
    if (n >= APSIS_MAX_NAME) return 0;

    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' ||
              c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }

    return 1;
}

int apsis_parse_duration(const char *s, double *out) {
    char *end = NULL;
    double value;
    double factor = 1.0;

    if (!s || !out || !*s) return -1;

    errno = 0;
    value = strtod(s, &end);
    if (errno != 0 || end == s || value < 0.0) return -1;

    if (strcmp(end, "") == 0 || strcmp(end, "s") == 0) {
        factor = 1.0;
    } else if (strcmp(end, "ms") == 0) {
        factor = 0.001;
    } else if (strcmp(end, "m") == 0) {
        factor = 60.0;
    } else if (strcmp(end, "h") == 0) {
        factor = 3600.0;
    } else {
        return -1;
    }

    *out = value * factor;
    return 0;
}

const char *apsis_level_name(apsis_level level) {
    switch (level) {
        case APSIS_INFO: return "info";
        case APSIS_WARN: return "warn";
        case APSIS_ERROR: return "error";
        default: return "unknown";
    }
}

const char *apsis_op_name(apsis_op op) {
    switch (op) {
        case APSIS_GT: return ">";
        case APSIS_GTE: return ">=";
        case APSIS_LT: return "<";
        case APSIS_LTE: return "<=";
        case APSIS_EQ: return "==";
        case APSIS_NEQ: return "!=";
        case APSIS_STALE: return "stale";
        default: return "?";
    }
}

const char *apsis_rule_op_name(const apsis_rule *rule) {
    if (!rule) return "?";
    return apsis_op_name(rule->op);
}

int apsis_parse_level(const char *s, apsis_level *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "info") == 0) {
        *out = APSIS_INFO;
        return 0;
    }
    if (strcmp(s, "warn") == 0 || strcmp(s, "warning") == 0) {
        *out = APSIS_WARN;
        return 0;
    }
    if (strcmp(s, "error") == 0 || strcmp(s, "err") == 0) {
        *out = APSIS_ERROR;
        return 0;
    }
    return -1;
}

int apsis_parse_op(const char *s, apsis_op *out) {
    if (!s || !out) return -1;
    if (strcmp(s, ">") == 0) {
        *out = APSIS_GT;
        return 0;
    }
    if (strcmp(s, ">=") == 0) {
        *out = APSIS_GTE;
        return 0;
    }
    if (strcmp(s, "<") == 0) {
        *out = APSIS_LT;
        return 0;
    }
    if (strcmp(s, "<=") == 0) {
        *out = APSIS_LTE;
        return 0;
    }
    if (strcmp(s, "==") == 0) {
        *out = APSIS_EQ;
        return 0;
    }
    if (strcmp(s, "!=") == 0) {
        *out = APSIS_NEQ;
        return 0;
    }
    if (strcmp(s, "stale") == 0) {
        *out = APSIS_STALE;
        return 0;
    }
    return -1;
}

int apsis_rule_matches(const apsis_rule *rule, double value) {
    if (!rule) return 0;

    switch (rule->op) {
        case APSIS_GT: return value > rule->threshold;
        case APSIS_GTE: return value >= rule->threshold;
        case APSIS_LT: return value < rule->threshold;
        case APSIS_LTE: return value <= rule->threshold;
        case APSIS_EQ: return value == rule->threshold;
        case APSIS_NEQ: return value != rule->threshold;
        case APSIS_STALE: return value > rule->threshold;
        default: return 0;
    }
}

void apsis_init(apsis_ctx *ctx) {
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

int apsis_add_rule(apsis_ctx *ctx, const char *key, apsis_op op, double threshold,
                 apsis_level level, const char *event_id) {
    apsis_rule *rule;

    if (!ctx || !apsis_valid_name(key) || !apsis_valid_name(event_id)) return -1;
    if (op == APSIS_STALE) return -1;
    if (ctx->rule_count >= APSIS_MAX_RULES) return -2;

    rule = &ctx->rules[ctx->rule_count++];
    memset(rule, 0, sizeof(*rule));
    apsis_copy(rule->key, key, sizeof(rule->key));
    rule->op = op;
    rule->threshold = threshold;
    rule->level = level;
    apsis_copy(rule->event_id, event_id, sizeof(rule->event_id));
    return 0;
}

int apsis_add_stale_rule(apsis_ctx *ctx, const char *key, double stale_seconds,
                       apsis_level level, const char *event_id) {
    apsis_rule *rule;

    if (!ctx || !apsis_valid_name(key) || !apsis_valid_name(event_id)) return -1;
    if (stale_seconds < 0.0) return -1;
    if (ctx->rule_count >= APSIS_MAX_RULES) return -2;

    rule = &ctx->rules[ctx->rule_count++];
    memset(rule, 0, sizeof(*rule));
    apsis_copy(rule->key, key, sizeof(rule->key));
    rule->op = APSIS_STALE;
    rule->threshold = stale_seconds;
    rule->level = level;
    apsis_copy(rule->event_id, event_id, sizeof(rule->event_id));
    return 0;
}

static char *apsis_next_token(char **cursor) {
    char *s;
    char *tok;

    if (!cursor || !*cursor) return NULL;

    s = *cursor;
    while (*s && isspace((unsigned char)*s)) ++s;
    if (*s == '\0') {
        *cursor = s;
        return NULL;
    }

    tok = s;
    while (*s && !isspace((unsigned char)*s)) ++s;
    if (*s) {
        *s = '\0';
        ++s;
    }
    *cursor = s;
    return tok;
}

static int apsis_parse_rule_options(char **cursor,
                                  double *cooldown_seconds,
                                  unsigned long line_no,
                                  const char *label,
                                  char *err,
                                  size_t err_cap) {
    char *tok;
    char msg[192];
    int has_cooldown = 0;

    if (!cooldown_seconds) return -1;
    *cooldown_seconds = 0.0;

    while ((tok = apsis_next_token(cursor)) != NULL) {
        char *duration;

        if (strcmp(tok, "cooldown") != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: unknown rule option '%s'",
                     apsis_label(label), line_no, tok);
            apsis_set_err(err, err_cap, msg);
            return -1;
        }

        if (has_cooldown) {
            snprintf(msg, sizeof(msg), "%s:%lu: duplicate cooldown option",
                     apsis_label(label), line_no);
            apsis_set_err(err, err_cap, msg);
            return -1;
        }

        duration = apsis_next_token(cursor);
        if (!duration || apsis_parse_duration(duration, cooldown_seconds) != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: invalid cooldown duration",
                     apsis_label(label), line_no);
            apsis_set_err(err, err_cap, msg);
            return -1;
        }
        has_cooldown = 1;
    }

    return 0;
}

int apsis_parse_rule_line(apsis_ctx *ctx, const char *src, unsigned long line_no,
                        const char *label, char *err, size_t err_cap) {
    char line[APSIS_LINE_MAX];
    char *text;
    char *cursor;
    char *tok_key;
    char *tok_op;
    char *tok_threshold;
    char *tok_level;
    char *tok_event;
    apsis_op op;
    apsis_level level;
    double threshold;
    double cooldown_seconds;
    char *end = NULL;
    char msg[192];

    if (!ctx || !src) {
        apsis_set_err(err, err_cap, "missing rule");
        return -1;
    }
    if (strlen(src) >= sizeof(line)) {
        snprintf(msg, sizeof(msg), "%s:%lu: line too long",
                 apsis_label(label), line_no);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    apsis_copy(line, src, sizeof(line));
    apsis_strip_comment(line);
    text = apsis_ltrim(line);
    apsis_rtrim(text);
    if (*text == '\0') return 0;

    cursor = text;
    tok_key = apsis_next_token(&cursor);
    tok_op = apsis_next_token(&cursor);
    tok_threshold = apsis_next_token(&cursor);
    tok_level = apsis_next_token(&cursor);
    tok_event = apsis_next_token(&cursor);

    if (!tok_key || !tok_op || !tok_threshold || !tok_level || !tok_event) {
        snprintf(msg, sizeof(msg),
                 "%s:%lu: expected: <key> <op> <number> <level> <event_id>",
                 apsis_label(label), line_no);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    if (!apsis_valid_name(tok_key) || !apsis_valid_name(tok_event)) {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid key or event id",
                 apsis_label(label), line_no);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    if (apsis_parse_level(tok_level, &level) != 0) {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid level '%s'",
                 apsis_label(label), line_no, tok_level);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    if (apsis_parse_rule_options(&cursor, &cooldown_seconds, line_no, label,
                               err, err_cap) != 0) {
        return -1;
    }

    if (strcmp(tok_op, "stale") == 0) {
        if (apsis_parse_duration(tok_threshold, &threshold) != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: invalid stale duration '%s'",
                     apsis_label(label), line_no, tok_threshold);
            apsis_set_err(err, err_cap, msg);
            return -1;
        }

        if (apsis_add_stale_rule(ctx, tok_key, threshold, level, tok_event) != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: too many rules or invalid rule",
                     apsis_label(label), line_no);
            apsis_set_err(err, err_cap, msg);
            return -1;
        }
        ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;
        return 0;
    }

    if (apsis_parse_op(tok_op, &op) != 0 || op == APSIS_STALE) {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid operator '%s'",
                 apsis_label(label), line_no, tok_op);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    errno = 0;
    threshold = strtod(tok_threshold, &end);
    if (errno != 0 || !end || *end != '\0') {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid number '%s'",
                 apsis_label(label), line_no, tok_threshold);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    if (apsis_add_rule(ctx, tok_key, op, threshold, level, tok_event) != 0) {
        snprintf(msg, sizeof(msg), "%s:%lu: too many rules or invalid rule",
                 apsis_label(label), line_no);
        apsis_set_err(err, err_cap, msg);
        return -1;
    }
    ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;

    return 0;
}

int apsis_load_rules_stream(apsis_ctx *ctx, FILE *stream, const char *label,
                          char *err, size_t err_cap) {
    char line[APSIS_LINE_MAX];
    unsigned long line_no = 0;

    if (!ctx || !stream) {
        apsis_set_err(err, err_cap, "missing rules stream");
        return -1;
    }

    while (fgets(line, sizeof(line), stream)) {
        ++line_no;
        if (!strchr(line, '\n') && !feof(stream)) {
            char msg[192];
            snprintf(msg, sizeof(msg), "%s:%lu: line too long",
                     apsis_label(label), line_no);
            apsis_set_err(err, err_cap, msg);
            return -1;
        }
        if (apsis_parse_rule_line(ctx, line, line_no, label, err, err_cap) != 0) {
            return -1;
        }
    }

    if (ferror(stream)) {
        apsis_set_err(err, err_cap, "error reading rules");
        return -1;
    }

    return 0;
}

int apsis_load_rules_file(apsis_ctx *ctx, const char *path, char *err, size_t err_cap) {
    FILE *f;
    int rc;

    if (!ctx || !path) {
        apsis_set_err(err, err_cap, "missing rules path");
        return -1;
    }

    if (strcmp(path, "-") == 0) {
        return apsis_load_rules_stream(ctx, stdin, "stdin", err, err_cap);
    }

    f = fopen(path, "r");
    if (!f) {
        char msg[192];
        snprintf(msg, sizeof(msg), "cannot open rules file '%s': %s",
                 path, strerror(errno));
        apsis_set_err(err, err_cap, msg);
        return -1;
    }

    rc = apsis_load_rules_stream(ctx, f, path, err, err_cap);
    fclose(f);
    return rc;
}

int apsis_format_event(const apsis_rule *rule, double value,
                     char *out, size_t out_cap) {
    if (!rule || !out || out_cap == 0) return -1;
    snprintf(out, out_cap, "%s\t%s\t%s\t%s\t%.17g\t%.17g",
             apsis_level_name(rule->level),
             rule->event_id,
             rule->key,
             apsis_rule_op_name(rule),
             rule->threshold,
             value);
    return 0;
}

int apsis_format_event_record(const apsis_event *event, char *out, size_t out_cap) {
    if (!event || !out || out_cap == 0) return -1;
    snprintf(out, out_cap, "%s\t%s\t%s\t%s\t%.17g\t%.17g",
             apsis_level_name(event->level),
             event->event_id,
             event->key,
             apsis_op_name(event->op),
             event->threshold,
             event->value);
    return 0;
}

static void apsis_event_from_rule(const apsis_rule *rule,
                                double value,
                                double now_seconds,
                                apsis_event *event) {
    if (!rule || !event) return;
    memset(event, 0, sizeof(*event));
    apsis_copy(event->key, rule->key, sizeof(event->key));
    event->op = rule->op;
    event->threshold = rule->threshold;
    event->level = rule->level;
    apsis_copy(event->event_id, rule->event_id, sizeof(event->event_id));
    event->value = value;
    event->now_seconds = now_seconds;
}

static int apsis_record_event(apsis_ctx *ctx,
                            apsis_rule *rule,
                            double value,
                            double now_seconds,
                            apsis_event_fn emit,
                            void *user) {
    apsis_event event;

    if (!ctx || !rule) return -1;

    if (rule->cooldown_seconds > 0.0 && rule->has_last_emit_time) {
        double elapsed = now_seconds - rule->last_emit_time;
        if (elapsed < rule->cooldown_seconds) return 0;
    }

    apsis_event_from_rule(rule, value, now_seconds, &event);
    if (emit && emit(&event, user) != 0) return -1;

    rule->last_emit_time = now_seconds;
    rule->has_last_emit_time = 1;

    ctx->events_emitted++;
    if (rule->level == APSIS_INFO) ctx->info_count++;
    if (rule->level == APSIS_WARN) ctx->warn_count++;
    if (rule->level == APSIS_ERROR) ctx->error_count++;

    return 1;
}

int apsis_sample_each(apsis_ctx *ctx,
                    const char *key,
                    double value,
                    double now_seconds,
                    apsis_event_fn emit,
                    void *user) {
    int emitted = 0;
    size_t i;

    if (!ctx || !key) return -1;
    ctx->samples_seen++;

    for (i = 0; i < ctx->rule_count; ++i) {
        apsis_rule *rule = &ctx->rules[i];
        if (strcmp(rule->key, key) != 0) continue;
        if (rule->op == APSIS_STALE) {
            rule->last_seen_time = now_seconds;
            rule->has_last_seen_time = 1;
            continue;
        }
        if (!apsis_rule_matches(rule, value)) continue;
        {
            int rc = apsis_record_event(ctx, rule, value, now_seconds, emit, user);
            if (rc < 0) return rc;
            emitted += rc;
        }
    }

    return emitted;
}

typedef struct apsis_first_event {
    char *out;
    size_t out_cap;
} apsis_first_event;

static int apsis_capture_first_event(const apsis_event *event, void *user) {
    apsis_first_event *capture = (apsis_first_event *)user;

    if (!event || !capture) return -1;
    if (capture->out && capture->out_cap && capture->out[0] == '\0') {
        return apsis_format_event_record(event, capture->out, capture->out_cap);
    }
    return 0;
}

int apsis_sample(apsis_ctx *ctx, const char *key, double value,
               char *event, size_t event_cap) {
    apsis_first_event capture;

    if (event && event_cap) event[0] = '\0';
    capture.out = event;
    capture.out_cap = event_cap;
    return apsis_sample_each(ctx, key, value, apsis_now_seconds(),
                           apsis_capture_first_event, &capture);
}

int apsis_emit_stale_each(apsis_ctx *ctx,
                        double now_seconds,
                        apsis_event_fn fn,
                        void *user) {
    int emitted = 0;
    size_t i;

    if (!ctx) return -1;

    for (i = 0; i < ctx->rule_count; ++i) {
        apsis_rule *rule = &ctx->rules[i];
        double stale_value;
        int rc;

        if (rule->op != APSIS_STALE) continue;
        if (rule->has_last_seen_time) {
            stale_value = now_seconds - rule->last_seen_time;
            if (stale_value <= rule->threshold) continue;
        } else {
            stale_value = rule->threshold;
        }

        rc = apsis_record_event(ctx, rule, stale_value, now_seconds, fn, user);
        if (rc < 0) return rc;
        emitted += rc;
    }

    return emitted;
}

int apsis_emit_missing_stale(apsis_ctx *ctx, apsis_event_fn fn, void *user) {
    return apsis_emit_stale_each(ctx, apsis_now_seconds(), fn, user);
}
