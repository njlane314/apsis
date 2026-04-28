#if defined(__linux__)
#define _GNU_SOURCE
#include "apsis.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PROBE_MAX_WATCHES
#define PROBE_MAX_WATCHES 64
#endif
#ifndef PROBE_MAX_NAME
#define PROBE_MAX_NAME 128
#endif
#ifndef PROBE_MAX_PATH
#define PROBE_MAX_PATH 4096
#endif
#ifndef PROBE_MAX_ADDR
#define PROBE_MAX_ADDR 64
#endif
#ifndef PROBE_MAX_ELF_SECTIONS
#define PROBE_MAX_ELF_SECTIONS 4096
#endif

typedef enum probe_type { P_I8, P_U8, P_I16, P_U16, P_I32, P_U32, P_I64, P_U64, P_F32, P_F64, P_BOOL } probe_type;
typedef enum probe_output { OUT_KEYVALUE, OUT_JSONL, OUT_LIMLOG } probe_output;
typedef enum probe_emit { EMIT_AUTO, EMIT_SAMPLES, EMIT_EVENTS, EMIT_BOTH } probe_emit;
typedef enum probe_child_mode { CHILD_TERMINATE, CHILD_WAIT, CHILD_LEAVE } probe_child_mode;

typedef struct watch {
    char name[PROBE_MAX_NAME];
    probe_type type;
    uintptr_t addr;
    char symbol[PROBE_MAX_NAME];
    char object[PROBE_MAX_PATH];
    int from_symbol;
    int field_path;
} watch;

typedef struct options {
    pid_t pid;
    int have_pid;
    int interval_ms;
    int count;
    probe_output output;
    probe_emit emit;
    probe_child_mode child_mode;
    int verbose;
    int delay_ms;
    int fail_never;
    apsis_level fail_on;
    const char *rules_path;
    int ebpf;
    char default_object[PROBE_MAX_PATH];
    apsis_ctx rules;
    watch watches[PROBE_MAX_WATCHES];
    size_t watch_count;
    char **run_argv;
} options;

typedef struct symbol_options {
    char object[PROBE_MAX_PATH];
    const char *filter;
    int demangle;
    int show_types;
} symbol_options;

typedef struct plan_watch {
    watch resolved;
    char addr_text[PROBE_MAX_ADDR];
} plan_watch;

typedef struct plan_options {
    plan_watch watches[PROBE_MAX_WATCHES];
    size_t watch_count;
    char **run_argv;
} plan_options;

static void usage(FILE *f) {
    fprintf(f,
        "probe: read typed values from a running Linux process\n\n"
        "Usage:\n"
        "  probe symbols OBJECT [--filter TEXT] [--demangle] [--types]\n"
        "  probe plan --watch KEY=TYPE@symbol:SYMBOL [--watch ...] -- PROGRAM [args...]\n"
        "  probe run --symbol KEY=TYPE:SYMBOL [--symbol ...] -- PROGRAM [args...]\n"
        "  probe attach --pid PID --symbol KEY=TYPE:SYMBOL [--symbol ...]\n"
        "  probe -p PID -w name:type:0xADDR [-i MS] [-n COUNT]\n"
        "  probe -p PID -s name:type:symbol [-o OBJECT] [-i MS] [-n COUNT]\n"
        "  probe -s name:type:symbol [-o OBJECT] [-i MS] [-n COUNT] -- ./program [args...]\n\n"
        "Types: i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 bool\n\n"
        "Examples:\n"
        "  probe -p 1234 -w temp:f32:0x7ffd01234000 -n 1\n"
        "  probe -p 1234 -s temp:f32:imu_temperature_c -n 10 -i 100\n"
        "  probe run --symbol temp=f32:imu_temperature_c -- ./drone_sim\n"
        "  probe attach --pid 1234 --symbol temp=f32:imu_temperature_c\n"
        "  probe -s temp:f32:imu_temperature_c -n 10 -i 100 -- ./drone_sim\n"
        "  probe symbols ./drone_sim\n"
        "  probe symbols ./drone_sim --filter temp\n"
        "  probe symbols ./drone_sim --demangle\n"
        "  probe symbols ./drone_sim --types\n\n"
        "  probe plan --watch temp=f32@symbol:imu_temperature_c -- ./drone_sim\n\n"
        "Options:\n"
        "      symbols               list defined data symbols from an ELF object\n"
        "      plan                  print a runnable probe command from watch specs\n"
        "      --watch SPEC          plan watch, KEY=TYPE@symbol:NAME or KEY=TYPE@addr:ADDR\n"
        "      run                   launch PROGRAM and sample planned watches\n"
        "      attach                sample an existing --pid process\n"
        "      --symbol SPEC         watch symbol, KEY=TYPE:SYMBOL\n"
        "      --addr SPEC           watch address, KEY=TYPE:ADDRESS\n"
        "      --filter TEXT         show only symbols containing TEXT\n"
        "      --demangle            print supported C++ symbols in demangled form\n"
        "      --types               include ELF type, binding, size, and probe hint\n"
        "  -r, --rules PATH          evaluate samples with libapsis rules\n"
        "      --emit MODE           samples, events, or both; default: events with rules,\n"
        "                            samples without rules\n"
        "      --fail-on LEVEL       error, warn, info, or never; default: error\n"
        "      --format FORMAT       keyvalue, jsonl, or limlog; default: keyvalue\n"
        "      --json                alias for --format jsonl\n"
        "      --terminate-after-sampling\n"
        "                            terminate a launched target after sampling; default\n"
        "      --wait-child          wait for a launched target to exit naturally\n"
        "      --leave-running       leave a launched target running after sampling\n"
        "      --ebpf                reserved for future eBPF/uprobe mode\n\n"
        "Planned:\n"
        "  DWARF field paths such as drone_state.imu.temperature_c:f32 are reserved\n"
        "  but not implemented yet.\n\n"
        "probe does not bypass OS process access controls. It is intended for\n"
        "processes you own in lab, debug, simulation, and CI settings.\n\n"
        "Output formats: keyvalue, jsonl, limlog. Use --rules to evaluate with libapsis.\n");
}

static int parse_type(const char *s, probe_type *out) {
    if (!strcmp(s,"i8")) *out=P_I8; else if(!strcmp(s,"u8")) *out=P_U8;
    else if(!strcmp(s,"i16")) *out=P_I16; else if(!strcmp(s,"u16")) *out=P_U16;
    else if(!strcmp(s,"i32")) *out=P_I32; else if(!strcmp(s,"u32")) *out=P_U32;
    else if(!strcmp(s,"i64")) *out=P_I64; else if(!strcmp(s,"u64")) *out=P_U64;
    else if(!strcmp(s,"f32")) *out=P_F32; else if(!strcmp(s,"f64")) *out=P_F64;
    else if(!strcmp(s,"bool")) *out=P_BOOL; else return -1;
    return 0;
}

static const char *type_name(probe_type t) {
    switch(t) {
        case P_I8:return"i8"; case P_U8:return"u8"; case P_I16:return"i16"; case P_U16:return"u16";
        case P_I32:return"i32"; case P_U32:return"u32"; case P_I64:return"i64"; case P_U64:return"u64";
        case P_F32:return"f32"; case P_F64:return"f64"; case P_BOOL:return"bool";
    }
    return "?";
}

