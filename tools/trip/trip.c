#define _POSIX_C_SOURCE 200809L

#include "trip.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRIP_LINE_MAX 512

static void trip_strlcpy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static char *trip_ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static void trip_rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        --n;
    }
}

static void trip_strip_comment(char *s) {
    char *p = strchr(s, '#');
    if (p) *p = '\0';
}

static void trip_set_err(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap) trip_strlcpy(err, msg, err_cap);
}

static int trip_valid_name(const char *s) {
    size_t n;
    if (!s || !*s) return 0;
    n = strlen(s);
    if (n >= TRIP_MAX_NAME) return 0;
    for (size_t i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }
    return 1;
}

static int trip_parse_duration(const char *s, double *out) {
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

const char *trip_level_name(trip_level level) {
    switch (level) {
        case TRIP_LEVEL_INFO: return "info";
        case TRIP_LEVEL_WARN: return "warn";
        case TRIP_LEVEL_ERROR: return "error";
        default: return "unknown";
    }
}

const char *trip_op_name(trip_op op) {
    switch (op) {
        case TRIP_OP_GT: return ">";
        case TRIP_OP_GTE: return ">=";
        case TRIP_OP_LT: return "<";
        case TRIP_OP_LTE: return "<=";
        case TRIP_OP_EQ: return "==";
        case TRIP_OP_NEQ: return "!=";
        default: return "?";
    }
}

const char *trip_rule_op_name(const trip_rule *rule) {
    if (!rule) return "?";
    if (rule->kind == TRIP_RULE_STALE) return "stale";
    return trip_op_name(rule->op);
}

int trip_parse_level(const char *s, trip_level *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "info") == 0) { *out = TRIP_LEVEL_INFO; return 0; }
    if (strcmp(s, "warn") == 0 || strcmp(s, "warning") == 0) { *out = TRIP_LEVEL_WARN; return 0; }
    if (strcmp(s, "error") == 0 || strcmp(s, "err") == 0) { *out = TRIP_LEVEL_ERROR; return 0; }
    return -1;
}

int trip_parse_op(const char *s, trip_op *out) {
    if (!s || !out) return -1;
    if (strcmp(s, ">") == 0) { *out = TRIP_OP_GT; return 0; }
    if (strcmp(s, ">=") == 0) { *out = TRIP_OP_GTE; return 0; }
    if (strcmp(s, "<") == 0) { *out = TRIP_OP_LT; return 0; }
    if (strcmp(s, "<=") == 0) { *out = TRIP_OP_LTE; return 0; }
    if (strcmp(s, "==") == 0) { *out = TRIP_OP_EQ; return 0; }
    if (strcmp(s, "!=") == 0) { *out = TRIP_OP_NEQ; return 0; }
    return -1;
}

int trip_rule_matches(const trip_rule *rule, double value) {
    if (!rule) return 0;
    if (rule->kind == TRIP_RULE_STALE) return value > rule->threshold;
    switch (rule->op) {
        case TRIP_OP_GT: return value > rule->threshold;
        case TRIP_OP_GTE: return value >= rule->threshold;
        case TRIP_OP_LT: return value < rule->threshold;
        case TRIP_OP_LTE: return value <= rule->threshold;
        case TRIP_OP_EQ: return value == rule->threshold;
        case TRIP_OP_NEQ: return value != rule->threshold;
        default: return 0;
    }
}

void trip_init(trip_ctx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
}

int trip_add_rule(trip_ctx *ctx, const char *key, trip_op op, double threshold,
                 trip_level level, const char *event_id) {
    trip_rule *r;
    if (!ctx || !trip_valid_name(key) || !trip_valid_name(event_id)) return -1;
    if (ctx->rule_count >= TRIP_MAX_RULES) return -2;

    r = &ctx->rules[ctx->rule_count++];
    trip_strlcpy(r->key, key, sizeof(r->key));
    r->kind = TRIP_RULE_LIMIT;
    r->op = op;
    r->threshold = threshold;
    r->level = level;
    trip_strlcpy(r->event_id, event_id, sizeof(r->event_id));
    return 0;
}

