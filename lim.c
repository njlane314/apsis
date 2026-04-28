#define _POSIX_C_SOURCE 200809L

#include "lim.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LIM_LINE_MAX 512

static void lim_strlcpy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static char *lim_ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static void lim_rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        --n;
    }
}

static void lim_strip_comment(char *s) {
    char *p = strchr(s, '#');
    if (p) *p = '\0';
}

static void lim_set_err(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap) lim_strlcpy(err, msg, err_cap);
}

static int lim_valid_name(const char *s) {
    size_t n;
    if (!s || !*s) return 0;
    n = strlen(s);
    if (n >= LIM_MAX_NAME) return 0;
    for (size_t i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }
    return 1;
}

static int lim_parse_duration(const char *s, double *out) {
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

const char *lim_level_name(lim_level level) {
    switch (level) {
        case LIM_LEVEL_INFO: return "info";
        case LIM_LEVEL_WARN: return "warn";
        case LIM_LEVEL_ERROR: return "error";
        default: return "unknown";
    }
}

const char *lim_op_name(lim_op op) {
    switch (op) {
        case LIM_OP_GT: return ">";
        case LIM_OP_GTE: return ">=";
        case LIM_OP_LT: return "<";
        case LIM_OP_LTE: return "<=";
        case LIM_OP_EQ: return "==";
        case LIM_OP_NEQ: return "!=";
        default: return "?";
    }
}

const char *lim_rule_op_name(const lim_rule *rule) {
    if (!rule) return "?";
    if (rule->kind == LIM_RULE_STALE) return "stale";
    return lim_op_name(rule->op);
}

int lim_parse_level(const char *s, lim_level *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "info") == 0) { *out = LIM_LEVEL_INFO; return 0; }
    if (strcmp(s, "warn") == 0 || strcmp(s, "warning") == 0) { *out = LIM_LEVEL_WARN; return 0; }
    if (strcmp(s, "error") == 0 || strcmp(s, "err") == 0) { *out = LIM_LEVEL_ERROR; return 0; }
    return -1;
}

int lim_parse_op(const char *s, lim_op *out) {
    if (!s || !out) return -1;
    if (strcmp(s, ">") == 0) { *out = LIM_OP_GT; return 0; }
    if (strcmp(s, ">=") == 0) { *out = LIM_OP_GTE; return 0; }
    if (strcmp(s, "<") == 0) { *out = LIM_OP_LT; return 0; }
    if (strcmp(s, "<=") == 0) { *out = LIM_OP_LTE; return 0; }
    if (strcmp(s, "==") == 0) { *out = LIM_OP_EQ; return 0; }
    if (strcmp(s, "!=") == 0) { *out = LIM_OP_NEQ; return 0; }
    return -1;
}

int lim_rule_matches(const lim_rule *rule, double value) {
    if (!rule) return 0;
    if (rule->kind == LIM_RULE_STALE) return value > rule->threshold;
    switch (rule->op) {
        case LIM_OP_GT: return value > rule->threshold;
        case LIM_OP_GTE: return value >= rule->threshold;
        case LIM_OP_LT: return value < rule->threshold;
        case LIM_OP_LTE: return value <= rule->threshold;
        case LIM_OP_EQ: return value == rule->threshold;
        case LIM_OP_NEQ: return value != rule->threshold;
        default: return 0;
    }
}

void lim_init(lim_ctx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
}

int lim_add_rule(lim_ctx *ctx, const char *key, lim_op op, double threshold,
                 lim_level level, const char *event_id) {
    lim_rule *r;
    if (!ctx || !lim_valid_name(key) || !lim_valid_name(event_id)) return -1;
    if (ctx->rule_count >= LIM_MAX_RULES) return -2;

    r = &ctx->rules[ctx->rule_count++];
    lim_strlcpy(r->key, key, sizeof(r->key));
    r->kind = LIM_RULE_LIMIT;
    r->op = op;
    r->threshold = threshold;
    r->level = level;
    lim_strlcpy(r->event_id, event_id, sizeof(r->event_id));
    return 0;
}

int lim_add_stale_rule(lim_ctx *ctx, const char *key, double stale_seconds,
                       lim_level level, const char *event_id) {
    lim_rule *r;
    if (!ctx || !lim_valid_name(key) || !lim_valid_name(event_id)) return -1;
    if (stale_seconds < 0.0) return -1;
    if (ctx->rule_count >= LIM_MAX_RULES) return -2;

    r = &ctx->rules[ctx->rule_count++];
    lim_strlcpy(r->key, key, sizeof(r->key));
    r->kind = LIM_RULE_STALE;
    r->op = LIM_OP_GT;
    r->threshold = stale_seconds;
    r->level = level;
    lim_strlcpy(r->event_id, event_id, sizeof(r->event_id));
    return 0;
}