static size_t type_size(probe_type t) {
    switch(t) {
        case P_I8:case P_U8:case P_BOOL:return 1;
        case P_I16:case P_U16:return 2;
        case P_I32:case P_U32:case P_F32:return 4;
        case P_I64:case P_U64:case P_F64:return 8;
    }
    return 0;
}

static int split_watch_spec(char *tmp, char **a, char **b, char **c) {
    char *first;
    char *second;

    if (!tmp || !a || !b || !c) return -1;

    first = strchr(tmp, ':');
    if (!first) return -1;
    *first = '\0';
    second = strchr(first + 1, ':');
    if (second) {
        *second = '\0';
        *c = second + 1;
    } else {
        *c = NULL;
    }

    *a = tmp;
    *b = first + 1;
    return 0;
}

static int parse_watch_binding_spec(char *tmp, int symbol_mode, watch *w) {
    char *eq;
    char *sep;
    char *name;
    char *type;
    char *target;

    if (!tmp || !w) return -1;

    eq = strchr(tmp, '=');
    if (!eq) return -1;
    *eq = '\0';
    name = tmp;
    type = eq + 1;
    sep = strchr(type, ':');
    if (!sep) return -1;
    *sep = '\0';
    target = sep + 1;

    if (!apsis_valid_name(name) || !*type || !*target ||
        strlen(name) >= sizeof(w->name)) {
        return -1;
    }
    memset(w, 0, sizeof(*w));
    strcpy(w->name, name);
    if (parse_type(type, &w->type) != 0) return -1;

    if (symbol_mode) {
        char *at = strrchr(target, '@');
        if (at) {
            *at = '\0';
            at++;
            if (!*at || strlen(at) >= sizeof(w->object)) return -1;
            strcpy(w->object, at);
        }
        if (!*target || strlen(target) >= sizeof(w->symbol)) return -1;
        strcpy(w->symbol, target);
        w->from_symbol = 1;
    } else {
        char *end = NULL;
        unsigned long long v;

        errno = 0;
        v = strtoull(target, &end, 0);
        if (errno || !end || *end) return -1;
        w->addr = (uintptr_t)v;
    }
    return 0;
}

static int parse_watch_spec(const char *spec, int symbol_mode, watch *w) {
    char tmp[PROBE_MAX_PATH + PROBE_MAX_NAME * 2];
    char *a;
    char *b;
    char *c;
    char *eq;
    char *colon;

    if (strlen(spec) >= sizeof(tmp)) return -1;
    strcpy(tmp, spec);

    eq = strchr(tmp, '=');
    colon = strchr(tmp, ':');
    if (eq && (!colon || eq < colon)) {
        return parse_watch_binding_spec(tmp, symbol_mode, w);
    }

    if (split_watch_spec(tmp, &a, &b, &c) != 0) return -1;
    if (!a || !b || !*a || !*b || strlen(a) >= sizeof(w->name)) return -1;
    memset(w, 0, sizeof(*w));
    strcpy(w->name, a);
    if (parse_type(b, &w->type) != 0) return -1;
    if (symbol_mode) {
        if (c && *c) {
            char *at = strrchr(c, '@');
            if (at) {
                *at = '\0';
                at++;
                if (!*at || strlen(at) >= sizeof(w->object)) return -1;
                strcpy(w->object, at);
            }
            if (!*c) return -1;
            if (strlen(c) >= sizeof(w->symbol)) return -1;
            strcpy(w->symbol, c);
        } else {
            char *dot;
            if (!strchr(a, '.')) return -1;
            if (strlen(a) >= sizeof(w->symbol)) return -1;
            strcpy(w->symbol, a);
            dot = strchr(w->symbol, '.');
            if (dot) *dot = '\0';
            w->field_path = 1;
        }
        w->from_symbol = 1;
    } else {
        if (!c || !*c) return -1;
        char *end = NULL;
        errno = 0;
        unsigned long long v = strtoull(c, &end, 0);
        if (errno || !end || *end) return -1;
        w->addr = (uintptr_t)v;
    }
    return 0;
}

static int parse_plan_watch_spec(const char *spec, plan_watch *out) {
    char tmp[PROBE_MAX_PATH + PROBE_MAX_NAME * 2];
    char *eq;
    char *at;
    char *source;
    char *object;

    if (!spec || !out || strlen(spec) >= sizeof(tmp)) return -1;
    memset(out, 0, sizeof(*out));
    strcpy(tmp, spec);

    eq = strchr(tmp, '=');
    if (!eq) return -1;
    *eq = '\0';
    if (!apsis_valid_name(tmp) || strlen(tmp) >= sizeof(out->resolved.name)) {
        return -1;
    }
    strcpy(out->resolved.name, tmp);

    at = strchr(eq + 1, '@');
    if (!at) return -1;
    *at = '\0';
    if (parse_type(eq + 1, &out->resolved.type) != 0) return -1;

    source = at + 1;
    if (!strncmp(source, "symbol:", 7)) {
        char *symbol = source + 7;

        object = strstr(symbol, "@object:");
        if (object) {
            *object = '\0';
            object += 8;
            if (!*object || strlen(object) >= sizeof(out->resolved.object)) {
                return -1;
            }
            strcpy(out->resolved.object, object);
        }
        if (!*symbol || strlen(symbol) >= sizeof(out->resolved.symbol)) {
            return -1;
        }
        strcpy(out->resolved.symbol, symbol);
        out->resolved.from_symbol = 1;
        return 0;
    }

    if (!strncmp(source, "addr:", 5)) {
        char *addr = source + 5;
        char *end = NULL;
        unsigned long long value;

        if (!*addr || strlen(addr) >= sizeof(out->addr_text)) return -1;
        errno = 0;
        value = strtoull(addr, &end, 0);
        if (errno || !end || *end) return -1;
        out->resolved.addr = (uintptr_t)value;
        strcpy(out->addr_text, addr);
        return 0;
    }

    return -1;
}

static int realpath_or_copy(const char *path, char *out, size_t cap) {
    char tmp[PROBE_MAX_PATH];
    if (!path || !*path || cap == 0) return -1;
    if (realpath(path, tmp)) {
        if (strlen(tmp) >= cap) return -1;
        strcpy(out, tmp);
        return 0;
    }
    if (strlen(path) >= cap) return -1;
    strcpy(out, path);
    return 0;
}

static int probe_join_path(const char *dir,
                           const char *name,
                           char *out,
                           size_t cap) {
    size_t dir_len;
    size_t name_len;
    int need_slash;

    if (!dir || !name || !out || cap == 0) return -1;

    if (*dir == '\0') dir = ".";
    dir_len = strlen(dir);
    name_len = strlen(name);
    need_slash = dir_len > 0 && dir[dir_len - 1] != '/';

    if (dir_len + (need_slash ? 1u : 0u) + name_len >= cap) return -1;

    strcpy(out, dir);
    if (need_slash) strcat(out, "/");
    strcat(out, name);
    return 0;
}