int trip_add_stale_rule(trip_ctx *ctx, const char *key, double stale_seconds,
                       trip_level level, const char *event_id) {
    trip_rule *r;
    if (!ctx || !trip_valid_name(key) || !trip_valid_name(event_id)) return -1;
    if (stale_seconds < 0.0) return -1;
    if (ctx->rule_count >= TRIP_MAX_RULES) return -2;

    r = &ctx->rules[ctx->rule_count++];
    trip_strlcpy(r->key, key, sizeof(r->key));
    r->kind = TRIP_RULE_STALE;
    r->op = TRIP_OP_GT;
    r->threshold = stale_seconds;
    r->level = level;
    trip_strlcpy(r->event_id, event_id, sizeof(r->event_id));
    return 0;
}

static int trip_parse_rule_options(double *cooldown_seconds, unsigned long line_no,
                                  char *err, size_t err_cap) {
    char *tok;
    char msg[160];
    int has_cooldown = 0;

    if (!cooldown_seconds) return -1;
    *cooldown_seconds = 0.0;

    while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
        char *duration;

        if (strcmp(tok, "cooldown") != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: unknown rule option '%s'", line_no, tok);
            trip_set_err(err, err_cap, msg);
            return -1;
        }

        if (has_cooldown) {
            snprintf(msg, sizeof(msg), "rules:%lu: duplicate cooldown option", line_no);
            trip_set_err(err, err_cap, msg);
            return -1;
        }

        duration = strtok(NULL, " \t\r\n");
        if (!duration || trip_parse_duration(duration, cooldown_seconds) != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: invalid cooldown duration", line_no);
            trip_set_err(err, err_cap, msg);
            return -1;
        }
        has_cooldown = 1;
    }

    return 0;
}

static int trip_parse_rule_line(trip_ctx *ctx, char *line, unsigned long line_no,
                               char *err, size_t err_cap) {
    char *tok_key, *tok_op, *tok_threshold, *tok_level, *tok_event;
    trip_op op;
    trip_level level;
    double threshold;
    double cooldown_seconds;
    char *end = NULL;
    char msg[160];

    trip_strip_comment(line);
    line = trip_ltrim(line);
    trip_rtrim(line);
    if (*line == '\0') return 0;

    tok_key = strtok(line, " \t\r\n");
    tok_op = strtok(NULL, " \t\r\n");
    tok_threshold = strtok(NULL, " \t\r\n");
    tok_level = strtok(NULL, " \t\r\n");
    tok_event = strtok(NULL, " \t\r\n");

    if (!tok_key || !tok_op || !tok_threshold || !tok_level || !tok_event) {
        snprintf(msg, sizeof(msg), "rules:%lu: expected: <key> <op> <number> <level> <event_id>", line_no);
        trip_set_err(err, err_cap, msg);
        return -1;
    }

    if (!trip_valid_name(tok_key) || !trip_valid_name(tok_event)) {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid key or event id", line_no);
        trip_set_err(err, err_cap, msg);
        return -1;
    }

    if (trip_parse_level(tok_level, &level) != 0) {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid level '%s'", line_no, tok_level);
        trip_set_err(err, err_cap, msg);
        return -1;
    }

    if (trip_parse_rule_options(&cooldown_seconds, line_no, err, err_cap) != 0) {
        return -1;
    }

    if (strcmp(tok_op, "stale") == 0) {
        if (trip_parse_duration(tok_threshold, &threshold) != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: invalid stale duration '%s'", line_no, tok_threshold);
            trip_set_err(err, err_cap, msg);
            return -1;
        }

        if (trip_add_stale_rule(ctx, tok_key, threshold, level, tok_event) != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: too many rules or invalid rule", line_no);
            trip_set_err(err, err_cap, msg);
            return -1;
        }
        ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;
        return 0;
    }

    if (trip_parse_op(tok_op, &op) != 0) {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid operator '%s'", line_no, tok_op);
        trip_set_err(err, err_cap, msg);
        return -1;
    }

    errno = 0;
    threshold = strtod(tok_threshold, &end);
    if (errno != 0 || !end || *end != '\0') {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid number '%s'", line_no, tok_threshold);
        trip_set_err(err, err_cap, msg);
        return -1;
    }

    if (trip_add_rule(ctx, tok_key, op, threshold, level, tok_event) != 0) {
        snprintf(msg, sizeof(msg), "rules:%lu: too many rules or invalid rule", line_no);
        trip_set_err(err, err_cap, msg);
        return -1;
    }
    ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;

    return 0;
}

