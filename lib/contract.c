#include "ctc_contract.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void ctc_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static char *ctc_ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static void ctc_rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        --n;
    }
}

static void ctc_strip_comment(char *s) {
    char *p = strchr(s, '#');
    if (p) *p = '\0';
}

static void ctc_set_err(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap) ctc_copy(err, msg, err_cap);
}

static const char *ctc_label(const char *label) {
    return (label && *label) ? label : "rules";
}

int ctc_valid_name(const char *s) {
    size_t n;
    size_t i;

    if (!s || !*s) return 0;
    n = strlen(s);
    if (n >= CTC_MAX_NAME) return 0;

    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' ||
              c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }

    return 1;
}

int ctc_parse_duration(const char *s, double *out) {
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

const char *ctc_level_name(ctc_level level) {
    switch (level) {
        case CTC_INFO: return "info";
        case CTC_WARN: return "warn";
        case CTC_ERROR: return "error";
        default: return "unknown";
    }
}

const char *ctc_op_name(ctc_op op) {
    switch (op) {
        case CTC_GT: return ">";
        case CTC_GTE: return ">=";
        case CTC_LT: return "<";
        case CTC_LTE: return "<=";
        case CTC_EQ: return "==";
        case CTC_NEQ: return "!=";
        case CTC_STALE: return "stale";
        default: return "?";
    }
}

const char *ctc_rule_op_name(const ctc_rule *rule) {
    if (!rule) return "?";
    return ctc_op_name(rule->op);
}

int ctc_parse_level(const char *s, ctc_level *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "info") == 0) {
        *out = CTC_INFO;
        return 0;
    }
    if (strcmp(s, "warn") == 0 || strcmp(s, "warning") == 0) {
        *out = CTC_WARN;
        return 0;
    }
    if (strcmp(s, "error") == 0 || strcmp(s, "err") == 0) {
        *out = CTC_ERROR;
        return 0;
    }
    return -1;
}

int ctc_parse_op(const char *s, ctc_op *out) {
    if (!s || !out) return -1;
    if (strcmp(s, ">") == 0) {
        *out = CTC_GT;
        return 0;
    }
    if (strcmp(s, ">=") == 0) {
        *out = CTC_GTE;
        return 0;
    }
    if (strcmp(s, "<") == 0) {
        *out = CTC_LT;
        return 0;
    }
    if (strcmp(s, "<=") == 0) {
        *out = CTC_LTE;
        return 0;
    }
    if (strcmp(s, "==") == 0) {
        *out = CTC_EQ;
        return 0;
    }
    if (strcmp(s, "!=") == 0) {
        *out = CTC_NEQ;
        return 0;
    }
    if (strcmp(s, "stale") == 0) {
        *out = CTC_STALE;
        return 0;
    }
    return -1;
}

int ctc_rule_matches(const ctc_rule *rule, double value) {
    if (!rule) return 0;

    switch (rule->op) {
        case CTC_GT: return value > rule->threshold;
        case CTC_GTE: return value >= rule->threshold;
        case CTC_LT: return value < rule->threshold;
        case CTC_LTE: return value <= rule->threshold;
        case CTC_EQ: return value == rule->threshold;
        case CTC_NEQ: return value != rule->threshold;
        case CTC_STALE: return value > rule->threshold;
        default: return 0;
    }
}