static int lim_parse_rule_options(double *cooldown_seconds, unsigned long line_no,
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
            lim_set_err(err, err_cap, msg);
            return -1;
        }

        if (has_cooldown) {
            snprintf(msg, sizeof(msg), "rules:%lu: duplicate cooldown option", line_no);
            lim_set_err(err, err_cap, msg);
            return -1;
        }

        duration = strtok(NULL, " \t\r\n");
        if (!duration || lim_parse_duration(duration, cooldown_seconds) != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: invalid cooldown duration", line_no);
            lim_set_err(err, err_cap, msg);
            return -1;
        }
        has_cooldown = 1;
    }

    return 0;
}

static int lim_parse_rule_line(lim_ctx *ctx, char *line, unsigned long line_no,
                               char *err, size_t err_cap) {
    char *tok_key, *tok_op, *tok_threshold, *tok_level, *tok_event;
    lim_op op;
    lim_level level;
    double threshold;
    double cooldown_seconds;
    char *end = NULL;
    char msg[160];

    lim_strip_comment(line);
    line = lim_ltrim(line);
    lim_rtrim(line);
    if (*line == '\0') return 0;

    tok_key = strtok(line, " \t\r\n");
    tok_op = strtok(NULL, " \t\r\n");
    tok_threshold = strtok(NULL, " \t\r\n");
    tok_level = strtok(NULL, " \t\r\n");
    tok_event = strtok(NULL, " \t\r\n");

    if (!tok_key || !tok_op || !tok_threshold || !tok_level || !tok_event) {
        snprintf(msg, sizeof(msg), "rules:%lu: expected: <key> <op> <number> <level> <event_id>", line_no);
        lim_set_err(err, err_cap, msg);
        return -1;
    }

    if (!lim_valid_name(tok_key) || !lim_valid_name(tok_event)) {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid key or event id", line_no);
        lim_set_err(err, err_cap, msg);
        return -1;
    }

    if (lim_parse_level(tok_level, &level) != 0) {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid level '%s'", line_no, tok_level);
        lim_set_err(err, err_cap, msg);
        return -1;
    }

    if (lim_parse_rule_options(&cooldown_seconds, line_no, err, err_cap) != 0) {
        return -1;
    }

    if (strcmp(tok_op, "stale") == 0) {
        if (lim_parse_duration(tok_threshold, &threshold) != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: invalid stale duration '%s'", line_no, tok_threshold);
            lim_set_err(err, err_cap, msg);
            return -1;
        }

        if (lim_add_stale_rule(ctx, tok_key, threshold, level, tok_event) != 0) {
            snprintf(msg, sizeof(msg), "rules:%lu: too many rules or invalid rule", line_no);
            lim_set_err(err, err_cap, msg);
            return -1;
        }
        ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;
        return 0;
    }

    if (lim_parse_op(tok_op, &op) != 0) {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid operator '%s'", line_no, tok_op);
        lim_set_err(err, err_cap, msg);
        return -1;
    }

    errno = 0;
    threshold = strtod(tok_threshold, &end);
    if (errno != 0 || !end || *end != '\0') {
        snprintf(msg, sizeof(msg), "rules:%lu: invalid number '%s'", line_no, tok_threshold);
        lim_set_err(err, err_cap, msg);
        return -1;
    }

    if (lim_add_rule(ctx, tok_key, op, threshold, level, tok_event) != 0) {
        snprintf(msg, sizeof(msg), "rules:%lu: too many rules or invalid rule", line_no);
        lim_set_err(err, err_cap, msg);
        return -1;
    }
    ctx->rules[ctx->rule_count - 1].cooldown_seconds = cooldown_seconds;

    return 0;
}

