#include "dwell.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DWELL_EPSILON
#define DWELL_EPSILON 1e-9
#endif

static void dwell_strlcpy(char *dst, const char *src, size_t cap) {
    size_t i = 0;

    if (cap == 0) {
        return;
    }

    if (!src) {
        src = "";
    }

    for (; i + 1 < cap && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static char *dwell_ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static void dwell_rtrim(char *s) {
    size_t n = strlen(s);

    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        --n;
    }
}

static void dwell_strip_comment(char *s) {
    char *p = strchr(s, '#');

    if (p) {
        *p = '\0';
    }
}

static int dwell_valid_name(const char *s) {
    size_t n;
    size_t i;

    if (!s || !*s) {
        return 0;
    }

    n = strlen(s);
    if (n >= DWELL_MAX_NAME) {
        return 0;
    }

    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' ||
              c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }

    return 1;
}

static void dwell_set_err(char *err, size_t err_cap, const char *msg) {
    if (err && err_cap > 0) {
        dwell_strlcpy(err, msg ? msg : "dwell error", err_cap);
    }
}

static int dwell_copy_name(char dst[DWELL_MAX_NAME], const char *src) {
    if (!dwell_valid_name(src)) {
        return -1;
    }

    dwell_strlcpy(dst, src, DWELL_MAX_NAME);
    return 0;
}

