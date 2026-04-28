/*
 * bound.c - learn reviewable contract suggestions from known-good samples
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOUND_LINE_MAX     4096
#define BOUND_MAX_KEYS      512
#define BOUND_MAX_NAME      128
#define BOUND_MAX_LEVEL      16
#define BOUND_EXIT_OK         0
#define BOUND_EXIT_FAIL       1
#define BOUND_EXIT_USAGE      2

typedef enum bound_emit {
    BOUND_EMIT_REPORT = 0,
    BOUND_EMIT_ATLAS_PATCH,
    BOUND_EMIT_JSON
} bound_emit;

typedef struct bound_options {
    bound_emit emit;
    double margin;
    unsigned long min_samples;
    char include[BOUND_MAX_NAME];
    char exclude[BOUND_MAX_NAME];
    char level[BOUND_MAX_LEVEL];
    char **files;
    int file_count;
} bound_options;

typedef struct bound_stat {
    char key[BOUND_MAX_NAME];
    unsigned long count;
    double min;
    double max;
    double mean;
    double m2;
} bound_stat;

typedef struct bound_model {
    bound_stat stats[BOUND_MAX_KEYS];
    size_t count;
    unsigned long malformed;
} bound_model;

static void bound_usage(FILE *out) {
    fprintf(out,
            "usage: bound learn [FILES...] [options]\n"
            "\n"
            "Learn conservative, review-required contract suggestions from samples.\n"
            "\n"
            "Options:\n"
            "  --emit report|atlas-patch|json  output mode; default report\n"
            "  --margin PERCENT                20%% or 0.20; default 20%%\n"
            "  --min-samples N                 default 20\n"
            "  --include TEXT                  include keys containing TEXT\n"
            "  --exclude TEXT                  exclude keys containing TEXT\n"
            "  --level warn|error              generated limit level; default error\n"
            "  -h, --help                      show help\n");
}

static int bound_streq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static void bound_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;

    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char *bound_ltrim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void bound_rtrim(char *s) {
    size_t n;

    if (!s) return;
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static char *bound_next_token(char **cursor) {
    char *start;

    if (!cursor || !*cursor) return NULL;
    start = *cursor;
    while (*start && isspace((unsigned char)*start)) start++;
    if (!*start) {
        *cursor = start;
        return NULL;
    }
    *cursor = start;
    while (**cursor && !isspace((unsigned char)**cursor)) (*cursor)++;
    if (**cursor) {
        **cursor = '\0';
        (*cursor)++;
    }
    return start;
}

static int bound_valid_name(const char *s) {
    size_t i;

    if (!s || !*s || strlen(s) >= BOUND_MAX_NAME) return 0;
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];

        if (!(isalnum(c) || c == '_' || c == '-' ||
              c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }
    return 1;
}

static int bound_parse_double(const char *s, double *out) {
    char *end = NULL;
    double value;

    if (!s || !*s || !out) return -1;
    errno = 0;
    value = strtod(s, &end);
    if (errno != 0 || !end || end == s || *end != '\0') return -1;
    if (value != value || value > DBL_MAX || value < -DBL_MAX) return -1;
    *out = value;
    return 0;
}

static int bound_parse_margin(const char *s, double *out) {
    char tmp[64];
    size_t n;
    double value;

    if (!s || !*s || !out || strlen(s) >= sizeof(tmp)) return -1;
    bound_copy(tmp, s, sizeof(tmp));
    n = strlen(tmp);
    if (n > 0 && tmp[n - 1] == '%') {
        tmp[n - 1] = '\0';
        if (bound_parse_double(tmp, &value) != 0) return -1;
        value /= 100.0;
    } else {
        if (bound_parse_double(tmp, &value) != 0) return -1;
    }
    if (value < 0.0 || value > 10.0) return -1;
    *out = value;
    return 0;
}

static int bound_parse_ulong(const char *s, unsigned long *out) {
    char *end = NULL;
    unsigned long value;

    if (!s || !*s || !out) return -1;
    errno = 0;
    value = strtoul(s, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0 ||
        value > 1000000000UL) {
        return -1;
    }
    *out = value;
    return 0;
}

static int bound_parse_emit(const char *s, bound_emit *out) {
    if (!s || !out) return -1;
    if (bound_streq(s, "report")) *out = BOUND_EMIT_REPORT;
    else if (bound_streq(s, "atlas-patch")) *out = BOUND_EMIT_ATLAS_PATCH;
    else if (bound_streq(s, "json")) *out = BOUND_EMIT_JSON;
    else return -1;
    return 0;
}

static int bound_valid_level(const char *s) {
    return bound_streq(s, "warn") || bound_streq(s, "error");
}

static int bound_parse_args(int argc, char **argv, bound_options *opt) {
    int i;

    if (!opt) return -1;
    memset(opt, 0, sizeof(*opt));
    opt->emit = BOUND_EMIT_REPORT;
    opt->margin = 0.20;
    opt->min_samples = 20;
    bound_copy(opt->level, "error", sizeof(opt->level));

    if (argc == 2 &&
        (bound_streq(argv[1], "-h") || bound_streq(argv[1], "--help"))) {
        bound_usage(stdout);
        exit(BOUND_EXIT_OK);
    }
    if (argc < 2 || !bound_streq(argv[1], "learn")) return -1;
    opt->files = &argv[2];
    for (i = 2; i < argc; ++i) {
        const char *arg = argv[i];

        if (bound_streq(arg, "-h") || bound_streq(arg, "--help")) {
            bound_usage(stdout);
            exit(BOUND_EXIT_OK);
        } else if (bound_streq(arg, "--emit")) {
            if (++i >= argc || bound_parse_emit(argv[i], &opt->emit) != 0) {
                return -1;
            }
        } else if (bound_streq(arg, "--margin")) {
            if (++i >= argc || bound_parse_margin(argv[i], &opt->margin) != 0) {
                return -1;
            }
        } else if (bound_streq(arg, "--min-samples")) {
            if (++i >= argc ||
                bound_parse_ulong(argv[i], &opt->min_samples) != 0) {
                return -1;
            }
        } else if (bound_streq(arg, "--include")) {
            if (++i >= argc || strlen(argv[i]) >= sizeof(opt->include)) return -1;
            bound_copy(opt->include, argv[i], sizeof(opt->include));
        } else if (bound_streq(arg, "--exclude")) {
            if (++i >= argc || strlen(argv[i]) >= sizeof(opt->exclude)) return -1;
            bound_copy(opt->exclude, argv[i], sizeof(opt->exclude));
        } else if (bound_streq(arg, "--level")) {
            if (++i >= argc || !bound_valid_level(argv[i])) return -1;
            bound_copy(opt->level, argv[i], sizeof(opt->level));
        } else if (arg[0] == '-') {
            return -1;
        } else {
            opt->files[opt->file_count++] = argv[i];
        }
    }
    return 0;
}

static int bound_key_selected(const bound_options *opt, const char *key) {
    if (!opt || !key) return 0;
    if (opt->include[0] && !strstr(key, opt->include)) return 0;
    if (opt->exclude[0] && strstr(key, opt->exclude)) return 0;
    return 1;
}

static bound_stat *bound_find_stat(bound_model *model, const char *key) {
    size_t i;

    if (!model || !key) return NULL;
    for (i = 0; i < model->count; ++i) {
        if (bound_streq(model->stats[i].key, key)) return &model->stats[i];
    }
    return NULL;
}

static int bound_add_sample(bound_model *model, const char *key, double value) {
    bound_stat *stat;
    double delta;
    double delta2;

    if (!model || !key) return -1;
    stat = bound_find_stat(model, key);
    if (!stat) {
        if (model->count >= BOUND_MAX_KEYS) return -1;
        stat = &model->stats[model->count++];
        memset(stat, 0, sizeof(*stat));
        bound_copy(stat->key, key, sizeof(stat->key));
        stat->min = value;
        stat->max = value;
    }

    stat->count++;
    if (value < stat->min) stat->min = value;
    if (value > stat->max) stat->max = value;
    delta = value - stat->mean;
    stat->mean += delta / (double)stat->count;
    delta2 = value - stat->mean;
    stat->m2 += delta * delta2;
    return 0;
}

static int bound_parse_json_string(const char *line,
                                   const char *field,
                                   char *out,
                                   size_t out_cap) {
    char needle[64];
    const char *p;
    size_t used = 0;

    if (!line || !field || !out || out_cap == 0) return -1;
    if (snprintf(needle, sizeof(needle), "\"%s\"", field) >= (int)sizeof(needle)) {
        return -1;
    }
    p = strstr(line, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return -1;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') return -1;
        if (used + 1 >= out_cap) return -1;
        out[used++] = *p++;
    }
    if (*p != '"') return -1;
    out[used] = '\0';
    return 0;
}

static int bound_parse_json_number(const char *line,
                                   const char *field,
                                   double *out) {
    char needle[64];
    const char *p;
    char value[128];
    size_t used = 0;

    if (!line || !field || !out) return -1;
    if (snprintf(needle, sizeof(needle), "\"%s\"", field) >= (int)sizeof(needle)) {
        return -1;
    }
    p = strstr(line, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p || *p == 'n' || *p == '"') return -1;
    while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
        if (used + 1 >= sizeof(value)) return -1;
        value[used++] = *p++;
    }
    value[used] = '\0';
    return bound_parse_double(value, out);
}

static int bound_parse_kv_tokens(char *line,
                                 char *key,
                                 size_t key_cap,
                                 double *value) {
    char *cursor = line;
    char *tok;
    const char *found_key = NULL;
    const char *found_value = NULL;

    while ((tok = bound_next_token(&cursor)) != NULL) {
        if (strncmp(tok, "key=", 4) == 0) found_key = tok + 4;
        else if (strncmp(tok, "name=", 5) == 0) found_key = tok + 5;
        else if (strncmp(tok, "value=", 6) == 0) found_value = tok + 6;
    }
    if (!found_key || !found_value || !bound_valid_name(found_key)) return -1;
    if (strlen(found_key) >= key_cap) return -1;
    if (bound_parse_double(found_value, value) != 0) return -1;
    bound_copy(key, found_key, key_cap);
    return 0;
}

static int bound_parse_sample_line(char *line,
                                   char *key,
                                   size_t key_cap,
                                   double *value,
                                   int *ignored) {
    char original[BOUND_LINE_MAX];
    char *text;
    char *eq;
    char *cursor;
    char *first;
    char *second;

    if (!line || !key || !value || !ignored) return -1;
    *ignored = 0;
    bound_rtrim(line);
    text = bound_ltrim(line);
    if (!*text || *text == '#') {
        *ignored = 1;
        return 0;
    }
    if ((strncmp(text, "info\t", 5) == 0) ||
        (strncmp(text, "warn\t", 5) == 0) ||
        (strncmp(text, "error\t", 6) == 0)) {
        *ignored = 1;
        return 0;
    }
    if (*text == '{') {
        if (bound_parse_json_string(text, "name", key, key_cap) != 0 &&
            bound_parse_json_string(text, "key", key, key_cap) != 0) {
            return -1;
        }
        if (!bound_valid_name(key)) return -1;
        return bound_parse_json_number(text, "value", value);
    }

    bound_copy(original, text, sizeof(original));
    if (strstr(text, "key=") && strstr(text, "value=")) {
        return bound_parse_kv_tokens(original, key, key_cap, value);
    }

    eq = strchr(text, '=');
    if (eq) {
        *eq = '\0';
        bound_rtrim(text);
        text = bound_ltrim(text);
        if (!bound_valid_name(text) || strlen(text) >= key_cap) return -1;
        if (bound_parse_double(bound_ltrim(eq + 1), value) != 0) return -1;
        bound_copy(key, text, key_cap);
        return 0;
    }

    cursor = text;
    first = bound_next_token(&cursor);
    second = bound_next_token(&cursor);
    if (!first || !second || bound_next_token(&cursor) != NULL) return -1;
    if (!bound_valid_name(first) || strlen(first) >= key_cap) return -1;
    if (bound_parse_double(second, value) != 0) return -1;
    bound_copy(key, first, key_cap);
    return 0;
}

static int bound_process_stream(bound_model *model,
                                const bound_options *opt,
                                FILE *in,
                                const char *label) {
    char line[BOUND_LINE_MAX];
    unsigned long line_no = 0;

    while (fgets(line, sizeof(line), in)) {
        char key[BOUND_MAX_NAME];
        double value;
        int ignored = 0;

        line_no++;
        if (!strchr(line, '\n') && !feof(in)) {
            fprintf(stderr, "bound: %s:%lu: line too long\n", label, line_no);
            return -1;
        }
        if (bound_parse_sample_line(line,
                                    key,
                                    sizeof(key),
                                    &value,
                                    &ignored) != 0) {
            fprintf(stderr,
                    "bound: %s:%lu: ignoring malformed sample\n",
                    label,
                    line_no);
            model->malformed++;
            continue;
        }
        if (!ignored && bound_key_selected(opt, key) &&
            bound_add_sample(model, key, value) != 0) {
            fprintf(stderr, "bound: too many keys\n");
            return -1;
        }
    }
    if (ferror(in)) return -1;
    return 0;
}

static int bound_process_file(bound_model *model,
                              const bound_options *opt,
                              const char *path) {
    FILE *in;
    int rc;

    if (bound_streq(path, "-")) {
        return bound_process_stream(model, opt, stdin, "stdin");
    }
    in = fopen(path, "r");
    if (!in) {
        fprintf(stderr, "bound: %s: %s\n", path, strerror(errno));
        return -1;
    }
    rc = bound_process_stream(model, opt, in, path);
    fclose(in);
    return rc;
}

static double bound_sqrt(double value) {
    double x;
    int i;

    if (value <= 0.0) return 0.0;
    x = value >= 1.0 ? value : 1.0;
    for (i = 0; i < 32; ++i) {
        x = 0.5 * (x + value / x);
    }
    return x;
}

static double bound_stddev(const bound_stat *stat) {
    if (!stat || stat->count < 2) return 0.0;
    return bound_sqrt(stat->m2 / (double)(stat->count - 1));
}

static int bound_enough(const bound_stat *stat, const bound_options *opt) {
    return stat && opt && stat->count >= opt->min_samples;
}

static void bound_event_id(const char *key,
                           const char *suffix,
                           char *out,
                           size_t out_cap) {
    size_t i;
    size_t used = 0;

    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!key) key = "metric";
    for (i = 0; key[i] && used + 1 < out_cap; ++i) {
        unsigned char c = (unsigned char)key[i];

        if (isalnum(c) || c == '.') out[used++] = (char)c;
        else out[used++] = '_';
    }
    out[used] = '\0';
    if (suffix && used + strlen(suffix) < out_cap) strcat(out, suffix);
}

static void bound_print_high_limit(const bound_stat *stat,
                                   const bound_options *opt,
                                   FILE *out) {
    char event_id[BOUND_MAX_NAME];
    double threshold = stat->max * (1.0 + opt->margin);

    bound_event_id(stat->key, ".high", event_id, sizeof(event_id));
    fprintf(out,
            "limit %s > %.12g %s %s\n",
            stat->key,
            threshold,
            opt->level,
            event_id);
}

static void bound_print_low_limit(const bound_stat *stat,
                                  const bound_options *opt,
                                  FILE *out) {
    char event_id[BOUND_MAX_NAME];
    double threshold = stat->min * (1.0 - opt->margin);

    if (threshold < 0.0) threshold = 0.0;
    bound_event_id(stat->key, ".low", event_id, sizeof(event_id));
    fprintf(out,
            "limit %s < %.12g %s %s\n",
            stat->key,
            threshold,
            opt->level,
            event_id);
}

static void bound_emit_report(const bound_model *model,
                              const bound_options *opt) {
    size_t i;

    for (i = 0; i < model->count; ++i) {
        const bound_stat *stat = &model->stats[i];

        printf("key: %s\n", stat->key);
        printf("  samples: %lu\n", stat->count);
        printf("  min: %.12g\n", stat->min);
        printf("  max: %.12g\n", stat->max);
        printf("  mean: %.12g\n", stat->mean);
        printf("  stddev: %.12g\n", bound_stddev(stat));
        if (bound_enough(stat, opt) && stat->max > 0.0) {
            puts("  suggested:");
            fputs("    ", stdout);
            bound_print_high_limit(stat, opt, stdout);
            if (stat->min > 0.0 && stat->min < stat->max) {
                fputs("    ", stdout);
                bound_print_low_limit(stat, opt, stdout);
            }
        } else {
            printf("  suggested: skipped; needs at least %lu samples\n",
                   opt->min_samples);
        }
        if (i + 1 < model->count) putchar('\n');
    }
}

static void bound_emit_atlas_patch(const bound_model *model,
                                   const bound_options *opt) {
    size_t i;

    puts("# learned by bound; review before committing");
    puts("# generated limits are candidate contracts, not safety evidence");
    for (i = 0; i < model->count; ++i) {
        const bound_stat *stat = &model->stats[i];

        if (!bound_enough(stat, opt) || stat->max <= 0.0) continue;
        printf("# key=%s samples=%lu min=%.12g max=%.12g margin=%.6g%%\n",
               stat->key,
               stat->count,
               stat->min,
               stat->max,
               opt->margin * 100.0);
        bound_print_high_limit(stat, opt, stdout);
        if (stat->min > 0.0 && stat->min < stat->max) {
            bound_print_low_limit(stat, opt, stdout);
        }
    }
}

static void bound_json_string(FILE *out, const char *s) {
    fputc('"', out);
    while (s && *s) {
        unsigned char c = (unsigned char)*s;

        if (c == '\\' || c == '"') {
            fputc('\\', out);
            fputc(c, out);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c == '\r') {
            fputs("\\r", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (c < 32 || c > 126) {
            fprintf(out, "\\u%04x", c);
        } else {
            fputc(c, out);
        }
        s++;
    }
    fputc('"', out);
}

static void bound_emit_json(const bound_model *model,
                            const bound_options *opt) {
    size_t i;

    puts("[");
    for (i = 0; i < model->count; ++i) {
        const bound_stat *stat = &model->stats[i];

        fputs("  {\"key\":", stdout);
        bound_json_string(stdout, stat->key);
        printf(",\"samples\":%lu,\"min\":%.12g,\"max\":%.12g",
               stat->count,
               stat->min,
               stat->max);
        printf(",\"mean\":%.12g,\"stddev\":%.12g,\"eligible\":%s}",
               stat->mean,
               bound_stddev(stat),
               bound_enough(stat, opt) ? "true" : "false");
        puts(i + 1 < model->count ? "," : "");
    }
    puts("]");
}

int main(int argc, char **argv) {
    bound_options opt;
    bound_model model;
    int i;

    if (bound_parse_args(argc, argv, &opt) != 0) {
        bound_usage(stderr);
        return BOUND_EXIT_USAGE;
    }
    memset(&model, 0, sizeof(model));
    if (opt.file_count == 0) {
        if (bound_process_stream(&model, &opt, stdin, "stdin") != 0) {
            return BOUND_EXIT_FAIL;
        }
    } else {
        for (i = 0; i < opt.file_count; ++i) {
            if (bound_process_file(&model, &opt, opt.files[i]) != 0) {
                return BOUND_EXIT_FAIL;
            }
        }
    }
    if (model.count == 0) {
        fprintf(stderr, "bound: no numeric samples found\n");
        return BOUND_EXIT_FAIL;
    }

    if (opt.emit == BOUND_EMIT_REPORT) bound_emit_report(&model, &opt);
    else if (opt.emit == BOUND_EMIT_ATLAS_PATCH) {
        bound_emit_atlas_patch(&model, &opt);
    } else {
        bound_emit_json(&model, &opt);
    }
    return BOUND_EXIT_OK;
}
