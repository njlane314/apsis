#define _POSIX_C_SOURCE 200809L

#include "ctc.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRIP_VERSION "0.1.0"

static void trip_copy(char *dst, const char *src, size_t cap) {
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
        "  -r, --rules PATH       rules file; use - to read rules from stdin\n"
        "  --fail-on LEVEL        error, warn, info, or never; default: error\n"
        "  --summary              print summary to stderr\n"
        "  -h, --help             show help\n"
        "  --version              show version\n");
}

static int trip_parse_sample_line(char *line,
                                  char *key,
                                  size_t key_cap,
                                  double *value) {
    char *p;
    char *v;
    char *end = NULL;

    if (!line || !key || key_cap == 0 || !value) return -1;

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
        char *sample_key = strtok_r(line, " \t\r\n", &save);
        char *sample_value = strtok_r(NULL, " \t\r\n", &save);

        if (!sample_key || !sample_value) return -1;
        if (!ctc_valid_name(sample_key)) return -1;

        trip_copy(key, sample_key, key_cap);
        errno = 0;
        *value = strtod(sample_value, &end);
        if (errno != 0 || !end || *end != '\0') return -1;
        return 1;
    }

    if (!ctc_valid_name(line)) return -1;
    trip_copy(key, line, key_cap);
    errno = 0;
    *value = strtod(v, &end);
    if (errno != 0 || !end || *end != '\0') return -1;
    return 1;
}

static void trip_put_event(const char *line, void *user) {
    (void)user;
    if (line) puts(line);
}

int main(int argc, char **argv) {
    const char *rules_path = NULL;
    ctc_level fail_on = CTC_ERROR;
    int fail_never = 0;
    int summary = 0;
    ctc_ctx ctx;
    char err[256];
    char line[CTC_LINE_MAX];
    unsigned long input_line = 0;
    int bad_input = 0;
    int rules_from_stdin = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rules") == 0) &&
            i + 1 < argc) {
            rules_path = argv[++i];
        } else if (strcmp(argv[i], "--fail-on") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            if (strcmp(arg, "never") == 0) {
                fail_never = 1;
            } else if (ctc_parse_level(arg, &fail_on) != 0) {
                fprintf(stderr, "trip: invalid --fail-on value '%s'\n", arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--summary") == 0) {
            summary = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            trip_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("trip " TRIP_VERSION);
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

    rules_from_stdin = strcmp(rules_path, "-") == 0;

    ctc_init(&ctx);
    if (ctc_load_rules_file(&ctx, rules_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "trip: %s\n", err);
        return 2;
    }

    while (fgets(line, sizeof(line), stdin)) {
        char key[CTC_MAX_NAME];
        double value;
        int parsed;
        int emitted;
        char event_line[CTC_LINE_MAX];

        input_line++;
        parsed = trip_parse_sample_line(line, key, sizeof(key), &value);
        if (parsed == 0) continue;
        if (parsed < 0) {
            fprintf(stderr, "trip: input:%lu: expected key=value or key value\n",
                    input_line);
            bad_input = 1;
            continue;
        }

        emitted = ctc_sample(&ctx, key, value, event_line, sizeof(event_line));
        if (emitted > 0 && event_line[0]) {
            puts(event_line);
        }
    }

    if (ferror(stdin)) {
        fprintf(stderr, "trip: error reading stdin\n");
        return 2;
    }

    if (!(rules_from_stdin && input_line == 0)) {
        ctc_emit_missing_stale(&ctx, trip_put_event, NULL);
    }

    if (summary) {
        fprintf(stderr,
                "trip: samples=%lu events=%lu info=%lu warn=%lu error=%lu rules=%lu\n",
                ctx.samples_seen,
                ctx.events_emitted,
                ctx.info_count,
                ctx.warn_count,
                ctx.error_count,
                (unsigned long)ctx.rule_count);
    }

    if (bad_input) return 2;
    if (!fail_never) {
        if (fail_on == CTC_ERROR && ctx.error_count > 0) return 1;
        if (fail_on == CTC_WARN && (ctx.warn_count > 0 || ctx.error_count > 0)) {
            return 1;
        }
        if (fail_on == CTC_INFO && ctx.events_emitted > 0) return 1;
    }

    return 0;
}
