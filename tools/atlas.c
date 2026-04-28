/*
 * atlas.c - small command/telemetry dictionary compiler
 *
 * Grammar, line-oriented:
 *   telemetry <key> <type> <unit> <description...>
 *   limit     <key> <op> <value> <level> <event_id> [cooldown <duration>]
 *   command   <name> <description...>
 *   arg       <command> <name> <type> <description...>
 *
 * Aliases:
 *   metric   == telemetry
 *   contract == limit
 *
 * Comments begin with # outside quoted strings. Quoted strings may contain spaces.
 * Example:
 *   telemetry renderer.frame.ms f64 ms "Frame render time"
 *   limit renderer.frame.ms > 16.6 warn frame.slow cooldown 5s
 *
 * Build:
 *   cc -std=c99 -Wall -Wextra -Wpedantic -O2 atlas.c -o atlas
 *
 * License: MIT-style; keep or replace with your repository LICENSE.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_VERSION "0.1.0"

#define ATLAS_LINE_MAX       1024
#define ATLAS_TOKEN_MAX      256
#define ATLAS_MAX_TOKENS     32
#define ATLAS_MAX_NAME       128
#define ATLAS_MAX_TYPE        32
#define ATLAS_MAX_UNIT        32
#define ATLAS_MAX_VALUE       64
#define ATLAS_MAX_LEVEL       16
#define ATLAS_MAX_DESC       256
#define ATLAS_MAX_TELEMETRY  512
#define ATLAS_MAX_LIMITS    1024
#define ATLAS_MAX_COMMANDS   256
#define ATLAS_MAX_ARGS      1024

#define ATLAS_EXIT_OK      0
#define ATLAS_EXIT_FAIL    1
#define ATLAS_EXIT_USAGE   2

typedef struct atlas_token {
    char text[ATLAS_TOKEN_MAX];
} atlas_token;

typedef struct atlas_telemetry {
    char key[ATLAS_MAX_NAME];
    char type[ATLAS_MAX_TYPE];
    char unit[ATLAS_MAX_UNIT];
    char desc[ATLAS_MAX_DESC];
    unsigned long line_no;
} atlas_telemetry;

typedef struct atlas_limit {
    char key[ATLAS_MAX_NAME];
    char op[8];
    char value[ATLAS_MAX_VALUE];
    char level[ATLAS_MAX_LEVEL];
    char event_id[ATLAS_MAX_NAME];
    char cooldown[ATLAS_MAX_VALUE];
    unsigned long line_no;
} atlas_limit;

typedef struct atlas_command {
    char name[ATLAS_MAX_NAME];
    char desc[ATLAS_MAX_DESC];
    unsigned long line_no;
} atlas_command;

typedef struct atlas_arg {
    char command[ATLAS_MAX_NAME];
    char name[ATLAS_MAX_NAME];
    char type[ATLAS_MAX_TYPE];
    char desc[ATLAS_MAX_DESC];
    unsigned long line_no;
} atlas_arg;

typedef struct atlas_model {
    atlas_telemetry telemetry[ATLAS_MAX_TELEMETRY];
    atlas_limit limits[ATLAS_MAX_LIMITS];
    atlas_command commands[ATLAS_MAX_COMMANDS];
    atlas_arg args[ATLAS_MAX_ARGS];
    size_t telemetry_count;
    size_t limit_count;
    size_t command_count;
    size_t arg_count;
} atlas_model;

static void atlas_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void atlas_set_err(char *err, size_t err_cap, const char *msg) {
    atlas_copy(err, msg, err_cap);
}

static char *atlas_ltrim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void atlas_rtrim(char *s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int atlas_streq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static void atlas_strip_comment(char *s) {
    int in_quote = 0;
    int escaped = 0;
    size_t i;

    if (!s) return;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (escaped) {
            escaped = 0;
            continue;
        }
        if (in_quote && c == '\\') {
            escaped = 1;
            continue;
        }
        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }
        if (!in_quote && c == '#') {
            s[i] = '\0';
            return;
        }
    }
}

static int atlas_valid_name(const char *s) {
    size_t n;
    size_t i;

    if (!s || !*s) return 0;
    n = strlen(s);
    if (n >= ATLAS_MAX_NAME) return 0;

    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':' || c == '/')) {
            return 0;
        }
    }
    return 1;
}

static int atlas_valid_unit(const char *s) {
    size_t i;
    if (!s || !*s || strlen(s) >= ATLAS_MAX_UNIT) return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/' || c == '%' || c == '*')) {
            return 0;
        }
    }
    return 1;
}

static int atlas_valid_type(const char *s) {
    static const char *types[] = {
        "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "f32", "f64", "string", "enum", "duration", NULL
    };
    size_t i;
    if (!s || !*s || strlen(s) >= ATLAS_MAX_TYPE) return 0;
    for (i = 0; types[i]; i++) {
        if (strcmp(s, types[i]) == 0) return 1;
    }
    return 0;
}

static int atlas_valid_op(const char *s) {
    return atlas_streq(s, ">") || atlas_streq(s, ">=") ||
           atlas_streq(s, "<") || atlas_streq(s, "<=") ||
           atlas_streq(s, "==") || atlas_streq(s, "!=") ||
           atlas_streq(s, "stale");
}

static int atlas_valid_level(const char *s) {
    return atlas_streq(s, "info") || atlas_streq(s, "warn") ||
           atlas_streq(s, "warning") || atlas_streq(s, "error") ||
           atlas_streq(s, "err");
}

static int atlas_valid_number(const char *s) {
    char *end = NULL;
    if (!s || !*s) return 0;
    errno = 0;
    (void)strtod(s, &end);
    return errno == 0 && end && end != s && *end == '\0';
}

static int atlas_valid_duration(const char *s) {
    char *end = NULL;
    if (!s || !*s) return 0;
    errno = 0;
    (void)strtod(s, &end);
    if (errno != 0 || !end || end == s) return 0;
    return strcmp(end, "") == 0 || strcmp(end, "ms") == 0 || strcmp(end, "s") == 0 ||
           strcmp(end, "m") == 0 || strcmp(end, "h") == 0;
}

static int atlas_tokenize(const char *line, atlas_token toks[], size_t *out_count,
                          char *err, size_t err_cap) {
    const char *p = line;
    size_t count = 0;

    if (!line || !toks || !out_count) return -1;
    *out_count = 0;

    while (*p) {
        size_t len = 0;
        int quoted = 0;

        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (count >= ATLAS_MAX_TOKENS) {
            atlas_set_err(err, err_cap, "too many tokens");
            return -1;
        }

        if (*p == '"') {
            quoted = 1;
            p++;
        }

        while (*p) {
            char c = *p;

            if (quoted) {
                if (c == '"') {
                    p++;
                    break;
                }
                if (c == '\\') {
                    p++;
                    if (!*p) {
                        atlas_set_err(err, err_cap, "unterminated escape in quoted string");
                        return -1;
                    }
                    c = *p;
                    if (c == 'n') c = '\n';
                    else if (c == 't') c = '\t';
                    else if (c == 'r') c = '\r';
                    else if (c == '"') c = '"';
                    else if (c == '\\') c = '\\';
                    else {
                        atlas_set_err(err, err_cap, "unsupported escape in quoted string");
                        return -1;
                    }
                }
            } else {
                if (isspace((unsigned char)c)) break;
            }

            if (len + 1 >= ATLAS_TOKEN_MAX) {
                atlas_set_err(err, err_cap, "token too long");
                return -1;
            }
            toks[count].text[len++] = c;
            p++;
        }

        if (quoted && p[-1] != '"') {
            atlas_set_err(err, err_cap, "unterminated quoted string");
            return -1;
        }

        toks[count].text[len] = '\0';
        count++;
    }

    *out_count = count;
    return 0;
}

static void atlas_join_tokens(atlas_token toks[], size_t first, size_t count,
                              char *out, size_t out_cap) {
    size_t i;
    size_t used = 0;

    if (!out || out_cap == 0) return;
    out[0] = '\0';

    for (i = first; i < count; i++) {
        const char *s = toks[i].text;
        size_t j;

        if (i > first && used + 1 < out_cap) {
            out[used++] = ' ';
            out[used] = '\0';
        }
        for (j = 0; s[j] && used + 1 < out_cap; j++) {
            out[used++] = s[j];
        }
        out[used] = '\0';
    }
}

static int atlas_find_telemetry(const atlas_model *m, const char *key) {
    size_t i;
    if (!m || !key) return -1;
    for (i = 0; i < m->telemetry_count; i++) {
        if (strcmp(m->telemetry[i].key, key) == 0) return (int)i;
    }
    return -1;
}

static int atlas_find_command(const atlas_model *m, const char *name) {
    size_t i;
    if (!m || !name) return -1;
    for (i = 0; i < m->command_count; i++) {
        if (strcmp(m->commands[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int atlas_find_arg(const atlas_model *m, const char *command, const char *name) {
    size_t i;
    if (!m || !command || !name) return -1;
    for (i = 0; i < m->arg_count; i++) {
        if (strcmp(m->args[i].command, command) == 0 && strcmp(m->args[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void atlas_model_init(atlas_model *m) {
    if (m) memset(m, 0, sizeof(*m));
}

static int atlas_parse_telemetry(atlas_model *m, atlas_token toks[], size_t ntok,
                                 unsigned long line_no, char *err, size_t err_cap) {
    atlas_telemetry *t;
    char msg[768];

    if (ntok < 5) {
        snprintf(msg, sizeof(msg), "%lu: expected: telemetry <key> <type> <unit> <description>", line_no);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_name(toks[1].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid telemetry key '%s'", line_no, toks[1].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_type(toks[2].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid telemetry type '%s'", line_no, toks[2].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_unit(toks[3].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid telemetry unit '%s'", line_no, toks[3].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (atlas_find_telemetry(m, toks[1].text) >= 0) {
        snprintf(msg, sizeof(msg), "%lu: duplicate telemetry key '%s'", line_no, toks[1].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (m->telemetry_count >= ATLAS_MAX_TELEMETRY) {
        atlas_set_err(err, err_cap, "too many telemetry entries");
        return -1;
    }

    t = &m->telemetry[m->telemetry_count++];
    atlas_copy(t->key, toks[1].text, sizeof(t->key));
    atlas_copy(t->type, toks[2].text, sizeof(t->type));
    atlas_copy(t->unit, toks[3].text, sizeof(t->unit));
    atlas_join_tokens(toks, 4, ntok, t->desc, sizeof(t->desc));
    t->line_no = line_no;
    return 0;
}

static int atlas_parse_limit(atlas_model *m, atlas_token toks[], size_t ntok,
                             unsigned long line_no, char *err, size_t err_cap) {
    atlas_limit *l;
    char msg[768];

    if (!(ntok == 6 || ntok == 8)) {
        snprintf(msg,
                 sizeof(msg),
                 "%lu: expected: limit <key> <op> <value> <level> "
                 "<event_id> [cooldown <duration>]",
                 line_no);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_name(toks[1].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid limit key '%s'", line_no, toks[1].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_op(toks[2].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid operator '%s'", line_no, toks[2].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (atlas_streq(toks[2].text, "stale")) {
        if (!atlas_valid_duration(toks[3].text)) {
            snprintf(msg, sizeof(msg), "%lu: invalid stale duration '%s'", line_no, toks[3].text);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }
    } else {
        if (!atlas_valid_number(toks[3].text)) {
            snprintf(msg, sizeof(msg), "%lu: invalid numeric limit '%s'", line_no, toks[3].text);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }
    }
    if (!atlas_valid_level(toks[4].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid level '%s'", line_no, toks[4].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_name(toks[5].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid event id '%s'", line_no, toks[5].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (ntok == 8) {
        if (!atlas_streq(toks[6].text, "cooldown")) {
            snprintf(msg, sizeof(msg), "%lu: unknown limit option '%s'", line_no, toks[6].text);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }
        if (!atlas_valid_duration(toks[7].text)) {
            snprintf(msg, sizeof(msg), "%lu: invalid cooldown duration '%s'", line_no, toks[7].text);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }
    }
    if (m->limit_count >= ATLAS_MAX_LIMITS) {
        atlas_set_err(err, err_cap, "too many limits");
        return -1;
    }

    l = &m->limits[m->limit_count++];
    atlas_copy(l->key, toks[1].text, sizeof(l->key));
    atlas_copy(l->op, toks[2].text, sizeof(l->op));
    atlas_copy(l->value, toks[3].text, sizeof(l->value));
    atlas_copy(l->level, toks[4].text, sizeof(l->level));
    atlas_copy(l->event_id, toks[5].text, sizeof(l->event_id));
    if (ntok == 8) atlas_copy(l->cooldown, toks[7].text, sizeof(l->cooldown));
    l->line_no = line_no;
    return 0;
}

static int atlas_parse_command(atlas_model *m, atlas_token toks[], size_t ntok,
                               unsigned long line_no, char *err, size_t err_cap) {
    atlas_command *c;
    char msg[768];

    if (ntok < 3) {
        snprintf(msg, sizeof(msg), "%lu: expected: command <name> <description>", line_no);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_name(toks[1].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid command name '%s'", line_no, toks[1].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (atlas_find_command(m, toks[1].text) >= 0) {
        snprintf(msg, sizeof(msg), "%lu: duplicate command '%s'", line_no, toks[1].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (m->command_count >= ATLAS_MAX_COMMANDS) {
        atlas_set_err(err, err_cap, "too many commands");
        return -1;
    }

    c = &m->commands[m->command_count++];
    atlas_copy(c->name, toks[1].text, sizeof(c->name));
    atlas_join_tokens(toks, 2, ntok, c->desc, sizeof(c->desc));
    c->line_no = line_no;
    return 0;
}

static int atlas_parse_arg(atlas_model *m, atlas_token toks[], size_t ntok,
                           unsigned long line_no, char *err, size_t err_cap) {
    atlas_arg *a;
    char msg[768];

    if (ntok < 5) {
        snprintf(msg, sizeof(msg), "%lu: expected: arg <command> <name> <type> <description>", line_no);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_name(toks[1].text) || !atlas_valid_name(toks[2].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid command or arg name", line_no);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (!atlas_valid_type(toks[3].text)) {
        snprintf(msg, sizeof(msg), "%lu: invalid arg type '%s'", line_no, toks[3].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (atlas_find_arg(m, toks[1].text, toks[2].text) >= 0) {
        snprintf(msg, sizeof(msg), "%lu: duplicate arg '%s' for command '%s'", line_no, toks[2].text, toks[1].text);
        atlas_set_err(err, err_cap, msg);
        return -1;
    }
    if (m->arg_count >= ATLAS_MAX_ARGS) {
        atlas_set_err(err, err_cap, "too many command arguments");
        return -1;
    }

    a = &m->args[m->arg_count++];
    atlas_copy(a->command, toks[1].text, sizeof(a->command));
    atlas_copy(a->name, toks[2].text, sizeof(a->name));
    atlas_copy(a->type, toks[3].text, sizeof(a->type));
    atlas_join_tokens(toks, 4, ntok, a->desc, sizeof(a->desc));
    a->line_no = line_no;
    return 0;
}

static int atlas_validate_model(const atlas_model *m, char *err, size_t err_cap) {
    size_t i;
    char msg[768];

    if (!m) return -1;

    for (i = 0; i < m->limit_count; i++) {
        const atlas_limit *l = &m->limits[i];
        if (atlas_find_telemetry(m, l->key) < 0) {
            snprintf(msg, sizeof(msg), "%lu: limit references unknown telemetry key '%s'", l->line_no, l->key);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }
    }

    for (i = 0; i < m->arg_count; i++) {
        const atlas_arg *a = &m->args[i];
        if (atlas_find_command(m, a->command) < 0) {
            snprintf(msg, sizeof(msg), "%lu: arg references unknown command '%s'", a->line_no, a->command);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }
    }

    return 0;
}

static int atlas_load_file(atlas_model *m, const char *path, char *err, size_t err_cap) {
    FILE *f;
    char line[ATLAS_LINE_MAX];
    unsigned long line_no = 0;

    if (!m || !path) {
        atlas_set_err(err, err_cap, "missing dictionary path");
        return -1;
    }

    f = fopen(path, "r");
    if (!f) {
        char msg[768];
        snprintf(msg, sizeof(msg), "cannot open '%s': %s", path, strerror(errno));
        atlas_set_err(err, err_cap, msg);
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p;
        atlas_token toks[ATLAS_MAX_TOKENS];
        size_t ntok = 0;
        char tok_err[128];

        line_no++;
        if (!strchr(line, '\n') && !feof(f)) {
            char msg[768];
            snprintf(msg, sizeof(msg), "%lu: line too long", line_no);
            atlas_set_err(err, err_cap, msg);
            fclose(f);
            return -1;
        }

        atlas_strip_comment(line);
        p = atlas_ltrim(line);
        atlas_rtrim(p);
        if (*p == '\0') continue;

        tok_err[0] = '\0';
        if (atlas_tokenize(p, toks, &ntok, tok_err, sizeof(tok_err)) != 0) {
            char msg[768];
            snprintf(msg, sizeof(msg), "%lu: %s", line_no, tok_err[0] ? tok_err : "invalid tokens");
            atlas_set_err(err, err_cap, msg);
            fclose(f);
            return -1;
        }
        if (ntok == 0) continue;

        if (atlas_streq(toks[0].text, "telemetry") || atlas_streq(toks[0].text, "metric")) {
            if (atlas_parse_telemetry(m, toks, ntok, line_no, err, err_cap) != 0) {
                fclose(f);
                return -1;
            }
        } else if (atlas_streq(toks[0].text, "limit") || atlas_streq(toks[0].text, "contract")) {
            if (atlas_parse_limit(m, toks, ntok, line_no, err, err_cap) != 0) {
                fclose(f);
                return -1;
            }
        } else if (atlas_streq(toks[0].text, "command")) {
            if (atlas_parse_command(m, toks, ntok, line_no, err, err_cap) != 0) {
                fclose(f);
                return -1;
            }
        } else if (atlas_streq(toks[0].text, "arg")) {
            if (atlas_parse_arg(m, toks, ntok, line_no, err, err_cap) != 0) {
                fclose(f);
                return -1;
            }
        } else {
            char msg[768];
            snprintf(msg, sizeof(msg), "%lu: unknown directive '%s'", line_no, toks[0].text);
            atlas_set_err(err, err_cap, msg);
            fclose(f);
            return -1;
        }
    }

    if (ferror(f)) {
        atlas_set_err(err, err_cap, "error reading dictionary");
        fclose(f);
        return -1;
    }
    fclose(f);

    return atlas_validate_model(m, err, err_cap);
}

static const char *atlas_level_canonical(const char *level) {
    if (atlas_streq(level, "warning")) return "warn";
    if (atlas_streq(level, "err")) return "error";
    return level;
}

static void atlas_emit_rules(const atlas_model *m, FILE *out) {
    size_t i;
    for (i = 0; i < m->limit_count; i++) {
        const atlas_limit *l = &m->limits[i];
        fprintf(out, "%-32s %-6s %-12s %-6s %s",
                l->key, l->op, l->value, atlas_level_canonical(l->level), l->event_id);
        if (l->cooldown[0]) fprintf(out, " cooldown %s", l->cooldown);
        fputc('\n', out);
    }
}

static void atlas_md_cell(FILE *out, const char *s) {
    if (!s) return;
    while (*s) {
        if (*s == '|') fputc('\\', out);
        if (*s == '\n' || *s == '\r') fputc(' ', out);
        else fputc(*s, out);
        s++;
    }
}

static void atlas_emit_doc(const atlas_model *m, FILE *out) {
    size_t i;

    fprintf(out, "# Telemetry Dictionary\n\n");
    fprintf(out, "Generated from an atlas dictionary.\n\n");

    fprintf(out, "## Telemetry\n\n");
    fprintf(out, "| Key | Type | Unit | Description |\n");
    fprintf(out, "|---|---:|---:|---|\n");
    for (i = 0; i < m->telemetry_count; i++) {
        const atlas_telemetry *t = &m->telemetry[i];
        fprintf(out, "| "); atlas_md_cell(out, t->key);
        fprintf(out, " | "); atlas_md_cell(out, t->type);
        fprintf(out, " | "); atlas_md_cell(out, t->unit);
        fprintf(out, " | "); atlas_md_cell(out, t->desc);
        fprintf(out, " |\n");
    }

    fprintf(out, "\n## Limits\n\n");
    fprintf(out, "| Key | Op | Value | Level | Event | Cooldown |\n");
    fprintf(out, "|---|---:|---:|---:|---|---:|\n");
    for (i = 0; i < m->limit_count; i++) {
        const atlas_limit *l = &m->limits[i];
        fprintf(out, "| "); atlas_md_cell(out, l->key);
        fprintf(out, " | "); atlas_md_cell(out, l->op);
        fprintf(out, " | "); atlas_md_cell(out, l->value);
        fprintf(out, " | "); atlas_md_cell(out, atlas_level_canonical(l->level));
        fprintf(out, " | "); atlas_md_cell(out, l->event_id);
        fprintf(out, " | "); atlas_md_cell(out, l->cooldown[0] ? l->cooldown : "-");
        fprintf(out, " |\n");
    }

    fprintf(out, "\n## Commands\n\n");
    fprintf(out, "| Command | Description |\n");
    fprintf(out, "|---|---|\n");
    for (i = 0; i < m->command_count; i++) {
        const atlas_command *c = &m->commands[i];
        fprintf(out, "| "); atlas_md_cell(out, c->name);
        fprintf(out, " | "); atlas_md_cell(out, c->desc);
        fprintf(out, " |\n");
    }

    fprintf(out, "\n## Command Arguments\n\n");
    fprintf(out, "| Command | Argument | Type | Description |\n");
    fprintf(out, "|---|---|---:|---|\n");
    for (i = 0; i < m->arg_count; i++) {
        const atlas_arg *a = &m->args[i];
        fprintf(out, "| "); atlas_md_cell(out, a->command);
        fprintf(out, " | "); atlas_md_cell(out, a->name);
        fprintf(out, " | "); atlas_md_cell(out, a->type);
        fprintf(out, " | "); atlas_md_cell(out, a->desc);
        fprintf(out, " |\n");
    }
}

static void atlas_ident_from_name(const char *name, char *out, size_t out_cap) {
    size_t used = 0;
    size_t i;
    int last_us = 0;

    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!name) return;

    for (i = 0; name[i] && used + 1 < out_cap; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c)) {
            out[used++] = (char)toupper(c);
            last_us = 0;
        } else {
            if (!last_us && used + 1 < out_cap) {
                out[used++] = '_';
                last_us = 1;
            }
        }
    }
    while (used > 0 && out[used - 1] == '_') used--;
    out[used] = '\0';
}

static int atlas_check_header_id(const char *kind,
                                 const char *first_name,
                                 unsigned long first_line,
                                 const char *second_name,
                                 unsigned long second_line,
                                 const char *id,
                                 char *err,
                                 size_t err_cap) {
    char msg[768];

    snprintf(msg,
             sizeof(msg),
             "%lu: %s macro id collision '%s': '%s' also maps from line %lu key/name '%s'",
             second_line,
             kind,
             id,
             second_name,
             first_line,
             first_name);
    atlas_set_err(err, err_cap, msg);
    return -1;
}

static int atlas_validate_header_ids(const atlas_model *m,
                                     char *err,
                                     size_t err_cap) {
    size_t i;
    size_t j;

    if (!m) return -1;

    for (i = 0; i < m->telemetry_count; i++) {
        char left[ATLAS_MAX_NAME * 2];

        atlas_ident_from_name(m->telemetry[i].key, left, sizeof(left));
        if (left[0] == '\0') {
            char msg[768];
            snprintf(msg,
                     sizeof(msg),
                     "%lu: telemetry key '%s' maps to an empty C macro id",
                     m->telemetry[i].line_no,
                     m->telemetry[i].key);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }

        for (j = i + 1; j < m->telemetry_count; j++) {
            char right[ATLAS_MAX_NAME * 2];

            atlas_ident_from_name(m->telemetry[j].key, right, sizeof(right));
            if (strcmp(left, right) == 0) {
                return atlas_check_header_id("telemetry",
                                             m->telemetry[i].key,
                                             m->telemetry[i].line_no,
                                             m->telemetry[j].key,
                                             m->telemetry[j].line_no,
                                             left,
                                             err,
                                             err_cap);
            }
        }
    }

    for (i = 0; i < m->command_count; i++) {
        char left[ATLAS_MAX_NAME * 2];

        atlas_ident_from_name(m->commands[i].name, left, sizeof(left));
        if (left[0] == '\0') {
            char msg[768];
            snprintf(msg,
                     sizeof(msg),
                     "%lu: command name '%s' maps to an empty C macro id",
                     m->commands[i].line_no,
                     m->commands[i].name);
            atlas_set_err(err, err_cap, msg);
            return -1;
        }

        for (j = i + 1; j < m->command_count; j++) {
            char right[ATLAS_MAX_NAME * 2];

            atlas_ident_from_name(m->commands[j].name, right, sizeof(right));
            if (strcmp(left, right) == 0) {
                return atlas_check_header_id("command",
                                             m->commands[i].name,
                                             m->commands[i].line_no,
                                             m->commands[j].name,
                                             m->commands[j].line_no,
                                             left,
                                             err,
                                             err_cap);
            }
        }
    }

    return 0;
}

static int atlas_valid_prefix(const char *s) {
    size_t i;
    if (!s || !*s) return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_')) return 0;
    }
    return 1;
}

static void atlas_emit_c_string(FILE *out, const char *s) {
    fputc('"', out);
    if (s) {
        while (*s) {
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
                fprintf(out, "\\x%02x", c);
            } else {
                fputc(c, out);
            }
            s++;
        }
    }
    fputc('"', out);
}

static void atlas_emit_header(const atlas_model *m, const char *prefix, FILE *out) {
    size_t i;
    char id[ATLAS_MAX_NAME * 2];

    fprintf(out, "#ifndef %sATLAS_GENERATED_H\n", prefix);
    fprintf(out, "#define %sATLAS_GENERATED_H\n\n", prefix);
    fprintf(out, "/* Generated by atlas %s. Do not edit by hand. */\n\n", ATLAS_VERSION);

    fprintf(out, "/* Telemetry keys. */\n");
    for (i = 0; i < m->telemetry_count; i++) {
        atlas_ident_from_name(m->telemetry[i].key, id, sizeof(id));
        fprintf(out, "#define %sTEL_%s ", prefix, id);
        atlas_emit_c_string(out, m->telemetry[i].key);
        fputc('\n', out);
    }

    fprintf(out, "\n/* Command names. */\n");
    for (i = 0; i < m->command_count; i++) {
        atlas_ident_from_name(m->commands[i].name, id, sizeof(id));
        fprintf(out, "#define %sCMD_%s ", prefix, id);
        atlas_emit_c_string(out, m->commands[i].name);
        fputc('\n', out);
    }

    fprintf(out, "\n#endif /* %sATLAS_GENERATED_H */\n", prefix);
}