int trip_load_rules_file(trip_ctx *ctx, const char *path, char *err, size_t err_cap) {
    FILE *f;
    char line[TRIP_LINE_MAX];
    unsigned long line_no = 0;

    if (!ctx || !path) {
        trip_set_err(err, err_cap, "missing rules path");
        return -1;
    }

    f = fopen(path, "r");
    if (!f) {
        char msg[160];
        snprintf(msg, sizeof(msg), "cannot open rules file '%s': %s", path, strerror(errno));
        trip_set_err(err, err_cap, msg);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        ++line_no;
        if (trip_parse_rule_line(ctx, line, line_no, err, err_cap) != 0) {
            fclose(f);
            return -1;
        }
    }

    if (ferror(f)) {
        trip_set_err(err, err_cap, "error reading rules file");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

static int trip_emit_event(const trip_rule *rule, double value, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return -1;
    snprintf(out, out_cap, "%s\t%s\t%s\t%s\t%.17g\t%.17g",
             trip_level_name(rule->level),
             rule->event_id,
             rule->key,
             trip_rule_op_name(rule),
             rule->threshold,
             value);
    return 0;
}

static int trip_record_event(trip_ctx *ctx, trip_rule *rule, double value,
                            char *out, size_t out_cap, time_t now) {
    if (!ctx || !rule) return -1;

    if (rule->cooldown_seconds > 0.0 && rule->has_last_emit_time) {
        double elapsed = difftime(now, (time_t)rule->last_emit_time);
        if (elapsed < rule->cooldown_seconds) return 0;
    }

    rule->last_emit_time = (long long)now;
    rule->has_last_emit_time = 1;

    ctx->events_emitted++;
    if (rule->level == TRIP_LEVEL_INFO) ctx->info_count++;
    if (rule->level == TRIP_LEVEL_WARN) ctx->warn_count++;
    if (rule->level == TRIP_LEVEL_ERROR) ctx->error_count++;

    if (out && out_cap && out[0] == '\0') {
        trip_emit_event(rule, value, out, out_cap);
    }

    return 1;
}

int trip_sample(trip_ctx *ctx, const char *key, double value, char *out, size_t out_cap) {
    int emitted = 0;
    time_t now = time(NULL);
    if (!ctx || !key) return -1;
    ctx->samples_seen++;

    if (out && out_cap) out[0] = '\0';

    for (size_t i = 0; i < ctx->rule_count; ++i) {
        trip_rule *r = &ctx->rules[i];
        if (strcmp(r->key, key) != 0) continue;
        if (r->kind == TRIP_RULE_STALE) r->seen = 1;
        if (!trip_rule_matches(r, value)) continue;

        emitted += trip_record_event(ctx, r, value, out, out_cap, now);
    }

    return emitted;
}

#ifndef TRIP_NO_MAIN
static int trip_emit_missing_stale(trip_ctx *ctx) {
    int emitted = 0;
    time_t now = time(NULL);

    if (!ctx) return -1;

    for (size_t i = 0; i < ctx->rule_count; ++i) {
        trip_rule *r = &ctx->rules[i];
        char event_line[512];
        int er;

        if (r->kind != TRIP_RULE_STALE || r->seen) continue;

        event_line[0] = '\0';
        er = trip_record_event(ctx, r, r->threshold, event_line,
                              sizeof(event_line), now);
        if (er > 0 && event_line[0]) {
            puts(event_line);
            emitted += er;
        }
    }

    return emitted;
}

static void trip_usage(FILE *f) {
    fprintf(f,
        "usage: trip -r rules.trip [--fail-on error|warn|never] [--summary]\n"
        "\n"
        "Read key=value telemetry from stdin and emit events for rules that trip.\n"
        "\n"
        "Rules:\n"
        "  <key> <op> <number> <level> <event_id>\n"
        "  <key> stale <duration> <level> <event_id>\n"
        "  append: cooldown <duration>\n"
        "\n"
        "Example rules:\n"
        "  temperature > 80 warn temperature.high\n"
        "  queue.depth > 1000 error queue.backpressure\n"
        "  heartbeat stale 5s error heartbeat.missing\n"
        "  temperature > 80 warn temperature.high cooldown 10s\n"
        "\n"
        "Input:\n"
        "  temperature=82.4\n"
        "  queue.depth 1402\n"
        "\n"
        "Options:\n"
        "  -r, --rules PATH       rules file\n"
        "  --fail-on LEVEL        error, warn, or never; default: error\n"
        "  --summary              print summary to stderr\n"
        "  -h, --help             show help\n"
        "  --version              show version\n");
}

static int trip_parse_sample_line(char *line, char *key, size_t key_cap, double *value) {
    char *p;
    char *v;
    char *end = NULL;

    trip_strip_comment(line);
    line = trip_ltrim(line);
    trip_rtrim(line);
    if (*line == '\0') return 0;

    p = strchr(line, '=');
    if (p) {
        *p = '\0';
        v = p + 1;
        trip_rtrim(line);
        v = trip_ltrim(v);
    } else {
        char *save = NULL;
        char *k = strtok_r(line, " \t\r\n", &save);
        char *val = strtok_r(NULL, " \t\r\n", &save);
        if (!k || !val) return -1;
        trip_strlcpy(key, k, key_cap);
        errno = 0;
        *value = strtod(val, &end);
        if (errno != 0 || !end || *end != '\0') return -1;
        return 1;
    }

    if (!trip_valid_name(line)) return -1;
    trip_strlcpy(key, line, key_cap);
    errno = 0;
    *value = strtod(v, &end);
    if (errno != 0 || !end || *end != '\0') return -1;
    return 1;
}

int main(int argc, char **argv) {
    const char *rules_path = NULL;
    trip_level fail_on = TRIP_LEVEL_ERROR;
    int fail_never = 0;
    int summary = 0;
    trip_ctx ctx;
    char err[256];
    char line[TRIP_LINE_MAX];
    unsigned long input_line = 0;
    int bad_input = 0;

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rules") == 0) && i + 1 < argc) {
            rules_path = argv[++i];
        } else if (strcmp(argv[i], "--fail-on") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            if (strcmp(arg, "never") == 0) {
                fail_never = 1;
            } else if (trip_parse_level(arg, &fail_on) != 0) {
                fprintf(stderr, "trip: invalid --fail-on value '%s'\n", arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--summary") == 0) {
            summary = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            trip_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("trip 0.1.0");
            return 0;
        } else {
            fprintf(stderr, "trip: unknown argument '%s'\n", argv[i]);
            trip_usage(stderr);
            return 2;
        }
    }

    if (!rules_path) {
        fprintf(stderr, "trip: missing -r rules.trip\n");
        trip_usage(stderr);
        return 2;
    }

    trip_init(&ctx);
    if (trip_load_rules_file(&ctx, rules_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "trip: %s\n", err);
        return 2;
    }

    while (fgets(line, sizeof(line), stdin)) {
        char key[TRIP_MAX_NAME];
        double value;
        int pr;
        int emitted;
        char event_line[512];

        input_line++;
        pr = trip_parse_sample_line(line, key, sizeof(key), &value);
        if (pr == 0) continue;
        if (pr < 0) {
            fprintf(stderr, "trip: input:%lu: expected key=value or key value\n", input_line);
            bad_input = 1;
            continue;
        }

        emitted = trip_sample(&ctx, key, value, event_line, sizeof(event_line));
        if (emitted > 0 && event_line[0]) {
            puts(event_line);
        }
    }

    if (ferror(stdin)) {
        fprintf(stderr, "trip: error reading stdin\n");
        return 2;
    }

    trip_emit_missing_stale(&ctx);

    if (summary) {
        fprintf(stderr,
                "trip: samples=%lu events=%lu info=%lu warn=%lu error=%lu rules=%lu\n",
                ctx.samples_seen, ctx.events_emitted, ctx.info_count,
                ctx.warn_count, ctx.error_count, (unsigned long)ctx.rule_count);
    }

    if (bad_input) return 2;
    if (!fail_never) {
        if (fail_on == TRIP_LEVEL_ERROR && ctx.error_count > 0) return 1;
        if (fail_on == TRIP_LEVEL_WARN && (ctx.warn_count > 0 || ctx.error_count > 0)) return 1;
        if (fail_on == TRIP_LEVEL_INFO && ctx.events_emitted > 0) return 1;
    }

    return 0;
}
#endif