static int resolve_exec_object(const char *cmd, char *out, size_t cap) {
    const char *path_env;
    const char *start;

    if (!cmd || !*cmd || !out || cap == 0) return -1;
    if (cap < PROBE_MAX_PATH) return -1;

    if (strchr(cmd, '/')) {
        return realpath(cmd, out) ? 0 : -1;
    }

    path_env = getenv("PATH");
    if (!path_env || !*path_env) path_env = "/bin:/usr/bin";

    start = path_env;
    while (1) {
        const char *end = strchr(start, ':');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        char dir[PROBE_MAX_PATH];
        char candidate[PROBE_MAX_PATH];

        if (len >= sizeof(dir)) return -1;
        memcpy(dir, start, len);
        dir[len] = '\0';

        if (probe_join_path(dir, cmd, candidate, sizeof(candidate)) == 0 &&
            access(candidate, X_OK) == 0 &&
            realpath(candidate, out)) {
            return 0;
        }

        if (!end) break;
        start = end + 1;
    }

    return -1;
}

static int read_exe_path(pid_t pid, char *out, size_t cap) {
    char linkpath[64];
    snprintf(linkpath, sizeof(linkpath), "/proc/%ld/exe", (long)pid);
    ssize_t n = readlink(linkpath, out, cap - 1);
    if (n < 0) return -1;
    out[n] = '\0';
    return 0;
}

static int elf_type(const char *path, int *out_type) {
    unsigned char hdr[18];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, hdr, sizeof(hdr));
    close(fd);
    if (n < (ssize_t)sizeof(hdr)) return -1;
    if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') return -1;
    if (hdr[5] != 1) return -1; /* little endian */
    *out_type = (int)(hdr[16] | (hdr[17] << 8));
    return 0;
}

static int read_exact_at(int fd, off_t off, void *buf, size_t n) {
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

static int append_text(char *out, size_t cap, size_t *used, const char *s, size_t n) {
    if (!out || !used || *used >= cap) return -1;
    while (n > 0) {
        if (*used + 1 >= cap) return -1;
        out[*used] = *s;
        (*used)++;
        s++;
        n--;
    }
    out[*used] = '\0';
    return 0;
}

static int parse_itanium_length(const char **p, size_t *len) {
    size_t value = 0;
    int digits = 0;

    while (**p >= '0' && **p <= '9') {
        value = value * 10u + (size_t)(**p - '0');
        (*p)++;
        digits = 1;
    }

    if (!digits || value == 0) return -1;
    *len = value;
    return 0;
}

static int demangle_itanium_name(const char *raw, char *out, size_t cap) {
    const char *p;
    size_t used = 0;
    int nested = 0;
    int parts = 0;

    if (!raw || !out || cap == 0) return -1;
    out[0] = '\0';
    if (strncmp(raw, "_Z", 2) != 0) return -1;

    p = raw + 2;
    if (*p == 'L') p++;
    if (*p == 'N') {
        nested = 1;
        p++;
    }

    while (*p && (!nested || *p != 'E')) {
        size_t len;

        if (*p == 'K' || *p == 'V') {
            p++;
            continue;
        }

        if (parse_itanium_length(&p, &len) != 0) break;
        if (strlen(p) < len) return -1;

        if (parts > 0 && append_text(out, cap, &used, "::", 2) != 0) return -1;
        if (append_text(out, cap, &used, p, len) != 0) return -1;
        p += len;
        parts++;
        if (!nested) break;
    }

    if (nested && *p != 'E') return -1;
    return parts > 0 ? 0 : -1;
}

static int symbol_name_matches(const char *raw, const char *want) {
    char demangled[PROBE_MAX_PATH];
    const char *leaf;

    if (!raw || !want) return 0;
    if (strcmp(raw, want) == 0) return 1;

    if (demangle_itanium_name(raw, demangled, sizeof(demangled)) != 0) {
        return 0;
    }
    if (strcmp(demangled, want) == 0) return 1;

    leaf = strrchr(demangled, ':');
    if (leaf && leaf > demangled && leaf[-1] == ':') {
        leaf++;
        if (strcmp(leaf, want) == 0) return 1;
    }

    return 0;
}

static int read_elf_string(int fd,
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
        if (read_exact_at(fd, (off_t)(strsec->sh_offset + pos), &c, 1) != 0) {
            return -1;
        }
        out[i] = c;
        if (c == '\0') return 0;
    }

    out[cap - 1] = '\0';
    return -1;
}

static int scan_elf64_symbols(int fd,
                              const Elf64_Shdr *sections,
                              size_t section_count,
                              size_t sym_index,
                              const char *symbol,
                              uintptr_t *out,
                              int *matches) {
    const Elf64_Shdr *symsec = &sections[sym_index];
    const Elf64_Shdr *strsec;
    size_t count;
    size_t i;

    if (symsec->sh_link >= section_count) return -1;
    if (symsec->sh_entsize == 0) return -1;

    strsec = &sections[symsec->sh_link];
    if (strsec->sh_size == 0) return -1;

    count = (size_t)(symsec->sh_size / symsec->sh_entsize);
    for (i = 0; i < count; ++i) {
        Elf64_Sym sym;
        unsigned char type;
        char name[PROBE_MAX_PATH];
        off_t off = (off_t)(symsec->sh_offset + (Elf64_Off)(i * symsec->sh_entsize));

        if (read_exact_at(fd, off, &sym, sizeof(sym)) != 0) {
            return -1;
        }
        if (sym.st_name == 0 || sym.st_name >= strsec->sh_size) continue;
        if (sym.st_shndx == SHN_UNDEF || sym.st_value == 0) continue;

        type = ELF64_ST_TYPE(sym.st_info);
        if (type == STT_FILE || type == STT_SECTION) continue;

        if (read_elf_string(fd, strsec, sym.st_name, name, sizeof(name)) != 0) {
            return -1;
        }
        if (symbol_name_matches(name, symbol)) {
            *out = (uintptr_t)sym.st_value;
            (*matches)++;
            if (*matches > 1) {
                return 1;
            }
        }
    }

    return 0;
}

static int resolve_symbol_elf(const char *object, const char *symbol, uintptr_t *out) {
    int fd;
    Elf64_Ehdr ehdr;
    Elf64_Shdr sections[PROBE_MAX_ELF_SECTIONS];
    int matches = 0;
    size_t i;

    if (!object || !symbol || !out) return -1;

    fd = open(object, O_RDONLY);
    if (fd < 0) return -1;

    if (read_exact_at(fd, 0, &ehdr, sizeof(ehdr)) != 0) {
        close(fd);
        return -1;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr.e_shnum == 0) {
        close(fd);
        return -1;
    }
    if (ehdr.e_shnum > PROBE_MAX_ELF_SECTIONS) {
        close(fd);
        return -1;
    }

    if (read_exact_at(fd, (off_t)ehdr.e_shoff, sections,
                      (size_t)ehdr.e_shnum * sizeof(*sections)) != 0) {
        close(fd);
        return -1;
    }

    for (i = 0; i < (size_t)ehdr.e_shnum; ++i) {
        int rc;

        if (sections[i].sh_type != SHT_SYMTAB && sections[i].sh_type != SHT_DYNSYM) {
            continue;
        }

        rc = scan_elf64_symbols(fd, sections, (size_t)ehdr.e_shnum,
                                i, symbol, out, &matches);
        if (rc != 0 || matches > 1) {
            close(fd);
            return matches > 1 ? -2 : -1;
        }
    }

    close(fd);
    return matches == 1 ? 0 : -1;
}