void ctc_init(ctc_ctx *ctx) {
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

int ctc_add_rule(ctc_ctx *ctx, const char *key, ctc_op op, double threshold,
                 ctc_level level, const char *event_id) {
    ctc_rule *rule;

    if (!ctx || !ctc_valid_name(key) || !ctc_valid_name(event_id)) return -1;
    if (op == CTC_STALE) return -1;
    if (ctx->rule_count >= CTC_MAX_RULES) return -2;

    rule = &ctx->rules[ctx->rule_count++];
    memset(rule, 0, sizeof(*rule));
    ctc_copy(rule->key, key, sizeof(rule->key));
    rule->op = op;
    rule->threshold = threshold;
    rule->level = level;
    ctc_copy(rule->event_id, event_id, sizeof(rule->event_id));
    return 0;
}

int ctc_add_stale_rule(ctc_ctx *ctx, const char *key, double stale_seconds,
                       ctc_level level, const char *event_id) {
    ctc_rule *rule;

    if (!ctx || !ctc_valid_name(key) || !ctc_valid_name(event_id)) return -1;
    if (stale_seconds < 0.0) return -1;
    if (ctx->rule_count >= CTC_MAX_RULES) return -2;

    rule = &ctx->rules[ctx->rule_count++];
    memset(rule, 0, sizeof(*rule));
    ctc_copy(rule->key, key, sizeof(rule->key));
    rule->op = CTC_STALE;
    rule->threshold = stale_seconds;
    rule->level = level;
    ctc_copy(rule->event_id, event_id, sizeof(rule->event_id));
    return 0;
}

static int ctc_parse_rule_options(double *cooldown_seconds,
                                  unsigned long line_no,
                                  const char *label,
                                  char *err,
                                  size_t err_cap) {
    char *tok;
    char msg[192];
    int has_cooldown = 0;

    if (!cooldown_seconds) return -1;
    *cooldown_seconds = 0.0;

    while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
        char *duration;

        if (strcmp(tok, "cooldown") != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: unknown rule option '%s'",
                     ctc_label(label), line_no, tok);
            ctc_set_err(err, err_cap, msg);
            return -1;
        }

        if (has_cooldown) {
            snprintf(msg, sizeof(msg), "%s:%lu: duplicate cooldown option",
                     ctc_label(label), line_no);
            ctc_set_err(err, err_cap, msg);
            return -1;
        }

        duration = strtok(NULL, " \t\r\n");
        if (!duration || ctc_parse_duration(duration, cooldown_seconds) != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: invalid cooldown duration",
                     ctc_label(label), line_no);
            ctc_set_err(err, err_cap, msg);
            return -1;
        }
        has_cooldown = 1;
    }

    return 0;
}

