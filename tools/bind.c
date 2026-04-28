/*
 * bind.c - source-to-probe binding manifest compiler
 *
 * Grammar, line-oriented:
 *   source <key> [type] symbol <symbol> [type <type>] [object <path>]
 *   source <key> [type] addr   <address> [type <type>] [object <path>]
 *
 * Examples:
 *   source motor.temperature f32 symbol motor_state.temperature_c
 *   source imu.temperature_c f32 addr 0x7ffd1234
 */

#define _POSIX_C_SOURCE 200809L

#include "apsis.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <elf.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define BIND_LINE_MAX     1024
#define BIND_MAX_TOKENS     16
#define BIND_MAX_SOURCES   512
#define BIND_MAX_NAME      128
#define BIND_MAX_TYPE       16
#define BIND_MAX_SYMBOL    128
#define BIND_MAX_ADDR       64
#define BIND_MAX_PATH     4096
#define BIND_MAX_ELF_SECTIONS 4096

#define BIND_EXIT_OK       0
#define BIND_EXIT_FAIL     1
#define BIND_EXIT_USAGE    2

typedef enum bind_kind {
    BIND_KIND_NONE = 0,
    BIND_KIND_SYMBOL,
    BIND_KIND_ADDR
} bind_kind;

typedef struct bind_source {
    char key[BIND_MAX_NAME];
    char type[BIND_MAX_TYPE];
    char symbol[BIND_MAX_SYMBOL];
    char addr[BIND_MAX_ADDR];
    char object[BIND_MAX_PATH];
    bind_kind kind;
    unsigned long line_no;
} bind_source;

typedef struct bind_model {
    bind_source sources[BIND_MAX_SOURCES];
    size_t source_count;
} bind_model;

typedef struct bind_telemetry {
    char key[BIND_MAX_NAME];
    char type[BIND_MAX_TYPE];
    unsigned long line_no;
} bind_telemetry;

typedef struct bind_atlas {
    bind_telemetry telemetry[BIND_MAX_SOURCES];
    size_t telemetry_count;
} bind_atlas;

static void bind_usage(FILE *f) {
    fprintf(f,
            "Usage:\n"
            "  bind emit watch OBJECT ATLAS [--verify-types]\n"
            "  bind check  [--object OBJECT] FILE\n"
            "  bind probe  [--object OBJECT] FILE\n"
            "  bind json   [--object OBJECT] FILE\n"
            "  bind github [--object OBJECT] FILE\n\n"
            "Atlas watch emit:\n"
            "  telemetry imu.temperature_c f32 C \"IMU temperature\"\n"
            "  -> --watch imu.temperature_c=f32@symbol:imu_temperature_c@object:OBJECT\n\n"
            "Binding lines:\n"
            "  source <key> [type] symbol <symbol> [type <type>] [object <path>]\n"
            "  source <key> [type] addr   <address> [type <type>] [object <path>]\n");
}