static const char *symbol_elf_type_name(unsigned char type) {
    switch (type) {
        case STT_NOTYPE: return "notype";
        case STT_OBJECT: return "object";
        case STT_FUNC: return "func";
        case STT_SECTION: return "section";
        case STT_FILE: return "file";
        case STT_COMMON: return "common";
        case STT_TLS: return "tls";
    }
    return "unknown";
}

static const char *symbol_elf_bind_name(unsigned char bind) {
    switch (bind) {
        case STB_LOCAL: return "local";
        case STB_GLOBAL: return "global";
        case STB_WEAK: return "weak";
    }
    return "unknown";
}

static int symbol_is_watchable_type(unsigned char type) {
    return type == STT_OBJECT || type == STT_COMMON || type == STT_TLS;
}

static const char *symbol_probe_type_hint(unsigned char type, Elf64_Xword size) {
    if (!symbol_is_watchable_type(type)) return "-";
    switch (size) {
        case 1: return "i8/u8/bool";
        case 2: return "i16/u16";
        case 4: return "i32/u32/f32";
        case 8: return "i64/u64/f64";
    }
    return "-";
}

static void symbol_display_name(const char *raw,
                                int demangle,
                                char *out,
                                size_t cap) {
    if (!raw || !out || cap == 0) return;
    if (demangle && demangle_itanium_name(raw, out, cap) == 0) return;
    if (strlen(raw) >= cap) {
        out[0] = '\0';
        return;
    }
    strcpy(out, raw);
}

static int symbol_filter_matches(const char *raw,
                                 const char *display,
                                 const char *filter) {
    char demangled[PROBE_MAX_PATH];

    if (!filter || !*filter) return 1;
    if (raw && strstr(raw, filter)) return 1;
    if (display && strstr(display, filter)) return 1;
    if (raw && demangle_itanium_name(raw, demangled, sizeof(demangled)) == 0 &&
        strstr(demangled, filter)) {
        return 1;
    }
    return 0;
}

static void symbol_print(const char *name,
                         uintptr_t value,
                         unsigned char type,
                         unsigned char bind,
                         Elf64_Xword size,
                         int show_types) {
    if (show_types) {
        printf("%s\tkind=%s\tbind=%s\tsize=%" PRIu64
               "\taddr=0x%" PRIxPTR "\thint=%s\n",
               name,
               symbol_elf_type_name(type),
               symbol_elf_bind_name(bind),
               (uint64_t)size,
               value,
               symbol_probe_type_hint(type, size));
    } else {
        printf("%s\n", name);
    }
}

static int scan_elf64_symbols_for_listing(int fd,
                                          const Elf64_Shdr *sections,
                                          size_t section_count,
                                          size_t sym_index,
                                          const symbol_options *opt,
                                          unsigned long *emitted) {
    const Elf64_Shdr *symsec = &sections[sym_index];
    const Elf64_Shdr *strsec;
    size_t count;
    size_t i;

    if (!opt || !emitted) return -1;
    if (symsec->sh_link >= section_count) return -1;
    if (symsec->sh_entsize < sizeof(Elf64_Sym)) return -1;

    strsec = &sections[symsec->sh_link];
    if (strsec->sh_size == 0) return -1;

    count = (size_t)(symsec->sh_size / symsec->sh_entsize);
    for (i = 0; i < count; ++i) {
        Elf64_Sym sym;
        unsigned char type;
        unsigned char bind;
        char raw[PROBE_MAX_PATH];
        char display[PROBE_MAX_PATH];
        off_t off = (off_t)(symsec->sh_offset + (Elf64_Off)(i * symsec->sh_entsize));

        if (read_exact_at(fd, off, &sym, sizeof(sym)) != 0) return -1;
        if (sym.st_name == 0 || sym.st_name >= strsec->sh_size) continue;
        if (sym.st_shndx == SHN_UNDEF) continue;

        type = ELF64_ST_TYPE(sym.st_info);
        if (!symbol_is_watchable_type(type)) continue;

        if (read_elf_string(fd, strsec, sym.st_name, raw, sizeof(raw)) != 0) {
            return -1;
        }
        symbol_display_name(raw, opt->demangle, display, sizeof(display));
        if (display[0] == '\0') continue;
        if (!symbol_filter_matches(raw, display, opt->filter)) continue;

        bind = ELF64_ST_BIND(sym.st_info);
        symbol_print(display,
                     (uintptr_t)sym.st_value,
                     type,
                     bind,
                     sym.st_size,
                     opt->show_types);
        (*emitted)++;
    }

    return 0;
}

static int list_elf64_symbols(const symbol_options *opt) {
    int fd;
    Elf64_Ehdr ehdr;
    Elf64_Shdr sections[PROBE_MAX_ELF_SECTIONS];
    unsigned long emitted = 0;
    size_t i;

    if (!opt || !opt->object[0]) return -1;

    fd = open(opt->object, O_RDONLY);
    if (fd < 0) return -1;

    if (read_exact_at(fd, 0, &ehdr, sizeof(ehdr)) != 0) {
        close(fd);
        return -1;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr.e_shnum == 0) {
        close(fd);
        return -1;
    }
    if (ehdr.e_shnum > PROBE_MAX_ELF_SECTIONS) {
        close(fd);
        return -1;
    }

    if (read_exact_at(fd, (off_t)ehdr.e_shoff, sections,
                      (size_t)ehdr.e_shnum * sizeof(*sections)) != 0) {
        close(fd);
        return -1;
    }

    for (i = 0; i < (size_t)ehdr.e_shnum; ++i) {
        if (sections[i].sh_type != SHT_SYMTAB && sections[i].sh_type != SHT_DYNSYM) {
            continue;
        }
        if (scan_elf64_symbols_for_listing(fd,
                                           sections,
                                           (size_t)ehdr.e_shnum,
                                           i,
                                           opt,
                                           &emitted) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return emitted > 0 ? 0 : 1;
}

static void symbols_usage(FILE *f) {
    fprintf(f,
            "Usage: probe symbols OBJECT [--filter TEXT] [--demangle] [--types]\n");
}

static int parse_symbols_args(int argc, char **argv, symbol_options *opt) {
    int i;
    int have_object = 0;

    if (!opt) return -1;
    memset(opt, 0, sizeof(*opt));

    for (i = 2; i < argc; ++i) {
        const char *a = argv[i];

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            symbols_usage(stdout);
            exit(0);
        } else if (!strcmp(a, "--filter")) {
            if (++i >= argc || !argv[i][0]) return -1;
            opt->filter = argv[i];
        } else if (!strcmp(a, "--demangle")) {
            opt->demangle = 1;
        } else if (!strcmp(a, "--types")) {
            opt->show_types = 1;
        } else if (a[0] == '-') {
            return -1;
        } else if (!have_object) {
            if (realpath_or_copy(a, opt->object, sizeof(opt->object)) != 0) return -1;
            have_object = 1;
        } else {
            return -1;
        }
    }

    return have_object ? 0 : -1;
}

static int symbols_main(int argc, char **argv) {
    symbol_options opt;
    int rc;

    if (parse_symbols_args(argc, argv, &opt) != 0) {
        symbols_usage(stderr);
        return 2;
    }

    rc = list_elf64_symbols(&opt);
    if (rc < 0) {
        fprintf(stderr, "probe: could not read symbols from '%s'\n", opt.object);
        return 2;
    }
    return rc == 0 ? 0 : 1;
}

static void plan_usage(FILE *f) {
    fprintf(f,
            "Usage: probe plan --watch KEY=TYPE@symbol:SYMBOL "
            "[--watch KEY=TYPE@addr:ADDR] -- PROGRAM [args...]\n");
}

static int plan_arg_is_safe(const char *s) {
    size_t i;

    if (!s || !*s) return 0;
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' ||
              c == '/' || c == ':' || c == '@' || c == '=')) {
            return 0;
        }
    }
    return 1;
}