int ctc_parse_rule_line(ctc_ctx *ctx, const char *src, unsigned long line_no,
                        const char *label, char *err, size_t err_cap) {
    char line[CTC_LINE_MAX];
    char *text;
    char *tok_key;
    char *tok_op;
    char *tok_threshold;
    char *tok_level;
    char *tok_event;
    ctc_op op;
    ctc_level level;
    double threshold;
    double cooldown_seconds;
    char *end = NULL;
    char msg[192];

    if (!ctx || !src) {
        ctc_set_err(err, err_cap, "missing rule");
        return -1;
    }
    if (strlen(src) >= sizeof(line)) {
        snprintf(msg, sizeof(msg), "%s:%lu: line too long",
                 ctc_label(label), line_no);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    ctc_copy(line, src, sizeof(line));
    ctc_strip_comment(line);
    text = ctc_ltrim(line);
    ctc_rtrim(text);
    if (*text == '\0') return 0;

    tok_key = strtok(text, " \t\r\n");
    tok_op = strtok(NULL, " \t\r\n");
    tok_threshold = strtok(NULL, " \t\r\n");
    tok_level = strtok(NULL, " \t\r\n");
    tok_event = strtok(NULL, " \t\r\n");

    if (!tok_key || !tok_op || !tok_threshold || !tok_level || !tok_event) {
        snprintf(msg, sizeof(msg),
                 "%s:%lu: expected: <key> <op> <number> <level> <event_id>",
                 ctc_label(label), line_no);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    if (!ctc_valid_name(tok_key) || !ctc_valid_name(tok_event)) {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid key or event id",
                 ctc_label(label), line_no);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    if (ctc_parse_level(tok_level, &level) != 0) {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid level '%s'",
                 ctc_label(label), line_no, tok_level);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    if (ctc_parse_rule_options(&cooldown_seconds, line_no, label,
                               err, err_cap) != 0) {
        return -1;
    }

    if (strcmp(tok_op, "stale") == 0) {
        if (ctc_parse_duration(tok_threshold, &threshold) != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: invalid stale duration '%s'",
                     ctc_label(label), line_no, tok_threshold);
            ctc_set_err(err, err_cap, msg);
            return -1;
        }

        if (ctc_add_stale_rule(ctx, tok_key, threshold, level, tok_event) != 0) {
            snprintf(msg, sizeof(msg), "%s:%lu: too many rules or invalid rule",
                     ctc_label(label), line_no);
            ctc_set_err(err, err_cap, msg);
            return -1;
        }
        ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;
        return 0;
    }

    if (ctc_parse_op(tok_op, &op) != 0 || op == CTC_STALE) {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid operator '%s'",
                 ctc_label(label), line_no, tok_op);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    errno = 0;
    threshold = strtod(tok_threshold, &end);
    if (errno != 0 || !end || *end != '\0') {
        snprintf(msg, sizeof(msg), "%s:%lu: invalid number '%s'",
                 ctc_label(label), line_no, tok_threshold);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    if (ctc_add_rule(ctx, tok_key, op, threshold, level, tok_event) != 0) {
        snprintf(msg, sizeof(msg), "%s:%lu: too many rules or invalid rule",
                 ctc_label(label), line_no);
        ctc_set_err(err, err_cap, msg);
        return -1;
    }
    ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;

    return 0;
}

int ctc_load_rules_stream(ctc_ctx *ctx, FILE *stream, const char *label,
                          char *err, size_t err_cap) {
    char line[CTC_LINE_MAX];
    unsigned long line_no = 0;

    if (!ctx || !stream) {
        ctc_set_err(err, err_cap, "missing rules stream");
        return -1;
    }

    while (fgets(line, sizeof(line), stream)) {
        ++line_no;
        if (!strchr(line, '\n') && !feof(stream)) {
            char msg[192];
            snprintf(msg, sizeof(msg), "%s:%lu: line too long",
                     ctc_label(label), line_no);
            ctc_set_err(err, err_cap, msg);
            return -1;
        }
        if (ctc_parse_rule_line(ctx, line, line_no, label, err, err_cap) != 0) {
            return -1;
        }
    }

    if (ferror(stream)) {
        ctc_set_err(err, err_cap, "error reading rules");
        return -1;
    }

    return 0;
}

int ctc_load_rules_file(ctc_ctx *ctx, const char *path, char *err, size_t err_cap) {
    FILE *f;
    int rc;

    if (!ctx || !path) {
        ctc_set_err(err, err_cap, "missing rules path");
        return -1;
    }

    if (strcmp(path, "-") == 0) {
        return ctc_load_rules_stream(ctx, stdin, "stdin", err, err_cap);
    }

    f = fopen(path, "r");
    if (!f) {
        char msg[192];
        snprintf(msg, sizeof(msg), "cannot open rules file '%s': %s",
                 path, strerror(errno));
        ctc_set_err(err, err_cap, msg);
        return -1;
    }

    rc = ctc_load_rules_stream(ctx, f, path, err, err_cap);
    fclose(f);
    return rc;
}

int ctc_format_event(const ctc_rule *rule, double value,
                     char *out, size_t out_cap) {
    if (!rule || !out || out_cap == 0) return -1;
    snprintf(out, out_cap, "%s\t%s\t%s\t%s\t%.17g\t%.17g",
             ctc_level_name(rule->level),
             rule->event_id,
             rule->key,
             ctc_rule_op_name(rule),
             rule->threshold,
             value);
    return 0;
}

static int ctc_record_event(ctc_ctx *ctx, ctc_rule *rule, double value,
                            char *out, size_t out_cap, time_t now) {
    if (!ctx || !rule) return -1;

    if (rule->cooldown_seconds > 0.0 && rule->has_last_emit_time) {
        double elapsed = difftime(now, (time_t)rule->last_emit_time);
        if (elapsed < rule->cooldown_seconds) return 0;
    }

    rule->last_emit_time = (long long)now;
    rule->has_last_emit_time = 1;

    ctx->events_emitted++;
    if (rule->level == CTC_INFO) ctx->info_count++;
    if (rule->level == CTC_WARN) ctx->warn_count++;
    if (rule->level == CTC_ERROR) ctx->error_count++;

    if (out && out_cap && out[0] == '\0') {
        ctc_format_event(rule, value, out, out_cap);
    }

    return 1;
}

int ctc_sample(ctc_ctx *ctx, const char *key, double value,
               char *event, size_t event_cap) {
    int emitted = 0;
    time_t now = time(NULL);
    size_t i;

    if (!ctx || !key) return -1;
    ctx->samples_seen++;

    if (event && event_cap) event[0] = '\0';

    for (i = 0; i < ctx->rule_count; ++i) {
        ctc_rule *rule = &ctx->rules[i];
        if (strcmp(rule->key, key) != 0) continue;
        if (rule->op == CTC_STALE) rule->seen = 1;
        if (!ctc_rule_matches(rule, value)) continue;
        emitted += ctc_record_event(ctx, rule, value, event, event_cap, now);
    }

    return emitted;
}

int ctc_emit_missing_stale(ctc_ctx *ctx, ctc_event_fn fn, void *user) {
    int emitted = 0;
    time_t now = time(NULL);
    size_t i;

    if (!ctx) return -1;

    for (i = 0; i < ctx->rule_count; ++i) {
        ctc_rule *rule = &ctx->rules[i];
        char line[CTC_LINE_MAX];
        int rc;

        if (rule->op != CTC_STALE || rule->seen) continue;

        line[0] = '\0';
        rc = ctc_record_event(ctx, rule, rule->threshold,
                              line, sizeof(line), now);
        if (rc > 0) {
            emitted += rc;
            if (fn && line[0]) fn(line, user);
        }
    }

    return emitted;
}