static void bind_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;

    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char *bind_ltrim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void bind_rtrim(char *s) {
    size_t n;

    if (!s) return;
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static void bind_strip_comment(char *s) {
    size_t i;

    if (!s) return;
    for (i = 0; s[i]; ++i) {
        if (s[i] == '#') {
            s[i] = '\0';
            return;
        }
    }
}

static char *bind_next_token(char **cursor) {
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

static int bind_streq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static int bind_valid_type(const char *s) {
    static const char *types[] = {
        "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "f32", "f64", NULL
    };
    size_t i;

    if (!s || !*s || strlen(s) >= BIND_MAX_TYPE) return 0;
    for (i = 0; types[i]; ++i) {
        if (strcmp(s, types[i]) == 0) return 1;
    }
    return 0;
}

static unsigned long bind_probe_type_size(const char *s) {
    if (!s) return 0;
    if (bind_streq(s, "bool") || bind_streq(s, "i8") ||
        bind_streq(s, "u8")) {
        return 1;
    }
    if (bind_streq(s, "i16") || bind_streq(s, "u16")) return 2;
    if (bind_streq(s, "i32") || bind_streq(s, "u32") ||
        bind_streq(s, "f32")) {
        return 4;
    }
    if (bind_streq(s, "i64") || bind_streq(s, "u64") ||
        bind_streq(s, "f64")) {
        return 8;
    }
    return 0;
}

static int bind_valid_symbol(const char *s) {
    size_t i;

    if (!s || !*s || strlen(s) >= BIND_MAX_SYMBOL) return 0;
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == ':' || c == '.' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int bind_valid_addr(const char *s) {
    char *end = NULL;

    if (!s || !*s || strlen(s) >= BIND_MAX_ADDR) return 0;
    errno = 0;
    (void)strtoull(s, &end, 0);
    return errno == 0 && end && *end == '\0';
}

static int bind_set_type(bind_source *src, const char *type) {
    if (!src || !bind_valid_type(type) || src->type[0]) return -1;
    bind_copy(src->type, type, sizeof(src->type));
    return 0;
}

static int bind_set_object(bind_source *src, const char *object) {
    if (!src || !object || !*object || strlen(object) >= sizeof(src->object)) {
        return -1;
    }
    bind_copy(src->object, object, sizeof(src->object));
    return 0;
}

static int bind_parse_source(char **tok,
                             int ntok,
                             unsigned long line_no,
                             bind_source *out,
                             char *err,
                             size_t err_cap) {
    int i;

    if (ntok < 4 || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->line_no = line_no;

    if (!apsis_valid_name(tok[1]) || strlen(tok[1]) >= sizeof(out->key)) {
        snprintf(err, err_cap, "line %lu: invalid source key", line_no);
        return -1;
    }
    bind_copy(out->key, tok[1], sizeof(out->key));

    for (i = 2; i < ntok; ++i) {
        if (bind_valid_type(tok[i])) {
            if (bind_set_type(out, tok[i]) != 0) {
                snprintf(err, err_cap, "line %lu: duplicate or invalid type", line_no);
                return -1;
            }
        } else if (bind_streq(tok[i], "type")) {
            if (++i >= ntok || bind_set_type(out, tok[i]) != 0) {
                snprintf(err, err_cap, "line %lu: invalid type", line_no);
                return -1;
            }
        } else if (bind_streq(tok[i], "symbol")) {
            if (++i >= ntok || out->kind != BIND_KIND_NONE ||
                !bind_valid_symbol(tok[i])) {
                snprintf(err, err_cap, "line %lu: invalid symbol binding", line_no);
                return -1;
            }
            out->kind = BIND_KIND_SYMBOL;
            bind_copy(out->symbol, tok[i], sizeof(out->symbol));
        } else if (bind_streq(tok[i], "addr")) {
            if (++i >= ntok || out->kind != BIND_KIND_NONE ||
                !bind_valid_addr(tok[i])) {
                snprintf(err, err_cap, "line %lu: invalid address binding", line_no);
                return -1;
            }
            out->kind = BIND_KIND_ADDR;
            bind_copy(out->addr, tok[i], sizeof(out->addr));
        } else if (bind_streq(tok[i], "object")) {
            if (++i >= ntok || bind_set_object(out, tok[i]) != 0) {
                snprintf(err, err_cap, "line %lu: invalid object path", line_no);
                return -1;
            }
        } else {
            snprintf(err, err_cap, "line %lu: unknown token '%s'", line_no, tok[i]);
            return -1;
        }
    }

    if (out->kind == BIND_KIND_NONE) {
        snprintf(err, err_cap, "line %lu: missing symbol or addr binding", line_no);
        return -1;
    }
    return 0;
}

static int bind_parse_line(bind_model *model,
                           const char *src,
                           unsigned long line_no,
                           char *err,
                           size_t err_cap) {
    char line[BIND_LINE_MAX];
    char *cursor;
    char *text;
    char *tok[BIND_MAX_TOKENS];
    int ntok = 0;
    bind_source parsed;

    if (!model || !src) return -1;
    if (strlen(src) >= sizeof(line)) {
        snprintf(err, err_cap, "line %lu: line too long", line_no);
        return -1;
    }

    bind_copy(line, src, sizeof(line));
    bind_strip_comment(line);
    text = bind_ltrim(line);
    bind_rtrim(text);
    if (!*text) return 0;

    cursor = text;
    while (ntok < BIND_MAX_TOKENS) {
        char *next = bind_next_token(&cursor);
        if (!next) break;
        tok[ntok++] = next;
    }
    if (bind_next_token(&cursor) != NULL) {
        snprintf(err, err_cap, "line %lu: too many tokens", line_no);
        return -1;
    }
    if (ntok == 0) return 0;

    if (!bind_streq(tok[0], "source")) {
        snprintf(err, err_cap, "line %lu: expected source", line_no);
        return -1;
    }
    if (model->source_count >= BIND_MAX_SOURCES) {
        snprintf(err, err_cap, "line %lu: too many sources", line_no);
        return -1;
    }
    if (bind_parse_source(tok, ntok, line_no, &parsed, err, err_cap) != 0) {
        return -1;
    }

    model->sources[model->source_count++] = parsed;
    return 0;
}

static int bind_load_stream(bind_model *model,
                            FILE *stream,
                            char *err,
                            size_t err_cap) {
    char line[BIND_LINE_MAX];
    unsigned long line_no = 0;

    if (!model || !stream) return -1;
    memset(model, 0, sizeof(*model));

    while (fgets(line, sizeof(line), stream)) {
        line_no++;
        if (!strchr(line, '\n') && !feof(stream)) {
            snprintf(err, err_cap, "line %lu: line too long", line_no);
            return -1;
        }
        if (bind_parse_line(model, line, line_no, err, err_cap) != 0) {
            return -1;
        }
    }
    if (ferror(stream)) {
        snprintf(err, err_cap, "error reading bindings");
        return -1;
    }
    return 0;
}

static int bind_load_file(bind_model *model,
                          const char *path,
                          char *err,
                          size_t err_cap) {
    FILE *f;
    int rc;

    if (!path || !*path) return -1;
    if (bind_streq(path, "-")) {
        return bind_load_stream(model, stdin, err, err_cap);
    }

    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_cap, "%s: %s", path, strerror(errno));
        return -1;
    }
    rc = bind_load_stream(model, f, err, err_cap);
    fclose(f);
    return rc;
}

static int bind_find_telemetry(const bind_atlas *atlas, const char *key) {
    size_t i;

    if (!atlas || !key) return -1;
    for (i = 0; i < atlas->telemetry_count; ++i) {
        if (bind_streq(atlas->telemetry[i].key, key)) return (int)i;
    }
    return -1;
}

static int bind_parse_atlas_line(bind_atlas *atlas,
                                 const char *src,
                                 unsigned long line_no,
                                 char *err,
                                 size_t err_cap) {
    char line[BIND_LINE_MAX];
    char *cursor;
    char *text;
    char *kind;
    char *key;
    char *type;
    char *unit;
    bind_telemetry *telemetry;

    if (!atlas || !src) return -1;
    if (strlen(src) >= sizeof(line)) {
        snprintf(err, err_cap, "line %lu: line too long", line_no);
        return -1;
    }

    bind_copy(line, src, sizeof(line));
    bind_strip_comment(line);
    text = bind_ltrim(line);
    bind_rtrim(text);
    if (!*text) return 0;

    cursor = text;
    kind = bind_next_token(&cursor);
    if (!kind) return 0;
    if (!bind_streq(kind, "telemetry") && !bind_streq(kind, "metric")) {
        return 0;
    }

    key = bind_next_token(&cursor);
    type = bind_next_token(&cursor);
    unit = bind_next_token(&cursor);
    if (!key || !type || !unit) {
        snprintf(err, err_cap,
                 "line %lu: expected telemetry <key> <type> <unit>",
                 line_no);
        return -1;
    }
    if (!apsis_valid_name(key) || strlen(key) >= BIND_MAX_NAME) {
        snprintf(err, err_cap, "line %lu: invalid telemetry key", line_no);
        return -1;
    }
    if (!bind_valid_type(type)) {
        snprintf(err,
                 err_cap,
                 "line %lu: telemetry type '%s' is not probe-watchable",
                 line_no,
                 type);
        return -1;
    }
    if (bind_find_telemetry(atlas, key) >= 0) {
        snprintf(err,
                 err_cap,
                 "line %lu: duplicate telemetry key '%s'",
                 line_no,
                 key);
        return -1;
    }
    if (atlas->telemetry_count >= BIND_MAX_SOURCES) {
        snprintf(err, err_cap, "line %lu: too many telemetry entries", line_no);
        return -1;
    }

    telemetry = &atlas->telemetry[atlas->telemetry_count++];
    bind_copy(telemetry->key, key, sizeof(telemetry->key));
    bind_copy(telemetry->type, type, sizeof(telemetry->type));
    telemetry->line_no = line_no;
    return 0;
}

static int bind_load_atlas_stream(bind_atlas *atlas,
                                  FILE *stream,
                                  char *err,
                                  size_t err_cap) {
    char line[BIND_LINE_MAX];
    unsigned long line_no = 0;

    if (!atlas || !stream) return -1;
    memset(atlas, 0, sizeof(*atlas));

    while (fgets(line, sizeof(line), stream)) {
        line_no++;
        if (!strchr(line, '\n') && !feof(stream)) {
            snprintf(err, err_cap, "line %lu: line too long", line_no);
            return -1;
        }
        if (bind_parse_atlas_line(atlas, line, line_no, err, err_cap) != 0) {
            return -1;
        }
    }
    if (ferror(stream)) {
        snprintf(err, err_cap, "error reading atlas");
        return -1;
    }
    if (atlas->telemetry_count == 0) {
        snprintf(err, err_cap, "no telemetry entries found");
        return -1;
    }
    return 0;
}

static int bind_load_atlas_file(bind_atlas *atlas,
                                const char *path,
                                char *err,
                                size_t err_cap) {
    FILE *f;
    int rc;

    if (!path || !*path) return -1;
    if (bind_streq(path, "-")) {
        return bind_load_atlas_stream(atlas, stdin, err, err_cap);
    }

    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_cap, "%s: %s", path, strerror(errno));
        return -1;
    }
    rc = bind_load_atlas_stream(atlas, f, err, err_cap);
    fclose(f);
    return rc;
}