static void plan_print_shell_arg(FILE *out, const char *s) {
    if (plan_arg_is_safe(s)) {
        fputs(s, out);
        return;
    }

    fputc('\'', out);
    while (s && *s) {
        if (*s == '\'') {
            fputs("'\\''", out);
        } else {
            fputc(*s, out);
        }
        s++;
    }
    fputc('\'', out);
}

static int plan_format_watch_arg(const plan_watch *watch,
                                 char *out,
                                 size_t cap,
                                 const char **flag) {
    const watch *w;
    int n;

    if (!watch || !out || cap == 0 || !flag) return -1;
    w = &watch->resolved;
    if (w->from_symbol) {
        *flag = "-s";
        if (w->object[0]) {
            n = snprintf(out, cap, "%s:%s:%s@%s",
                         w->name, type_name(w->type), w->symbol, w->object);
        } else {
            n = snprintf(out, cap, "%s:%s:%s",
                         w->name, type_name(w->type), w->symbol);
        }
    } else {
        *flag = "-w";
        n = snprintf(out, cap, "%s:%s:%s",
                     w->name,
                     type_name(w->type),
                     watch->addr_text[0] ? watch->addr_text : "0x0");
    }

    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int parse_plan_args(int argc, char **argv, plan_options *opt) {
    int i;

    if (!opt) return -1;
    memset(opt, 0, sizeof(*opt));

    for (i = 2; i < argc; ++i) {
        const char *a = argv[i];

        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            plan_usage(stdout);
            exit(0);
        } else if (!strcmp(a, "--")) {
            if (i + 1 >= argc) return -1;
            opt->run_argv = &argv[i + 1];
            break;
        } else if (!strcmp(a, "--watch")) {
            if (++i >= argc || opt->watch_count >= PROBE_MAX_WATCHES) {
                return -1;
            }
            if (parse_plan_watch_spec(argv[i],
                                      &opt->watches[opt->watch_count]) != 0) {
                return -1;
            }
            opt->watch_count++;
        } else {
            return -1;
        }
    }

    return opt->watch_count > 0 && opt->run_argv ? 0 : -1;
}

static int plan_main(int argc, char **argv) {
    plan_options opt;
    size_t i;
    int argi;

    if (parse_plan_args(argc, argv, &opt) != 0) {
        plan_usage(stderr);
        return 2;
    }

    fputs("probe", stdout);
    for (i = 0; i < opt.watch_count; ++i) {
        char spec[PROBE_MAX_PATH + PROBE_MAX_NAME * 2];
        const char *flag = NULL;

        if (plan_format_watch_arg(&opt.watches[i], spec, sizeof(spec), &flag) != 0) {
            fprintf(stderr, "probe: could not format planned watch\n");
            return 2;
        }
        fputs(" \\\n  ", stdout);
        plan_print_shell_arg(stdout, flag);
        fputc(' ', stdout);
        plan_print_shell_arg(stdout, spec);
    }

    fputs(" \\\n  --", stdout);
    for (argi = 0; opt.run_argv[argi]; ++argi) {
        fputc(' ', stdout);
        plan_print_shell_arg(stdout, opt.run_argv[argi]);
    }
    fputc('\n', stdout);
    return 0;
}

static int object_load_base(pid_t pid, const char *object, uintptr_t *out) {
    char maps_path[64], object_real[PROBE_MAX_PATH], line[8192];
    if (realpath_or_copy(object, object_real, sizeof(object_real)) != 0) return -1;
    snprintf(maps_path, sizeof(maps_path), "/proc/%ld/maps", (long)pid);
    FILE *fp = fopen(maps_path, "r");
    if (!fp) return -1;
    uintptr_t best = (uintptr_t)-1;
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long start = 0, end = 0, offset = 0;
        char perms[8] = {0};
        char path[PROBE_MAX_PATH] = {0};
        int fields = sscanf(line, "%lx-%lx %7s %lx %*s %*s %4095[^\n]", &start, &end, perms, &offset, path);
        (void)end; (void)perms;
        if (fields == 5) {
            char path_real[PROBE_MAX_PATH];
            char *p = path;
            while (*p == ' ') p++;
            if (realpath_or_copy(p, path_real, sizeof(path_real)) != 0) {
                strncpy(path_real, p, sizeof(path_real)-1);
                path_real[sizeof(path_real)-1] = '\0';
            }
            if (!strcmp(path_real, object_real) || !strcmp(p, object_real)) {
                uintptr_t base = (uintptr_t)start - (uintptr_t)offset;
                if (base < best) best = base;
                found = 1;
            }
        }
    }
    fclose(fp);
    if (!found) return -1;
    *out = best;
    return 0;
}

static int resolve_watch_address(pid_t pid, watch *w, const char *default_object) {
    if (!w->from_symbol) return 0;
    if (w->field_path) {
        fprintf(stderr,
                "probe: DWARF field paths are not implemented yet for '%s'\n",
                w->name);
        return -1;
    }
    char object[PROBE_MAX_PATH];
    if (w->object[0]) {
        if (realpath_or_copy(w->object, object, sizeof(object)) != 0) return -1;
    } else if (default_object && *default_object) {
        if (realpath_or_copy(default_object, object, sizeof(object)) != 0) return -1;
    } else if (read_exe_path(pid, object, sizeof(object)) != 0) {
        return -1;
    }
    uintptr_t sym = 0;
    if (resolve_symbol_elf(object, w->symbol, &sym) != 0) return -1;
    int etype = 0;
    if (elf_type(object, &etype) != 0) return -1;
    if (etype == 3) { /* ET_DYN: PIE executable or shared object */
        uintptr_t base = 0;
        if (object_load_base(pid, object, &base) != 0) return -1;
        w->addr = base + sym;
    } else {
        w->addr = sym;
    }
    return 0;
}