static void atlas_usage(FILE *out) {
    fprintf(out,
        "usage:\n"
        "  atlas check FILE\n"
        "  atlas emit rules FILE\n"
        "  atlas emit doc FILE\n"
        "  atlas emit header [--prefix PREFIX] FILE\n"
        "  atlas rules FILE\n"
        "  atlas doc FILE\n"
        "  atlas header [--prefix PREFIX] FILE\n"
        "\n"
        "Dictionary lines:\n"
        "  telemetry <key> <type> <unit> <description...>\n"
        "  limit     <key> <op> <value> <level> <event_id> [cooldown <duration>]\n"
        "  command   <name> <description...>\n"
        "  arg       <command> <name> <type> <description...>\n"
        "\n"
        "Types: bool i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 string enum duration\n"
        "Ops:   > >= < <= == != stale\n"
        "Levels: info warn error\n"
        "\n"
        "Output goes to stdout. Redirect it. Keep the tool boring.\n");
}

static int atlas_load_or_print(const char *path, atlas_model *m) {
    char err[256];
    atlas_model_init(m);
    err[0] = '\0';
    if (atlas_load_file(m, path, err, sizeof(err)) != 0) {
        fprintf(stderr, "atlas: %s\n", err[0] ? err : "invalid dictionary");
        return ATLAS_EXIT_FAIL;
    }
    return ATLAS_EXIT_OK;
}