void dwell_init(dwell_ctx *ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

void dwell_set_event_callback(dwell_ctx *ctx, dwell_event_fn fn, void *user) {
    if (!ctx) {
        return;
    }
    ctx->event_cb = fn;
    ctx->event_user = user;
}

void dwell_set_sample_callback(dwell_ctx *ctx, dwell_sample_fn fn, void *user) {
    if (!ctx) {
        return;
    }
    ctx->sample_cb = fn;
    ctx->sample_user = user;
}

int dwell_find_watch(const dwell_ctx *ctx, const char *name) {
    size_t i;

    if (!ctx || !name) {
        return -1;
    }

    for (i = 0; i < ctx->watch_count; ++i) {
        if (strcmp(ctx->watches[i].name, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

int dwell_watch(dwell_ctx *ctx,
                const char *name,
                dwell_type type,
                const volatile void *addr) {
    dwell_watch_t *watch;

    if (!ctx || !name || !addr) {
        return -1;
    }

    if (ctx->watch_count >= DWELL_MAX_WATCHES) {
        return -2;
    }

    if (dwell_find_watch(ctx, name) >= 0) {
        return -3;
    }

    watch = &ctx->watches[ctx->watch_count];
    memset(watch, 0, sizeof(*watch));

    if (dwell_copy_name(watch->name, name) != 0) {
        return -4;
    }

    watch->type = type;
    watch->addr = addr;

    ctx->watch_count++;
    return 0;
}

int dwell_watch_reader(dwell_ctx *ctx,
                       const char *name,
                       dwell_reader_fn reader,
                       void *user) {
    dwell_watch_t *watch;

    if (!ctx || !name || !reader) {
        return -1;
    }

    if (ctx->watch_count >= DWELL_MAX_WATCHES) {
        return -2;
    }

    if (dwell_find_watch(ctx, name) >= 0) {
        return -3;
    }

    watch = &ctx->watches[ctx->watch_count];
    memset(watch, 0, sizeof(*watch));

    if (dwell_copy_name(watch->name, name) != 0) {
        return -4;
    }

    watch->type = DWELL_READER;
    watch->reader = reader;
    watch->reader_user = user;

    ctx->watch_count++;
    return 0;
}

int dwell_watch_i32(dwell_ctx *ctx, const char *name, const volatile int32_t *addr) {
    return dwell_watch(ctx, name, DWELL_I32, addr);
}

int dwell_watch_u32(dwell_ctx *ctx, const char *name, const volatile uint32_t *addr) {
    return dwell_watch(ctx, name, DWELL_U32, addr);
}

int dwell_watch_i64(dwell_ctx *ctx, const char *name, const volatile int64_t *addr) {
    return dwell_watch(ctx, name, DWELL_I64, addr);
}

int dwell_watch_u64(dwell_ctx *ctx, const char *name, const volatile uint64_t *addr) {
    return dwell_watch(ctx, name, DWELL_U64, addr);
}

int dwell_watch_f32(dwell_ctx *ctx, const char *name, const volatile float *addr) {
    return dwell_watch(ctx, name, DWELL_F32, addr);
}

int dwell_watch_f64(dwell_ctx *ctx, const char *name, const volatile double *addr) {
    return dwell_watch(ctx, name, DWELL_F64, addr);
}

int dwell_watch_bool(dwell_ctx *ctx, const char *name, const volatile int *addr) {
    return dwell_watch(ctx, name, DWELL_BOOL, addr);
}

int dwell_add_contract(dwell_ctx *ctx,
                       const char *key,
                       dwell_op op,
                       double threshold,
                       dwell_level level,
                       const char *event_id) {
    dwell_contract_t *contract;

    if (!ctx || !key || !event_id) {
        return -1;
    }

    if (ctx->contract_count >= DWELL_MAX_CONTRACTS) {
        return -2;
    }

    contract = &ctx->contracts[ctx->contract_count];
    memset(contract, 0, sizeof(*contract));

    if (dwell_copy_name(contract->key, key) != 0) {
        return -3;
    }
    if (dwell_copy_name(contract->event_id, event_id) != 0) {
        return -4;
    }

    contract->op = op;
    contract->threshold = threshold;
    contract->level = level;

    ctx->contract_count++;
    return 0;
}

const char *dwell_type_name(dwell_type type) {
    switch (type) {
        case DWELL_I32: return "i32";
        case DWELL_U32: return "u32";
        case DWELL_I64: return "i64";
        case DWELL_U64: return "u64";
        case DWELL_F32: return "f32";
        case DWELL_F64: return "f64";
        case DWELL_BOOL: return "bool";
        case DWELL_READER: return "reader";
        default: return "unknown";
    }
}

const char *dwell_level_name(dwell_level level) {
    switch (level) {
        case DWELL_INFO: return "info";
        case DWELL_WARN: return "warn";
        case DWELL_ERROR: return "error";
        default: return "unknown";
    }
}

const char *dwell_op_name(dwell_op op) {
    switch (op) {
        case DWELL_GT: return ">";
        case DWELL_GTE: return ">=";
        case DWELL_LT: return "<";
        case DWELL_LTE: return "<=";
        case DWELL_EQ: return "==";
        case DWELL_NEQ: return "!=";
        default: return "?";
    }
}

int dwell_parse_level(const char *s, dwell_level *out) {
    if (!s || !out) {
        return -1;
    }

    if (strcmp(s, "info") == 0) {
        *out = DWELL_INFO;
        return 0;
    }
    if (strcmp(s, "warn") == 0 || strcmp(s, "warning") == 0) {
        *out = DWELL_WARN;
        return 0;
    }
    if (strcmp(s, "error") == 0 || strcmp(s, "err") == 0) {
        *out = DWELL_ERROR;
        return 0;
    }

    return -1;
}

int dwell_parse_op(const char *s, dwell_op *out) {
    if (!s || !out) {
        return -1;
    }

    if (strcmp(s, ">") == 0) {
        *out = DWELL_GT;
        return 0;
    }
    if (strcmp(s, ">=") == 0) {
        *out = DWELL_GTE;
        return 0;
    }
    if (strcmp(s, "<") == 0) {
        *out = DWELL_LT;
        return 0;
    }
    if (strcmp(s, "<=") == 0) {
        *out = DWELL_LTE;
        return 0;
    }
    if (strcmp(s, "==") == 0 || strcmp(s, "=") == 0) {
        *out = DWELL_EQ;
        return 0;
    }
    if (strcmp(s, "!=") == 0) {
        *out = DWELL_NEQ;
        return 0;
    }

    return -1;
}

int dwell_contract_matches(const dwell_contract_t *contract, double value) {
    if (!contract) {
        return 0;
    }

    switch (contract->op) {
        case DWELL_GT: return value > contract->threshold;
        case DWELL_GTE: return value >= contract->threshold;
        case DWELL_LT: return value < contract->threshold;
        case DWELL_LTE: return value <= contract->threshold;
        case DWELL_EQ: return (value > contract->threshold ? value - contract->threshold : contract->threshold - value) <= DWELL_EPSILON;
        case DWELL_NEQ: return (value > contract->threshold ? value - contract->threshold : contract->threshold - value) > DWELL_EPSILON;
        default: return 0;
    }
}

int dwell_contract(dwell_ctx *ctx,
                   const char *rule,
                   char *err,
                   size_t err_cap) {
    char line[DWELL_MAX_LINE];
    char *tok_key;
    char *tok_op;
    char *tok_threshold;
    char *tok_level;
    char *tok_event;
    char *end = NULL;
    double threshold;
    dwell_op op;
    dwell_level level;

    if (!ctx || !rule) {
        dwell_set_err(err, err_cap, "null context or rule");
        return -1;
    }

    dwell_strlcpy(line, rule, sizeof(line));
    dwell_strip_comment(line);
    tok_key = dwell_ltrim(line);
    dwell_rtrim(tok_key);
    if (*tok_key == '\0') {
        return 0;
    }

    tok_key = strtok(tok_key, " \t\r\n");
    tok_op = strtok(NULL, " \t\r\n");
    tok_threshold = strtok(NULL, " \t\r\n");
    tok_level = strtok(NULL, " \t\r\n");
    tok_event = strtok(NULL, " \t\r\n");

    if (!tok_key || !tok_op || !tok_threshold || !tok_level || !tok_event) {
        dwell_set_err(err, err_cap, "rule must be: <key> <op> <number> <level> <event_id>");
        return -2;
    }

    if (!dwell_valid_name(tok_key) || !dwell_valid_name(tok_event)) {
        dwell_set_err(err, err_cap, "invalid key or event id");
        return -3;
    }

    if (dwell_parse_op(tok_op, &op) != 0) {
        dwell_set_err(err, err_cap, "invalid operator; expected one of > >= < <= == !=");
        return -4;
    }

    errno = 0;
    threshold = strtod(tok_threshold, &end);
    if (errno != 0 || !end || *end != '\0') {
        dwell_set_err(err, err_cap, "invalid number");
        return -5;
    }

    if (dwell_parse_level(tok_level, &level) != 0) {
        dwell_set_err(err, err_cap, "invalid level; expected info, warn, or error");
        return -6;
    }

    return dwell_add_contract(ctx, tok_key, op, threshold, level, tok_event);
}

int dwell_load_contracts_file(dwell_ctx *ctx,
                              const char *path,
                              char *err,
                              size_t err_cap) {
    FILE *fp;
    char line[DWELL_MAX_LINE];
    unsigned long line_no = 0;
    int rc;

    if (!ctx || !path) {
        dwell_set_err(err, err_cap, "null context or path");
        return -1;
    }

    fp = fopen(path, "r");
    if (!fp) {
        char tmp[DWELL_MAX_ERROR];
        snprintf(tmp, sizeof(tmp), "cannot open contract file '%s': %s",
                 path, strerror(errno));
        dwell_set_err(err, err_cap, tmp);
        return -2;
    }

    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        rc = dwell_contract(ctx, line, err, err_cap);
        if (rc < 0) {
            char tmp[DWELL_MAX_ERROR];
            snprintf(tmp, sizeof(tmp), "contracts:%lu: %s",
                     line_no, err ? err : "invalid rule");
            dwell_set_err(err, err_cap, tmp);
            fclose(fp);
            return rc;
        }
    }

    fclose(fp);
    return 0;
}

static double dwell_read_watch(const dwell_watch_t *watch) {
    if (!watch) {
        return 0.0;
    }

    switch (watch->type) {
        case DWELL_I32:
            return (double)(*(const volatile int32_t *)watch->addr);
        case DWELL_U32:
            return (double)(*(const volatile uint32_t *)watch->addr);
        case DWELL_I64:
            return (double)(*(const volatile int64_t *)watch->addr);
        case DWELL_U64:
            return (double)(*(const volatile uint64_t *)watch->addr);
        case DWELL_F32:
            return (double)(*(const volatile float *)watch->addr);
        case DWELL_F64:
            return (double)(*(const volatile double *)watch->addr);
        case DWELL_BOOL:
            return (*(const volatile int *)watch->addr) ? 1.0 : 0.0;
        case DWELL_READER:
            return watch->reader ? watch->reader(watch->reader_user) : 0.0;
        default:
            return 0.0;
    }
}

static void dwell_emit_event(dwell_ctx *ctx,
                             dwell_contract_t *contract,
                             double value) {
    dwell_event event;
    char line[DWELL_MAX_LINE];

    if (!ctx || !contract) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.tick = ctx->tick_count;
    event.level = contract->level;
    dwell_copy_name(event.id, contract->event_id);
    dwell_copy_name(event.key, contract->key);
    event.op = contract->op;
    event.threshold = contract->threshold;
    event.value = value;
    event.fired_count = contract->fired_count;

    dwell_format_event(&event, line, sizeof(line));

    if (ctx->event_cb) {
        ctx->event_cb(&event, line, ctx->event_user);
    } else {
        puts(line);
    }
}

static void dwell_count_event(dwell_ctx *ctx, dwell_level level) {
    ctx->event_count++;
    switch (level) {
        case DWELL_INFO: ctx->info_count++; break;
        case DWELL_WARN: ctx->warn_count++; break;
        case DWELL_ERROR: ctx->error_count++; break;
        default: break;
    }
}

int dwell_format_event(const dwell_event *event, char *out, size_t out_cap) {
    if (!event || !out || out_cap == 0) {
        return -1;
    }

    snprintf(out, out_cap, "%s\t%s\t%s\t%s\t%.17g\t%.17g",
             dwell_level_name(event->level),
             event->id,
             event->key,
             dwell_op_name(event->op),
             event->threshold,
             event->value);
    return 0;
}

int dwell_format_sample(const char *key, double value, char *out, size_t out_cap) {
    if (!dwell_valid_name(key) || !out || out_cap == 0) {
        return -1;
    }

    snprintf(out, out_cap, "%s=%.17g", key, value);
    return 0;
}

int dwell_sample_value(dwell_ctx *ctx, const char *key, double value) {
    size_t i;
    int emitted = 0;

    if (!ctx || !key) {
        return -1;
    }

    ctx->sample_count++;

    if (ctx->sample_cb) {
        ctx->sample_cb(key, value, ctx->sample_user);
    }

    for (i = 0; i < ctx->contract_count; ++i) {
        dwell_contract_t *contract = &ctx->contracts[i];

        if (strcmp(contract->key, key) != 0) {
            continue;
        }

        if (dwell_contract_matches(contract, value)) {
            contract->fired_count++;
            dwell_count_event(ctx, contract->level);
            dwell_emit_event(ctx, contract, value);
            emitted++;
        }
    }

    return emitted;
}

#ifndef DWELL_NO_MAIN
static int dwell_parse_sample_line(char *line,
                                   char *key,
                                   size_t key_cap,
                                   double *value) {
    char *p;
    char *v;
    char *end = NULL;

    if (!line || !key || key_cap == 0 || !value) {
        return -1;
    }

    dwell_strip_comment(line);
    line = dwell_ltrim(line);
    dwell_rtrim(line);
    if (*line == '\0') {
        return 0;
    }

    p = strchr(line, '=');
    if (p) {
        *p = '\0';
        v = dwell_ltrim(p + 1);
        dwell_rtrim(line);
    } else {
        char *tok_key = strtok(line, " \t\r\n");
        char *tok_value = strtok(NULL, " \t\r\n");

        if (!tok_key || !tok_value) {
            return -1;
        }

        dwell_strlcpy(key, tok_key, key_cap);
        errno = 0;
        *value = strtod(tok_value, &end);
        if (errno != 0 || !end || *end != '\0' || !dwell_valid_name(key)) {
            return -1;
        }
        return 1;
    }

    if (!dwell_valid_name(line)) {
        return -1;
    }

    dwell_strlcpy(key, line, key_cap);
    errno = 0;
    *value = strtod(v, &end);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }

    return 1;
}
#endif

int dwell_tick(dwell_ctx *ctx) {
    size_t i;
    int emitted = 0;

    if (!ctx) {
        return -1;
    }

    ctx->tick_count++;

    for (i = 0; i < ctx->watch_count; ++i) {
        dwell_watch_t *watch = &ctx->watches[i];
        double value = dwell_read_watch(watch);
        int rc;

        watch->last_value = value;
        watch->sample_count++;

        rc = dwell_sample_value(ctx, watch->name, value);
        if (rc < 0) {
            return rc;
        }
        emitted += rc;
    }

    return emitted;
}

#ifndef DWELL_NO_MAIN
static void dwell_usage(FILE *f) {
    fprintf(f,
        "usage: dwell -r contracts.dwell [--fail-on error|warn|never] [--summary]\n"
        "\n"
        "Read key=value samples from stdin and emit events for contracts that fire.\n"
        "\n"
        "Contracts:\n"
        "  <key> <op> <number> <level> <event_id>\n"
        "\n"
        "Example contracts:\n"
        "  temperature > 80 warn temperature.high\n"
        "  queue.depth > 1000 error queue.backpressure\n"
        "\n"
        "Input:\n"
        "  temperature=82.4\n"
        "  queue.depth 1402\n"
        "\n"
        "Output:\n"
        "  <level>\\t<event_id>\\t<key>\\t<op>\\t<threshold>\\t<value>\n"
        "\n"
        "Options:\n"
        "  -r, --contracts PATH   contracts file\n"
        "      --rules PATH       alias for --contracts\n"
        "  --fail-on LEVEL        error, warn, info, or never; default: error\n"
        "  --summary              print summary to stderr\n"
        "  -h, --help             show help\n"
        "  --version              show version\n");
}

int main(int argc, char **argv) {
    const char *contracts_path = NULL;
    dwell_level fail_on = DWELL_ERROR;
    int fail_never = 0;
    int summary = 0;
    int bad_input = 0;
    unsigned long input_line = 0;
    dwell_ctx ctx;
    char err[DWELL_MAX_ERROR];
    char line[DWELL_MAX_LINE];

    int i;

    for (i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-r") == 0 ||
             strcmp(argv[i], "--contracts") == 0 ||
             strcmp(argv[i], "--rules") == 0) &&
            i + 1 < argc) {
            contracts_path = argv[++i];
        } else if (strcmp(argv[i], "--fail-on") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            if (strcmp(arg, "never") == 0) {
                fail_never = 1;
            } else if (dwell_parse_level(arg, &fail_on) != 0) {
                fprintf(stderr, "dwell: invalid --fail-on value '%s'\n", arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--summary") == 0) {
            summary = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            dwell_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("dwell 0.1.0");
            return 0;
        } else {
            fprintf(stderr, "dwell: unknown argument '%s'\n", argv[i]);
            dwell_usage(stderr);
            return 2;
        }
    }

    if (!contracts_path) {
        fprintf(stderr, "dwell: missing -r contracts.dwell\n");
        dwell_usage(stderr);
        return 2;
    }

    dwell_init(&ctx);
    if (dwell_load_contracts_file(&ctx, contracts_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "dwell: %s\n", err);
        return 2;
    }

    while (fgets(line, sizeof(line), stdin)) {
        char key[DWELL_MAX_NAME];
        double value;
        int parsed;

        input_line++;
        parsed = dwell_parse_sample_line(line, key, sizeof(key), &value);
        if (parsed == 0) {
            continue;
        }
        if (parsed < 0) {
            fprintf(stderr, "dwell: input:%lu: expected key=value or key value\n",
                    input_line);
            bad_input = 1;
            continue;
        }

        if (dwell_sample_value(&ctx, key, value) < 0) {
            fprintf(stderr, "dwell: failed to sample '%s'\n", key);
            return 2;
        }
    }

    if (ferror(stdin)) {
        fprintf(stderr, "dwell: error reading stdin\n");
        return 2;
    }

    if (summary) {
        fprintf(stderr,
                "dwell: samples=%lu events=%lu info=%lu warn=%lu error=%lu contracts=%lu\n",
                ctx.sample_count,
                ctx.event_count,
                ctx.info_count,
                ctx.warn_count,
                ctx.error_count,
                (unsigned long)ctx.contract_count);
    }

    if (bad_input) {
        return 2;
    }

    if (!fail_never) {
        if (fail_on == DWELL_ERROR && ctx.error_count > 0) {
            return 1;
        }
        if (fail_on == DWELL_WARN &&
            (ctx.warn_count > 0 || ctx.error_count > 0)) {
            return 1;
        }
        if (fail_on == DWELL_INFO && ctx.event_count > 0) {
            return 1;
        }
    }

    return 0;
}
#endif