static int read_remote(pid_t pid, uintptr_t addr, void *buf, size_t n) {
    struct iovec local = { .iov_base = buf, .iov_len = n };
    struct iovec remote = { .iov_base = (void *)addr, .iov_len = n };
    ssize_t r = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return (r == (ssize_t)n) ? 0 : -1;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int read_value(pid_t pid,
                      const watch *w,
                      char *out,
                      size_t cap,
                      double *numeric,
                      int *nonfinite) {
    unsigned char buf[8] = {0};
    if (nonfinite) *nonfinite = 0;
    if (read_remote(pid, w->addr, buf, type_size(w->type)) != 0) return -1;
    switch (w->type) {
        case P_I8: {
            int8_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%d", (int)v);
            break;
        }
        case P_U8: {
            uint8_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%u", (unsigned)v);
            break;
        }
        case P_I16: {
            int16_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%d", (int)v);
            break;
        }
        case P_U16: {
            uint16_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%u", (unsigned)v);
            break;
        }
        case P_I32: {
            int32_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%" PRId32, v);
            break;
        }
        case P_U32: {
            uint32_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%" PRIu32, v);
            break;
        }
        case P_I64: {
            int64_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%" PRId64, v);
            break;
        }
        case P_U64: {
            uint64_t v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%" PRIu64, v);
            break;
        }
        case P_F32: {
            float v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = (double)v;
            if (nonfinite && !isfinite((double)v)) *nonfinite = 1;
            snprintf(out, cap, "%.9g", (double)v);
            break;
        }
        case P_F64: {
            double v;
            memcpy(&v, buf, sizeof(v));
            if (numeric) *numeric = v;
            if (nonfinite && !isfinite(v)) *nonfinite = 1;
            snprintf(out, cap, "%.17g", v);
            break;
        }
        case P_BOOL: {
            unsigned v = buf[0] ? 1u : 0u;
            if (numeric) *numeric = (double)v;
            snprintf(out, cap, "%u", v);
            break;
        }
    }
    return 0;
}

static int child_alive(pid_t pid) {
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    return r == 0;
}

static void print_json_string(FILE *out, const char *s) {
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

static void emit_sample(const options *opt,
                        const watch *w,
                        const char *value,
                        int nonfinite,
                        long long ts_ms) {
    if (opt->output == OUT_JSONL) {
        fputs("{\"ts_ms\":", stdout);
        fprintf(stdout, "%lld,\"name\":", ts_ms);
        print_json_string(stdout, w->name);
        fputs(",\"type\":", stdout);
        print_json_string(stdout, type_name(w->type));
        fprintf(stdout, ",\"addr\":\"0x%" PRIxPTR "\",\"value\":", w->addr);
        if (nonfinite) {
            fputs("null,\"nonfinite\":", stdout);
            print_json_string(stdout, value);
            fputs("}\n", stdout);
        } else {
            fprintf(stdout, "%s}\n", value);
        }
    } else if (opt->output == OUT_LIMLOG) {
        fprintf(stdout,
                "ts_ms=%lld key=%s type=%s addr=0x%" PRIxPTR " value=%s\n",
                ts_ms, w->name, type_name(w->type), w->addr, value);
    } else {
        printf("%s=%s\n", w->name, value);
    }
}

static int put_event_line(const apsis_event *event, void *user) {
    char line[APSIS_LINE_MAX];

    (void)user;
    if (!event) return -1;
    if (apsis_format_event_record(event, line, sizeof(line)) != 0) return -1;
    puts(line);
    return 0;
}

static int sample_loop(options *opt) {
    for (size_t i = 0; i < opt->watch_count; ++i) {
        if (resolve_watch_address(opt->pid, &opt->watches[i], opt->default_object) != 0) {
            fprintf(stderr, "probe: could not resolve watch '%s'\n", opt->watches[i].name);
            return 2;
        }
        if (opt->verbose) {
            fprintf(stderr, "probe: %s %s @ 0x%" PRIxPTR "\n",
                    opt->watches[i].name, type_name(opt->watches[i].type), opt->watches[i].addr);
        }
    }

    int samples = opt->count <= 0 ? 1 : opt->count;
    for (int iter = 0; iter < samples; ++iter) {
        for (size_t i = 0; i < opt->watch_count; ++i) {
            char value[128];
            double numeric = 0.0;
            int nonfinite = 0;
            long long ts_ms;

            if (read_value(opt->pid,
                           &opt->watches[i],
                           value,
                           sizeof(value),
                           &numeric,
                           &nonfinite) != 0) {
                fprintf(stderr, "probe: read failed for '%s' at 0x%" PRIxPTR ": %s\n",
                        opt->watches[i].name, opt->watches[i].addr, strerror(errno));
                return 3;
            }
            ts_ms = now_ms();

            if (opt->emit == EMIT_SAMPLES || opt->emit == EMIT_BOTH) {
                emit_sample(opt, &opt->watches[i], value, nonfinite, ts_ms);
            }

            if (opt->rules_path) {
                apsis_event_fn emit = NULL;
                int emitted;

                if (opt->emit == EMIT_EVENTS || opt->emit == EMIT_BOTH) {
                    emit = put_event_line;
                }
                emitted = apsis_sample_each(&opt->rules,
                                          opt->watches[i].name,
                                          numeric,
                                          apsis_now_seconds(),
                                          emit,
                                          NULL);
                if (emitted < 0) {
                    fprintf(stderr, "probe: failed to evaluate rules for '%s'\n",
                            opt->watches[i].name);
                    return 3;
                }
            }
        }
        fflush(stdout);
        if (iter + 1 < samples && opt->interval_ms > 0) {
            struct timespec req;
            req.tv_sec = opt->interval_ms / 1000;
            req.tv_nsec = (long)(opt->interval_ms % 1000) * 1000000L;
            nanosleep(&req, NULL);
        }
    }
    if (opt->rules_path) {
        apsis_event_fn emit = NULL;

        if (opt->emit == EMIT_EVENTS || opt->emit == EMIT_BOTH) {
            emit = put_event_line;
        }

        if (apsis_emit_stale_each(&opt->rules, apsis_now_seconds(),
                                emit, NULL) < 0) {
            return 3;
        }
    }
    return 0;
}

static int parse_int_arg(const char *s, int *out) {
    char *end = NULL;
    long v;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || !end || *end != '\0' || v < 0 || v > 2147483647L) return -1;
    *out = (int)v;
    return 0;
}

static int parse_output_format(const char *s, probe_output *out) {
    if (!s || !out) return -1;
    if (!strcmp(s, "keyvalue") || !strcmp(s, "kv")) {
        *out = OUT_KEYVALUE;
        return 0;
    }
    if (!strcmp(s, "jsonl") || !strcmp(s, "json")) {
        *out = OUT_JSONL;
        return 0;
    }
    if (!strcmp(s, "limlog")) {
        *out = OUT_LIMLOG;
        return 0;
    }
    return -1;
}

static int parse_emit_mode(const char *s, probe_emit *out) {
    if (!s || !out) return -1;
    if (!strcmp(s, "samples") || !strcmp(s, "sample")) {
        *out = EMIT_SAMPLES;
        return 0;
    }
    if (!strcmp(s, "events") || !strcmp(s, "event")) {
        *out = EMIT_EVENTS;
        return 0;
    }
    if (!strcmp(s, "both")) {
        *out = EMIT_BOTH;
        return 0;
    }
    return -1;
}

static int parse_args(int argc, char **argv, options *opt) {
    int command_mode = 0;

    memset(opt, 0, sizeof(*opt));
    opt->interval_ms = 1000;
    opt->count = 1;
    opt->delay_ms = 100;
    opt->output = OUT_KEYVALUE;
    opt->emit = EMIT_AUTO;
    opt->child_mode = CHILD_TERMINATE;
    opt->fail_on = APSIS_ERROR;
    apsis_init(&opt->rules);

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (i == 1 && !strcmp(a, "run")) {
            command_mode = 1;
        } else if (i == 1 && !strcmp(a, "attach")) {
            command_mode = 2;
        } else if (!strcmp(a, "--")) {
            if (i + 1 >= argc) return -1;
            opt->run_argv = &argv[i + 1];
            break;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            exit(0);
        } else if (!strcmp(a, "-p") || !strcmp(a, "--pid")) {
            if (++i >= argc) return -1;
            int v;
            if (parse_int_arg(argv[i], &v) != 0 || v <= 0) return -1;
            opt->pid = (pid_t)v;
            opt->have_pid = 1;
        } else if (!strcmp(a, "-i") || !strcmp(a, "--interval-ms")) {
            if (++i >= argc || parse_int_arg(argv[i], &opt->interval_ms) != 0) return -1;
        } else if (!strcmp(a, "-n") || !strcmp(a, "--count")) {
            if (++i >= argc || parse_int_arg(argv[i], &opt->count) != 0) return -1;
        } else if (!strcmp(a, "--delay-ms")) {
            if (++i >= argc || parse_int_arg(argv[i], &opt->delay_ms) != 0) return -1;
        } else if (!strcmp(a, "--json")) {
            opt->output = OUT_JSONL;
        } else if (!strcmp(a, "--format")) {
            if (++i >= argc || parse_output_format(argv[i], &opt->output) != 0) return -1;
        } else if (!strcmp(a, "--emit")) {
            if (++i >= argc || parse_emit_mode(argv[i], &opt->emit) != 0) return -1;
        } else if (!strcmp(a, "-r") || !strcmp(a, "--rules")) {
            if (++i >= argc) return -1;
            opt->rules_path = argv[i];
        } else if (!strcmp(a, "--fail-on")) {
            const char *level;
            if (++i >= argc) return -1;
            level = argv[i];
            if (!strcmp(level, "never")) {
                opt->fail_never = 1;
            } else if (apsis_parse_level(level, &opt->fail_on) != 0) {
                return -1;
            }
        } else if (!strcmp(a, "--ebpf") || !strcmp(a, "--uprobe")) {
            opt->ebpf = 1;
        } else if (!strcmp(a, "--terminate-after-sampling")) {
            opt->child_mode = CHILD_TERMINATE;
        } else if (!strcmp(a, "--wait-child")) {
            opt->child_mode = CHILD_WAIT;
        } else if (!strcmp(a, "--leave-running")) {
            opt->child_mode = CHILD_LEAVE;
        } else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
            opt->verbose = 1;
        } else if (!strcmp(a, "-o") || !strcmp(a, "--object")) {
            if (++i >= argc) return -1;
            if (realpath_or_copy(argv[i], opt->default_object, sizeof(opt->default_object)) != 0) return -1;
        } else if (!strcmp(a, "-w") || !strcmp(a, "--watch-addr") ||
                   !strcmp(a, "--addr") || !strcmp(a, "--address")) {
            if (++i >= argc || opt->watch_count >= PROBE_MAX_WATCHES) return -1;
            if (parse_watch_spec(argv[i], 0, &opt->watches[opt->watch_count]) != 0) return -1;
            opt->watch_count++;
        } else if (!strcmp(a, "--watch")) {
            plan_watch planned;

            if (++i >= argc || opt->watch_count >= PROBE_MAX_WATCHES) return -1;
            if (parse_plan_watch_spec(argv[i], &planned) != 0) return -1;
            opt->watches[opt->watch_count] = planned.resolved;
            opt->watch_count++;
        } else if (!strcmp(a, "-s") || !strcmp(a, "--watch-symbol") ||
                   !strcmp(a, "--symbol")) {
            if (++i >= argc || opt->watch_count >= PROBE_MAX_WATCHES) return -1;
            if (parse_watch_spec(argv[i], 1, &opt->watches[opt->watch_count]) != 0) return -1;
            opt->watch_count++;
        } else {
            fprintf(stderr, "probe: unknown argument: %s\n", a);
            return -1;
        }
    }

    if (opt->watch_count == 0) return -1;
    if (command_mode == 1 && (!opt->run_argv || opt->have_pid)) return -1;
    if (command_mode == 2 && (!opt->have_pid || opt->run_argv)) return -1;
    if (!opt->have_pid && !opt->run_argv) return -1;
    if (opt->have_pid && opt->run_argv) return -1;
    if (opt->emit == EMIT_AUTO) {
        opt->emit = opt->rules_path ? EMIT_EVENTS : EMIT_SAMPLES;
    }
    if ((opt->emit == EMIT_EVENTS || opt->emit == EMIT_BOTH) &&
        !opt->rules_path) {
        return -1;
    }
    return 0;
}