static int bind_symbol_from_key(const char *key, char *out, size_t out_cap) {
    size_t i;

    if (!key || !out || out_cap == 0) return -1;
    if (strlen(key) >= out_cap) return -1;
    for (i = 0; key[i]; ++i) {
        unsigned char c = (unsigned char)key[i];

        out[i] = isalnum(c) ? (char)c : '_';
    }
    out[i] = '\0';
    return out[0] ? 0 : -1;
}

#if defined(__linux__)
typedef struct bind_symbol_info {
    int found;
    int ambiguous;
    unsigned char type;
    unsigned long size;
} bind_symbol_info;

static int bind_read_exact_at(int fd, off_t off, void *buf, size_t n) {
    unsigned char *p = (unsigned char *)buf;
    size_t done = 0;

    while (done < n) {
        ssize_t r = pread(fd, p + done, n - done, off + (off_t)done);

        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

static int bind_read_elf_string(int fd,
                                const Elf64_Shdr *strsec,
                                Elf64_Word name_offset,
                                char *out,
                                size_t cap) {
    size_t i;

    if (!strsec || !out || cap == 0) return -1;
    if ((Elf64_Xword)name_offset >= strsec->sh_size) return -1;

    for (i = 0; i + 1 < cap; ++i) {
        char c;
        Elf64_Xword pos = (Elf64_Xword)name_offset + (Elf64_Xword)i;

        if (pos >= strsec->sh_size) return -1;
        if (bind_read_exact_at(fd,
                               (off_t)(strsec->sh_offset + pos),
                               &c,
                               1) != 0) {
            return -1;
        }
        out[i] = c;
        if (c == '\0') return 0;
    }

    out[cap - 1] = '\0';
    return -1;
}

static int bind_symbol_is_watchable_type(unsigned char type) {
    return type == STT_OBJECT || type == STT_COMMON || type == STT_TLS;
}

static int bind_record_symbol_match(const Elf64_Sym *sym,
                                    bind_symbol_info *info) {
    unsigned char type;
    unsigned long size;

    if (!sym || !info) return -1;
    type = ELF64_ST_TYPE(sym->st_info);
    if (!bind_symbol_is_watchable_type(type)) return -1;
    size = (unsigned long)sym->st_size;

    if (!info->found) {
        info->found = 1;
        info->type = type;
        info->size = size;
        return 0;
    }
    if (info->type != type || info->size != size) {
        info->ambiguous = 1;
    }
    return 0;
}

static int bind_scan_elf64_symbols(int fd,
                                   const Elf64_Shdr *sections,
                                   size_t section_count,
                                   size_t sym_index,
                                   const char *symbol,
                                   bind_symbol_info *info) {
    const Elf64_Shdr *symsec = &sections[sym_index];
    const Elf64_Shdr *strsec;
    size_t count;
    size_t i;

    if (symsec->sh_link >= section_count) return -1;
    if (symsec->sh_entsize < sizeof(Elf64_Sym)) return -1;

    strsec = &sections[symsec->sh_link];
    if (strsec->sh_size == 0) return -1;

    count = (size_t)(symsec->sh_size / symsec->sh_entsize);
    for (i = 0; i < count; ++i) {
        Elf64_Sym sym;
        char name[BIND_MAX_PATH];
        off_t off = (off_t)(symsec->sh_offset +
                            (Elf64_Off)(i * symsec->sh_entsize));

        if (bind_read_exact_at(fd, off, &sym, sizeof(sym)) != 0) return -1;
        if (sym.st_name == 0 || sym.st_name >= strsec->sh_size) continue;
        if (sym.st_shndx == SHN_UNDEF) continue;
        if (bind_read_elf_string(fd, strsec, sym.st_name,
                                 name, sizeof(name)) != 0) {
            return -1;
        }
        if (bind_streq(name, symbol)) {
            if (bind_record_symbol_match(&sym, info) != 0) return -1;
        }
    }

    return 0;
}

static int bind_read_symbol_info(const char *object,
                                 const char *symbol,
                                 bind_symbol_info *info) {
    int fd;
    Elf64_Ehdr ehdr;
    Elf64_Shdr sections[BIND_MAX_ELF_SECTIONS];
    size_t i;

    if (!object || !symbol || !info) return -1;
    memset(info, 0, sizeof(*info));

    fd = open(object, O_RDONLY);
    if (fd < 0) return -1;

    if (bind_read_exact_at(fd, 0, &ehdr, sizeof(ehdr)) != 0) {
        close(fd);
        return -1;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr.e_shnum == 0 ||
        ehdr.e_shnum > BIND_MAX_ELF_SECTIONS) {
        close(fd);
        return -1;
    }

    if (bind_read_exact_at(fd,
                           (off_t)ehdr.e_shoff,
                           sections,
                           (size_t)ehdr.e_shnum * sizeof(*sections)) != 0) {
        close(fd);
        return -1;
    }

    for (i = 0; i < (size_t)ehdr.e_shnum; ++i) {
        if (sections[i].sh_type != SHT_SYMTAB &&
            sections[i].sh_type != SHT_DYNSYM) {
            continue;
        }
        if (bind_scan_elf64_symbols(fd,
                                    sections,
                                    (size_t)ehdr.e_shnum,
                                    i,
                                    symbol,
                                    info) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}
#endif

static int bind_verify_symbol(const char *object,
                              const char *symbol,
                              const char *type,
                              char *err,
                              size_t err_cap) {
    unsigned long expected;

    expected = bind_probe_type_size(type);
    if (expected == 0) {
        snprintf(err, err_cap, "unsupported probe type '%s'", type);
        return -1;
    }
#if defined(__linux__)
    bind_symbol_info info;

    if (bind_read_symbol_info(object, symbol, &info) != 0) {
        snprintf(err,
                 err_cap,
                 "could not read watchable symbol '%s' from '%s'",
                 symbol,
                 object);
        return -1;
    }
    if (!info.found) {
        snprintf(err, err_cap, "symbol '%s' not found in '%s'", symbol, object);
        return -1;
    }
    if (info.ambiguous) {
        snprintf(err,
                 err_cap,
                 "symbol '%s' has conflicting ELF symbol metadata",
                 symbol);
        return -1;
    }
    if (info.size != 0 && info.size != expected) {
        snprintf(err,
                 err_cap,
                 "symbol '%s' has size %lu, expected %lu for %s",
                 symbol,
                 info.size,
                 expected,
                 type);
        return -1;
    }
    return 0;
#else
    (void)object;
    (void)symbol;
    (void)type;
    snprintf(err, err_cap, "--verify-types requires Linux ELF support");
    return -1;
#endif
}

static int bind_parse_emit_watch_cli(int argc,
                                     char **argv,
                                     const char **object,
                                     const char **atlas_path,
                                     int *verify_types) {
    int i;

    if (!object || !atlas_path || !verify_types) return -1;
    *object = NULL;
    *atlas_path = NULL;
    *verify_types = 0;

    if (argc >= 4 && (bind_streq(argv[3], "-h") ||
                      bind_streq(argv[3], "--help"))) {
        bind_usage(stdout);
        exit(BIND_EXIT_OK);
    }

    for (i = 3; i < argc; ++i) {
        if (bind_streq(argv[i], "--verify-types")) {
            *verify_types = 1;
        } else if (!*object) {
            *object = argv[i];
        } else if (!*atlas_path) {
            *atlas_path = argv[i];
        } else {
            return -1;
        }
    }

    return *object && *atlas_path ? 0 : -1;
}

static int bind_emit_watch_main(int argc, char **argv) {
    bind_atlas atlas;
    const char *object;
    const char *atlas_path;
    int verify_types;
    char err[256];
    size_t i;

    if (bind_parse_emit_watch_cli(argc,
                                  argv,
                                  &object,
                                  &atlas_path,
                                  &verify_types) != 0) {
        bind_usage(stderr);
        return BIND_EXIT_USAGE;
    }
    if (!object[0]) {
        fprintf(stderr, "bind: empty object path\n");
        return BIND_EXIT_USAGE;
    }

    err[0] = '\0';
    if (bind_load_atlas_file(&atlas, atlas_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "bind: %s\n", err[0] ? err : "invalid atlas");
        return BIND_EXIT_FAIL;
    }

    for (i = 0; i < atlas.telemetry_count; ++i) {
        const bind_telemetry *telemetry = &atlas.telemetry[i];
        char symbol[BIND_MAX_SYMBOL];

        if (bind_symbol_from_key(telemetry->key,
                                 symbol,
                                 sizeof(symbol)) != 0) {
            fprintf(stderr,
                    "bind: line %lu: telemetry key does not map to a symbol\n",
                    telemetry->line_no);
            return BIND_EXIT_FAIL;
        }
        if (verify_types &&
            bind_verify_symbol(object,
                               symbol,
                               telemetry->type,
                               err,
                               sizeof(err)) != 0) {
            fprintf(stderr,
                    "bind: line %lu: %s\n",
                    telemetry->line_no,
                    err[0] ? err : "symbol verification failed");
            return BIND_EXIT_FAIL;
        }
        printf("--watch %s=%s@symbol:%s@object:%s\n",
               telemetry->key,
               telemetry->type,
               symbol,
               object);
    }

    return BIND_EXIT_OK;
}

static const char *bind_source_object(const bind_source *src,
                                      const char *default_object) {
    if (src && src->object[0]) return src->object;
    return default_object && *default_object ? default_object : "";
}

static int bind_emit_probe(const bind_model *model, const char *default_object) {
    size_t i;

    for (i = 0; i < model->source_count; ++i) {
        const bind_source *src = &model->sources[i];
        const char *object = bind_source_object(src, default_object);

        if (!src->type[0]) {
            fprintf(stderr, "bind: line %lu: probe output requires a type\n",
                    src->line_no);
            return BIND_EXIT_FAIL;
        }

        if (src->kind == BIND_KIND_ADDR) {
            printf("-w %s:%s:%s\n", src->key, src->type, src->addr);
        } else if (object[0]) {
            printf("-s %s:%s:%s@%s\n", src->key, src->type, src->symbol, object);
        } else {
            printf("-s %s:%s:%s\n", src->key, src->type, src->symbol);
        }
    }
    return BIND_EXIT_OK;
}

static void bind_json_string(FILE *out, const char *s) {
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

static void bind_emit_json(const bind_model *model, const char *default_object) {
    size_t i;

    puts("[");
    for (i = 0; i < model->source_count; ++i) {
        const bind_source *src = &model->sources[i];
        const char *object = bind_source_object(src, default_object);

        fputs("  {\"source\":", stdout);
        bind_json_string(stdout, src->key);
        fputs(",\"type\":", stdout);
        if (src->type[0]) bind_json_string(stdout, src->type);
        else fputs("null", stdout);
        fputs(",\"binding\":", stdout);
        bind_json_string(stdout, src->kind == BIND_KIND_ADDR ? "addr" : "symbol");
        if (src->kind == BIND_KIND_ADDR) {
            fputs(",\"addr\":", stdout);
            bind_json_string(stdout, src->addr);
        } else {
            fputs(",\"symbol\":", stdout);
            bind_json_string(stdout, src->symbol);
            fputs(",\"object\":", stdout);
            if (object[0]) bind_json_string(stdout, object);
            else fputs("null", stdout);
        }
        printf(",\"line\":%lu}", src->line_no);
        puts(i + 1 < model->source_count ? "," : "");
    }
    puts("]");
}

static void bind_emit_github(const bind_model *model, const char *default_object) {
    size_t i;

    puts("| Source | Type | Binding | Target | Object |");
    puts("| --- | --- | --- | --- | --- |");
    for (i = 0; i < model->source_count; ++i) {
        const bind_source *src = &model->sources[i];
        const char *object = bind_source_object(src, default_object);
        const char *kind = src->kind == BIND_KIND_ADDR ? "addr" : "symbol";
        const char *target = src->kind == BIND_KIND_ADDR ? src->addr : src->symbol;

        printf("| `%s` | `%s` | `%s` | `%s` | `%s` |\n",
               src->key,
               src->type[0] ? src->type : "?",
               kind,
               target,
               object[0] ? object : "-");
    }
}

static int bind_emit_check(const bind_model *model) {
    size_t i;
    size_t symbols = 0;
    size_t addrs = 0;
    size_t missing_types = 0;

    for (i = 0; i < model->source_count; ++i) {
        if (model->sources[i].kind == BIND_KIND_SYMBOL) symbols++;
        if (model->sources[i].kind == BIND_KIND_ADDR) addrs++;
        if (!model->sources[i].type[0]) missing_types++;
    }

    printf("sources=%lu symbols=%lu addrs=%lu missing_types=%lu\n",
           (unsigned long)model->source_count,
           (unsigned long)symbols,
           (unsigned long)addrs,
           (unsigned long)missing_types);
    return BIND_EXIT_OK;
}

static int bind_parse_cli(int argc,
                          char **argv,
                          const char **cmd,
                          const char **path,
                          const char **object) {
    int i;

    if (argc < 3 || !cmd || !path || !object) return -1;
    *cmd = argv[1];
    *path = NULL;
    *object = NULL;

    for (i = 2; i < argc; ++i) {
        if (bind_streq(argv[i], "--object")) {
            if (++i >= argc || !argv[i][0]) return -1;
            *object = argv[i];
        } else if (!*path) {
            *path = argv[i];
        } else {
            return -1;
        }
    }

    return *path ? 0 : -1;
}

int main(int argc, char **argv) {
    bind_model model;
    const char *cmd;
    const char *path;
    const char *object;
    char err[256];

    if (argc >= 2 && (bind_streq(argv[1], "-h") || bind_streq(argv[1], "--help"))) {
        bind_usage(stdout);
        return BIND_EXIT_OK;
    }

    if (argc >= 3 && bind_streq(argv[1], "emit") && bind_streq(argv[2], "watch")) {
        return bind_emit_watch_main(argc, argv);
    }

    if (bind_parse_cli(argc, argv, &cmd, &path, &object) != 0) {
        bind_usage(stderr);
        return BIND_EXIT_USAGE;
    }

    err[0] = '\0';
    if (bind_load_file(&model, path, err, sizeof(err)) != 0) {
        fprintf(stderr, "bind: %s\n", err[0] ? err : "invalid bindings");
        return BIND_EXIT_FAIL;
    }

    if (bind_streq(cmd, "check")) return bind_emit_check(&model);
    if (bind_streq(cmd, "probe")) return bind_emit_probe(&model, object);
    if (bind_streq(cmd, "json")) {
        bind_emit_json(&model, object);
        return BIND_EXIT_OK;
    }
    if (bind_streq(cmd, "github")) {
        bind_emit_github(&model, object);
        return BIND_EXIT_OK;
    }

    bind_usage(stderr);
    return BIND_EXIT_USAGE;
}