int main(int argc, char **argv) {
    const char *cmd;
    atlas_model model;
    int rc;

    if (argc < 2) {
        atlas_usage(stderr);
        return ATLAS_EXIT_USAGE;
    }

    cmd = argv[1];
    if (atlas_streq(cmd, "-h") || atlas_streq(cmd, "--help")) {
        atlas_usage(stdout);
        return ATLAS_EXIT_OK;
    }
    if (atlas_streq(cmd, "--version")) {
        puts("atlas " ATLAS_VERSION);
        return ATLAS_EXIT_OK;
    }

    if (atlas_streq(cmd, "check")) {
        if (argc != 3) {
            atlas_usage(stderr);
            return ATLAS_EXIT_USAGE;
        }
        rc = atlas_load_or_print(argv[2], &model);
        if (rc != ATLAS_EXIT_OK) return rc;
        fprintf(stdout, "atlas: ok: telemetry=%lu limits=%lu commands=%lu args=%lu\n",
                (unsigned long)model.telemetry_count,
                (unsigned long)model.limit_count,
                (unsigned long)model.command_count,
                (unsigned long)model.arg_count);
        return ATLAS_EXIT_OK;
    }

    if (atlas_streq(cmd, "emit")) {
        const char *kind;
        const char *prefix = "APSIS_";
        const char *path = NULL;
        int i;

        if (argc < 4) {
            atlas_usage(stderr);
            return ATLAS_EXIT_USAGE;
        }

        kind = argv[2];
        if (atlas_streq(kind, "rules")) {
            if (argc != 4) {
                atlas_usage(stderr);
                return ATLAS_EXIT_USAGE;
            }
            rc = atlas_load_or_print(argv[3], &model);
            if (rc != ATLAS_EXIT_OK) return rc;
            atlas_emit_rules(&model, stdout);
            return ATLAS_EXIT_OK;
        }
        if (atlas_streq(kind, "doc")) {
            if (argc != 4) {
                atlas_usage(stderr);
                return ATLAS_EXIT_USAGE;
            }
            rc = atlas_load_or_print(argv[3], &model);
            if (rc != ATLAS_EXIT_OK) return rc;
            atlas_emit_doc(&model, stdout);
            return ATLAS_EXIT_OK;
        }
        if (atlas_streq(kind, "header")) {
            for (i = 3; i < argc; i++) {
                if (atlas_streq(argv[i], "--prefix") && i + 1 < argc) {
                    prefix = argv[++i];
                } else if (!path) {
                    path = argv[i];
                } else {
                    atlas_usage(stderr);
                    return ATLAS_EXIT_USAGE;
                }
            }
            if (!path || !atlas_valid_prefix(prefix)) {
                atlas_usage(stderr);
                return ATLAS_EXIT_USAGE;
            }
            rc = atlas_load_or_print(path, &model);
            if (rc != ATLAS_EXIT_OK) return rc;
            {
                char err[256];
                err[0] = '\0';
                if (atlas_validate_header_ids(&model, err, sizeof(err)) != 0) {
                    fprintf(stderr, "atlas: %s\n",
                            err[0] ? err : "header identifier collision");
                    return ATLAS_EXIT_FAIL;
                }
            }
            atlas_emit_header(&model, prefix, stdout);
            return ATLAS_EXIT_OK;
        }

        fprintf(stderr, "atlas: unknown emit kind '%s'\n", kind);
        atlas_usage(stderr);
        return ATLAS_EXIT_USAGE;
    }

    if (atlas_streq(cmd, "rules")) {
        if (argc != 3) {
            atlas_usage(stderr);
            return ATLAS_EXIT_USAGE;
        }
        rc = atlas_load_or_print(argv[2], &model);
        if (rc != ATLAS_EXIT_OK) return rc;
        atlas_emit_rules(&model, stdout);
        return ATLAS_EXIT_OK;
    }

    if (atlas_streq(cmd, "doc")) {
        if (argc != 3) {
            atlas_usage(stderr);
            return ATLAS_EXIT_USAGE;
        }
        rc = atlas_load_or_print(argv[2], &model);
        if (rc != ATLAS_EXIT_OK) return rc;
        atlas_emit_doc(&model, stdout);
        return ATLAS_EXIT_OK;
    }

    if (atlas_streq(cmd, "header")) {
        const char *prefix = "APSIS_";
        const char *path = NULL;
        int i;
        for (i = 2; i < argc; i++) {
            if (atlas_streq(argv[i], "--prefix") && i + 1 < argc) {
                prefix = argv[++i];
            } else if (!path) {
                path = argv[i];
            } else {
                atlas_usage(stderr);
                return ATLAS_EXIT_USAGE;
            }
        }
        if (!path || !atlas_valid_prefix(prefix)) {
            atlas_usage(stderr);
            return ATLAS_EXIT_USAGE;
        }
        rc = atlas_load_or_print(path, &model);
        if (rc != ATLAS_EXIT_OK) return rc;
        {
            char err[256];
            err[0] = '\0';
            if (atlas_validate_header_ids(&model, err, sizeof(err)) != 0) {
                fprintf(stderr, "atlas: %s\n",
                        err[0] ? err : "header identifier collision");
                return ATLAS_EXIT_FAIL;
            }
        }
        atlas_emit_header(&model, prefix, stdout);
        return ATLAS_EXIT_OK;
    }

    fprintf(stderr, "atlas: unknown command '%s'\n", cmd);
    atlas_usage(stderr);
    return ATLAS_EXIT_USAGE;
}