static int pid_exists(pid_t pid) {
    if (kill(pid, 0) == 0) return 1;
    return errno == EPERM;
}

static void terminate_child(pid_t child) {
    int status;
    int i;
    struct timespec req;

    if (child <= 0) return;
    if (waitpid(child, &status, WNOHANG) != 0) return;

    kill(child, SIGTERM);
    req.tv_sec = 0;
    req.tv_nsec = 50 * 1000 * 1000;
    for (i = 0; i < 20; ++i) {
        pid_t r = waitpid(child, &status, WNOHANG);
        if (r != 0) return;
        nanosleep(&req, NULL);
    }

    kill(child, SIGKILL);
    (void)waitpid(child, NULL, 0);
}

static int finish_child(pid_t child, probe_child_mode mode, int sample_rc) {
    int status;

    if (child <= 0) return 0;
    if (mode == CHILD_LEAVE) return 0;
    if (mode == CHILD_WAIT && sample_rc == 0) {
        while (waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) return 3;
        }
        return 0;
    }

    terminate_child(child);
    return 0;
}

static int rules_exit_status(const options *opt) {
    if (!opt->rules_path || opt->fail_never) return 0;
    if (opt->fail_on == APSIS_ERROR && opt->rules.error_count > 0) return 1;
    if (opt->fail_on == APSIS_WARN &&
        (opt->rules.warn_count > 0 || opt->rules.error_count > 0)) {
        return 1;
    }
    if (opt->fail_on == APSIS_INFO && opt->rules.events_emitted > 0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    options opt;
    char err[256];
    pid_t child = -1;
    int rc;

    if (argc >= 2 && !strcmp(argv[1], "symbols")) {
        return symbols_main(argc, argv);
    }
    if (argc >= 2 && !strcmp(argv[1], "plan")) {
        return plan_main(argc, argv);
    }

    if (parse_args(argc, argv, &opt) != 0) {
        usage(stderr);
        return 2;
    }

    if (opt.ebpf) {
        fprintf(stderr, "probe: eBPF/uprobe mode is reserved but not implemented yet\n");
        return 2;
    }

    if (opt.rules_path &&
        apsis_load_rules_file(&opt.rules, opt.rules_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "probe: %s\n", err);
        return 2;
    }

    if (opt.run_argv) {
        if (!opt.default_object[0]) {
            if (resolve_exec_object(opt.run_argv[0],
                                    opt.default_object,
                                    sizeof(opt.default_object)) != 0) {
                fprintf(stderr, "probe: cannot resolve target object '%s'\n",
                        opt.run_argv[0]);
                return 2;
            }
        }
        child = fork();
        if (child < 0) {
            fprintf(stderr, "probe: fork failed: %s\n", strerror(errno));
            return 2;
        }
        if (child == 0) {
            execvp(opt.run_argv[0], opt.run_argv);
            fprintf(stderr, "probe: exec failed: %s\n", strerror(errno));
            _exit(127);
        }
        opt.pid = child;
        opt.have_pid = 1;
        if (opt.delay_ms > 0) {
            struct timespec req;
            req.tv_sec = opt.delay_ms / 1000;
            req.tv_nsec = (long)(opt.delay_ms % 1000) * 1000000L;
            nanosleep(&req, NULL);
        }
        if (!child_alive(child)) {
            fprintf(stderr, "probe: child exited before sampling\n");
            (void)waitpid(child, NULL, 0);
            return 3;
        }
    } else if (!pid_exists(opt.pid)) {
        fprintf(stderr, "probe: pid %ld does not exist or is not accessible\n",
                (long)opt.pid);
        return 2;
    }

    rc = sample_loop(&opt);

    {
        int child_rc = finish_child(child, opt.child_mode, rc);
        if (child_rc != 0 && rc == 0) rc = child_rc;
    }

    if (rc != 0) return rc;
    return rules_exit_status(&opt);
}
#else
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef PROBE_MAX_WATCHES
#define PROBE_MAX_WATCHES 64
#endif
#ifndef PROBE_MAX_NAME
#define PROBE_MAX_NAME 128
#endif
#ifndef PROBE_MAX_PATH
#define PROBE_MAX_PATH 4096
#endif

typedef struct portable_plan_watch {
    char flag[3];
    char spec[PROBE_MAX_PATH + PROBE_MAX_NAME * 2];
} portable_plan_watch;

static void portable_plan_usage(FILE *f) {
    fprintf(f,
            "Usage: probe plan --watch KEY=TYPE@symbol:SYMBOL "
            "[--watch KEY=TYPE@addr:ADDR] -- PROGRAM [args...]\n");
}

static int portable_plan_valid_name(const char *s) {
    size_t i;

    if (!s || !*s || strlen(s) >= PROBE_MAX_NAME) return 0;
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' ||
              c == ':' || c == '/')) {
            return 0;
        }
    }
    return 1;
}