int lim_load_rules_file(lim_ctx *ctx, const char *path, char *err, size_t err_cap) {
    FILE *f;
    char line[LIM_LINE_MAX];
    unsigned long line_no = 0;

    if (!ctx || !path) {
        lim_set_err(err, err_cap, "missing rules path");
        return -1;
    }

    f = fopen(path, "r");
    if (!f) {
        char msg[160];
        snprintf(msg, sizeof(msg), "cannot open rules file '%s': %s", path, strerror(errno));
        lim_set_err(err, err_cap, msg);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        ++line_no;
        if (lim_parse_rule_line(ctx, line, line_no, err, err_cap) != 0) {
            fclose(f);
            return -1;
        }
    }

    if (ferror(f)) {
        lim_set_err(err, err_cap, "error reading rules file");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

static int lim_json_escape(char *dst, size_t cap, const char *src) {
    size_t j = 0;
    if (!dst || cap == 0) return -1;
    if (!src) src = "";

    for (size_t i = 0; src[i]; ++i) {
        unsigned char c = (unsigned char)src[i];
        const char *rep = NULL;
        char hex[7];

        switch (c) {
            case '\\': rep = "\\\\"; break;
            case '"': rep = "\\\""; break;
            case '\b': rep = "\\b"; break;
            case '\f': rep = "\\f"; break;
            case '\n': rep = "\\n"; break;
            case '\r': rep = "\\r"; break;
            case '\t': rep = "\\t"; break;
            default:
                if (c < 0x20) {
                    snprintf(hex, sizeof(hex), "\\u%04x", c);
                    rep = hex;
                }
                break;
        }

        if (rep) {
            size_t rn = strlen(rep);
            if (j + rn >= cap) return -1;
            memcpy(dst + j, rep, rn);
            j += rn;
        } else {
            if (j + 1 >= cap) return -1;
            dst[j++] = (char)c;
        }
    }

    dst[j] = '\0';
    return 0;
}

static int lim_emit_event_json(const lim_rule *rule, double value, char *out, size_t out_cap,
                               time_t now) {
    char key_esc[LIM_MAX_NAME * 2];
    char id_esc[LIM_MAX_NAME * 2];
    if (lim_json_escape(key_esc, sizeof(key_esc), rule->key) != 0) return -1;
    if (lim_json_escape(id_esc, sizeof(id_esc), rule->event_id) != 0) return -1;

    if (!out || out_cap == 0) return -1;
    snprintf(out, out_cap,
             "{\"ts\":%lld,\"level\":\"%s\",\"id\":\"%s\",\"key\":\"%s\",\"op\":\"%s\",\"threshold\":%.17g,\"value\":%.17g}",
             (long long)now,
             lim_level_name(rule->level),
             id_esc,
             key_esc,
             lim_rule_op_name(rule),
             rule->threshold,
             value);
    return 0;
}

static int lim_emit_event_text(const lim_rule *rule, double value, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return -1;
    snprintf(out, out_cap, "%s\t%s\t%s\t%s\t%.17g\t%.17g",
             lim_level_name(rule->level),
             rule->event_id,
             rule->key,
             lim_rule_op_name(rule),
             rule->threshold,
             value);
    return 0;
}

static int lim_emit_event(const lim_rule *rule, double value, lim_format format,
                          char *out, size_t out_cap, time_t now) {
    if (format == LIM_FORMAT_JSON) {
        return lim_emit_event_json(rule, value, out, out_cap, now);
    }
    return lim_emit_event_text(rule, value, out, out_cap);
}

static int lim_record_event(lim_ctx *ctx, lim_rule *rule, double value,
                            lim_format format, char *out, size_t out_cap,
                            time_t now) {
    if (!ctx || !rule) return -1;

    if (rule->cooldown_seconds > 0.0 && rule->has_last_emit_time) {
        double elapsed = difftime(now, (time_t)rule->last_emit_time);
        if (elapsed < rule->cooldown_seconds) return 0;
    }

    rule->last_emit_time = (long long)now;
    rule->has_last_emit_time = 1;

    ctx->events_emitted++;
    if (rule->level == LIM_LEVEL_INFO) ctx->info_count++;
    if (rule->level == LIM_LEVEL_WARN) ctx->warn_count++;
    if (rule->level == LIM_LEVEL_ERROR) ctx->error_count++;

    if (out && out_cap && out[0] == '\0') {
        lim_emit_event(rule, value, format, out, out_cap, now);
    }

    return 1;
}

int lim_sample(lim_ctx *ctx, const char *key, double value, char *out, size_t out_cap) {
    return lim_sample_format(ctx, key, value, LIM_FORMAT_TEXT, out, out_cap);
}

int lim_sample_format(lim_ctx *ctx, const char *key, double value,
                      lim_format format, char *out, size_t out_cap) {
    int emitted = 0;
    time_t now = time(NULL);
    if (!ctx || !key) return -1;
    ctx->samples_seen++;

    if (out && out_cap) out[0] = '\0';

    for (size_t i = 0; i < ctx->rule_count; ++i) {
        lim_rule *r = &ctx->rules[i];
        if (strcmp(r->key, key) != 0) continue;
        if (r->kind == LIM_RULE_STALE) r->seen = 1;
        if (!lim_rule_matches(r, value)) continue;

        emitted += lim_record_event(ctx, r, value, format, out, out_cap, now);
    }

    return emitted;
}

#ifndef LIM_NO_MAIN
static int lim_emit_missing_stale(lim_ctx *ctx, lim_format format) {
    int emitted = 0;
    time_t now = time(NULL);

    if (!ctx) return -1;

    for (size_t i = 0; i < ctx->rule_count; ++i) {
        lim_rule *r = &ctx->rules[i];
        char event_line[512];
        int er;

        if (r->kind != LIM_RULE_STALE || r->seen) continue;

        event_line[0] = '\0';
        er = lim_record_event(ctx, r, r->threshold, format, event_line,
                              sizeof(event_line), now);
        if (er > 0 && event_line[0]) {
            puts(event_line);
            emitted += er;
        }
    }

    return emitted;
}

static void lim_usage(FILE *f) {
    fprintf(f,
        "usage: lim -r rules.lim [--fail-on error|warn|never] [--json] [--summary]\n"
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
        "  --json                 emit JSONL instead of tab-separated text\n"
        "  --summary              print summary to stderr\n"
        "  -h, --help             show help\n"
        "  --version              show version\n");
}

static int lim_parse_sample_line(char *line, char *key, size_t key_cap, double *value) {
    char *p;
    char *v;
    char *end = NULL;

    lim_strip_comment(line);
    line = lim_ltrim(line);
    lim_rtrim(line);
    if (*line == '\0') return 0;

    p = strchr(line, '=');
    if (p) {
        *p = '\0';
        v = p + 1;
        lim_rtrim(line);
        v = lim_ltrim(v);
    } else {
        char *save = NULL;
        char *k = strtok_r(line, " \t\r\n", &save);
        char *val = strtok_r(NULL, " \t\r\n", &save);
        if (!k || !val) return -1;
        lim_strlcpy(key, k, key_cap);
        errno = 0;
        *value = strtod(val, &end);
        if (errno != 0 || !end || *end != '\0') return -1;
        return 1;
    }

    if (!lim_valid_name(line)) return -1;
    lim_strlcpy(key, line, key_cap);
    errno = 0;
    *value = strtod(v, &end);
    if (errno != 0 || !end || *end != '\0') return -1;
    return 1;
}

int main(int argc, char **argv) {
    const char *rules_path = NULL;
    lim_level fail_on = LIM_LEVEL_ERROR;
    int fail_never = 0;
    int summary = 0;
    lim_format format = LIM_FORMAT_TEXT;
    lim_ctx ctx;
    char err[256];
    char line[LIM_LINE_MAX];
    unsigned long input_line = 0;
    int bad_input = 0;

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rules") == 0) && i + 1 < argc) {
            rules_path = argv[++i];
        } else if (strcmp(argv[i], "--fail-on") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            if (strcmp(arg, "never") == 0) {
                fail_never = 1;
            } else if (lim_parse_level(arg, &fail_on) != 0) {
                fprintf(stderr, "lim: invalid --fail-on value '%s'\n", arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--summary") == 0) {
            summary = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            format = LIM_FORMAT_JSON;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            lim_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("lim 0.1.0");
            return 0;
        } else {
            fprintf(stderr, "lim: unknown argument '%s'\n", argv[i]);
            lim_usage(stderr);
            return 2;
        }
    }

    if (!rules_path) {
        fprintf(stderr, "lim: missing -r rules.lim\n");
        lim_usage(stderr);
        return 2;
    }

    lim_init(&ctx);
    if (lim_load_rules_file(&ctx, rules_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "lim: %s\n", err);
        return 2;
    }

    while (fgets(line, sizeof(line), stdin)) {
        char key[LIM_MAX_NAME];
        double value;
        int pr;
        int emitted;
        char event_line[512];

        input_line++;
        pr = lim_parse_sample_line(line, key, sizeof(key), &value);
        if (pr == 0) continue;
        if (pr < 0) {
            fprintf(stderr, "lim: input:%lu: expected key=value or key value\n", input_line);
            bad_input = 1;
            continue;
        }

        emitted = lim_sample_format(&ctx, key, value, format, event_line,
                                    sizeof(event_line));
        if (emitted > 0 && event_line[0]) {
            puts(event_line);
        }
    }

    if (ferror(stdin)) {
        fprintf(stderr, "lim: error reading stdin\n");
        return 2;
    }

    lim_emit_missing_stale(&ctx, format);

    if (summary) {
        fprintf(stderr,
                "lim: samples=%lu events=%lu info=%lu warn=%lu error=%lu rules=%lu\n",
                ctx.samples_seen, ctx.events_emitted, ctx.info_count,
                ctx.warn_count, ctx.error_count, (unsigned long)ctx.rule_count);
    }

    if (bad_input) return 2;
    if (!fail_never) {
        if (fail_on == LIM_LEVEL_ERROR && ctx.error_count > 0) return 1;
        if (fail_on == LIM_LEVEL_WARN && (ctx.warn_count > 0 || ctx.error_count > 0)) return 1;
        if (fail_on == LIM_LEVEL_INFO && ctx.events_emitted > 0) return 1;
    }

    return 0;
}
#endif