static int portable_plan_valid_type(const char *s) {
    static const char *types[] = {
        "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "f32", "f64", NULL
    };
    size_t i;

    if (!s || !*s) return 0;
    for (i = 0; types[i]; ++i) {
        if (strcmp(s, types[i]) == 0) return 1;
    }
    return 0;
}

static int portable_plan_arg_is_safe(const char *s) {
    size_t i;

    if (!s || !*s) return 0;
    for (i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' ||
              c == '/' || c == ':' || c == '@' || c == '=')) {
            return 0;
        }
    }
    return 1;
}

static void portable_plan_print_shell_arg(FILE *out, const char *s) {
    if (portable_plan_arg_is_safe(s)) {
        fputs(s, out);
        return;
    }

    fputc('\'', out);
    while (s && *s) {
        if (*s == '\'') {
            fputs("'\\''", out);
        } else {
            fputc(*s, out);
        }
        s++;
    }
    fputc('\'', out);
}

static int portable_plan_parse_watch(const char *spec, portable_plan_watch *out) {
    char tmp[PROBE_MAX_PATH + PROBE_MAX_NAME * 2];
    char *eq;
    char *at;
    char *source;
    char *object;
    int n;

    if (!spec || !out || strlen(spec) >= sizeof(tmp)) return -1;
    memset(out, 0, sizeof(*out));
    strcpy(tmp, spec);

    eq = strchr(tmp, '=');
    if (!eq) return -1;
    *eq = '\0';
    if (!portable_plan_valid_name(tmp)) return -1;

    at = strchr(eq + 1, '@');
    if (!at) return -1;
    *at = '\0';
    if (!portable_plan_valid_type(eq + 1)) return -1;

    source = at + 1;
    if (!strncmp(source, "symbol:", 7)) {
        char *symbol = source + 7;

        object = strstr(symbol, "@object:");
        if (object) {
            *object = '\0';
            object += 8;
            if (!*object) return -1;
        }
        if (!*symbol) return -1;
        strcpy(out->flag, "-s");
        if (object && *object) {
            n = snprintf(out->spec, sizeof(out->spec), "%s:%s:%s@%s",
                         tmp, eq + 1, symbol, object);
        } else {
            n = snprintf(out->spec, sizeof(out->spec), "%s:%s:%s",
                         tmp, eq + 1, symbol);
        }
        return n > 0 && (size_t)n < sizeof(out->spec) ? 0 : -1;
    }

    if (!strncmp(source, "addr:", 5)) {
        char *addr = source + 5;
        char *end = NULL;

        if (!*addr) return -1;
        errno = 0;
        (void)strtoull(addr, &end, 0);
        if (errno || !end || *end) return -1;
        strcpy(out->flag, "-w");
        n = snprintf(out->spec, sizeof(out->spec), "%s:%s:%s",
                     tmp, eq + 1, addr);
        return n > 0 && (size_t)n < sizeof(out->spec) ? 0 : -1;
    }

    return -1;
}

static int portable_plan_main(int argc, char **argv) {
    portable_plan_watch watches[PROBE_MAX_WATCHES];
    size_t watch_count = 0;
    char **run_argv = NULL;
    int i;

    memset(watches, 0, sizeof(watches));
    for (i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            portable_plan_usage(stdout);
            return 0;
        } else if (!strcmp(argv[i], "--")) {
            if (i + 1 >= argc) return 2;
            run_argv = &argv[i + 1];
            break;
        } else if (!strcmp(argv[i], "--watch")) {
            if (++i >= argc || watch_count >= PROBE_MAX_WATCHES) return 2;
            if (portable_plan_parse_watch(argv[i], &watches[watch_count]) != 0) {
                return 2;
            }
            watch_count++;
        } else {
            return 2;
        }
    }

    if (watch_count == 0 || !run_argv) return 2;

    fputs("probe", stdout);
    for (i = 0; i < (int)watch_count; ++i) {
        fputs(" \\\n  ", stdout);
        portable_plan_print_shell_arg(stdout, watches[i].flag);
        fputc(' ', stdout);
        portable_plan_print_shell_arg(stdout, watches[i].spec);
    }
    fputs(" \\\n  --", stdout);
    for (i = 0; run_argv[i]; ++i) {
        fputc(' ', stdout);
        portable_plan_print_shell_arg(stdout, run_argv[i]);
    }
    fputc('\n', stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "plan") == 0) {
        return portable_plan_main(argc, argv);
    }

    fprintf(stderr, "probe: Linux-only; requires /proc and process_vm_readv(2)\n");
    return 2;
}
#endif
