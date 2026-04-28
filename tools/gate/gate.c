/*
 * gate.c - one-command apsis workflow runner
 *
 * Orchestrates:
 *   atlas -> bind -> probe -> trip
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GATE_MAX_PATH       4096
#define GATE_MAX_ARG        4096
#define GATE_MAX_ARGS        256
#define GATE_MAX_WATCHES      64
#define GATE_LINE_MAX       8192

#define GATE_EXIT_OK           0
#define GATE_EXIT_CONTRACT     1
#define GATE_EXIT_USAGE        2

typedef enum gate_format {
    GATE_FORMAT_TEXT = 0,
    GATE_FORMAT_JSONL,
    GATE_FORMAT_GITHUB
} gate_format;

typedef struct gate_options {
    const char *atlas_path;
    const char *binary_path;
    const char *summary_file;
    const char *fail_on;
    char count[32];
    char interval_ms[32];
    gate_format format;
    int explain;
    int keep_temp;
    char **command;
} gate_options;

typedef struct gate_paths {
    char tool_dir[GATE_MAX_PATH];
    char atlas[GATE_MAX_PATH];
    char bind[GATE_MAX_PATH];
    char probe[GATE_MAX_PATH];
    char trip[GATE_MAX_PATH];
    char rules[GATE_MAX_PATH];
    char watch[GATE_MAX_PATH];
    int have_rules;
    int have_watch;
} gate_paths;

typedef struct gate_watch_plan {
    char specs[GATE_MAX_WATCHES][GATE_MAX_ARG];
    size_t count;
} gate_watch_plan;

typedef enum gate_stdout_mode {
    GATE_STDOUT_INHERIT = 0,
    GATE_STDOUT_STDERR,
    GATE_STDOUT_FILE
} gate_stdout_mode;

static void gate_usage(FILE *out) {
    fprintf(out,
            "usage: gate --atlas FILE --binary PATH [options] -- COMMAND [ARGS...]\n"
            "\n"
            "Run atlas -> bind -> probe -> trip as one workflow.\n"
            "\n"
            "Options:\n"
            "  --atlas FILE          required atlas dictionary\n"
            "  --binary PATH         required binary/object used by bind\n"
            "  --count N             sample count passed to probe; default 10\n"
            "  --interval DURATION   sample interval; default 100ms\n"
            "  --fail-on LEVEL       info, warn, error, or never; default error\n"
            "  --format FORMAT       text, jsonl, or github; default text\n"
            "  --summary-file PATH   append Markdown summary for github output\n"
            "  --explain             print the generated workflow plan before running\n"
            "  --keep-temp           keep generated temporary files for debugging\n"
            "  -h, --help            show help\n");
}

static int gate_streq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static void gate_copy(char *dst, const char *src, size_t cap) {
    size_t i = 0;

    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void gate_rtrim(char *s) {
    size_t n;

    if (!s) return;
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int gate_parse_positive_int(const char *s,
                                   char *out,
                                   size_t out_cap) {
    char *end = NULL;
    long value;

    if (!s || !*s || !out || out_cap == 0) return -1;
    errno = 0;
    value = strtol(s, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value <= 0 ||
        value > 1000000L) {
        return -1;
    }
    snprintf(out, out_cap, "%ld", value);
    return 0;
}

static int gate_parse_interval_ms(const char *s,
                                  char *out,
                                  size_t out_cap) {
    char *end = NULL;
    long value;

    if (!s || !*s || !out || out_cap == 0) return -1;
    errno = 0;
    value = strtol(s, &end, 10);
    if (errno != 0 || !end || end == s || value <= 0 ||
        value > 86400000L) {
        return -1;
    }
    if (gate_streq(end, "ms") || gate_streq(end, "")) {
        /* already milliseconds */
    } else if (gate_streq(end, "s")) {
        if (value > 86400L) return -1;
        value *= 1000L;
    } else {
        return -1;
    }
    snprintf(out, out_cap, "%ld", value);
    return 0;
}

static int gate_valid_fail_on(const char *s) {
    return gate_streq(s, "info") || gate_streq(s, "warn") ||
           gate_streq(s, "error") || gate_streq(s, "never");
}

static int gate_parse_format(const char *s, gate_format *out) {
    if (!s || !out) return -1;
    if (gate_streq(s, "text")) {
        *out = GATE_FORMAT_TEXT;
    } else if (gate_streq(s, "jsonl")) {
        *out = GATE_FORMAT_JSONL;
    } else if (gate_streq(s, "github")) {
        *out = GATE_FORMAT_GITHUB;
    } else {
        return -1;
    }
    return 0;
}

static int gate_parse_args(int argc, char **argv, gate_options *opt) {
    int i;

    if (!opt) return -1;
    memset(opt, 0, sizeof(*opt));
    gate_copy(opt->count, "10", sizeof(opt->count));
    gate_copy(opt->interval_ms, "100", sizeof(opt->interval_ms));
    opt->fail_on = "error";
    opt->format = GATE_FORMAT_TEXT;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (gate_streq(arg, "-h") || gate_streq(arg, "--help")) {
            gate_usage(stdout);
            exit(GATE_EXIT_OK);
        } else if (gate_streq(arg, "--atlas")) {
            if (++i >= argc || !argv[i][0]) return -1;
            opt->atlas_path = argv[i];
        } else if (gate_streq(arg, "--binary")) {
            if (++i >= argc || !argv[i][0]) return -1;
            opt->binary_path = argv[i];
        } else if (gate_streq(arg, "--count")) {
            if (++i >= argc ||
                gate_parse_positive_int(argv[i],
                                        opt->count,
                                        sizeof(opt->count)) != 0) {
                return -1;
            }
        } else if (gate_streq(arg, "--interval")) {
            if (++i >= argc ||
                gate_parse_interval_ms(argv[i],
                                       opt->interval_ms,
                                       sizeof(opt->interval_ms)) != 0) {
                return -1;
            }
        } else if (gate_streq(arg, "--fail-on")) {
            if (++i >= argc || !gate_valid_fail_on(argv[i])) return -1;
            opt->fail_on = argv[i];
        } else if (gate_streq(arg, "--format")) {
            if (++i >= argc || gate_parse_format(argv[i], &opt->format) != 0) {
                return -1;
            }
        } else if (gate_streq(arg, "--summary-file")) {
            if (++i >= argc || !argv[i][0]) return -1;
            opt->summary_file = argv[i];
        } else if (gate_streq(arg, "--explain")) {
            opt->explain = 1;
        } else if (gate_streq(arg, "--keep-temp")) {
            opt->keep_temp = 1;
        } else if (gate_streq(arg, "--")) {
            if (i + 1 >= argc) return -1;
            opt->command = &argv[i + 1];
            break;
        } else {
            fprintf(stderr, "gate: unknown argument: %s\n", arg);
            return -1;
        }
    }

    if (!opt->atlas_path || !opt->binary_path || !opt->command) return -1;
    if (opt->summary_file && opt->format != GATE_FORMAT_GITHUB) {
        opt->format = GATE_FORMAT_GITHUB;
    }
    return 0;
}

static void gate_dirname(const char *argv0, char *out, size_t cap) {
    const char *slash;
    size_t len;

    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!argv0) return;

    slash = strrchr(argv0, '/');
    if (!slash) return;
    len = (size_t)(slash - argv0);
    if (len == 0) len = 1;
    if (len >= cap) return;
    memcpy(out, argv0, len);
    out[len] = '\0';
}

static void gate_join_path(const char *dir,
                           const char *name,
                           char *out,
                           size_t cap) {
    size_t dir_len;

    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!dir || !*dir) {
        gate_copy(out, name, cap);
        return;
    }

    dir_len = strlen(dir);
    if (dir_len + 1u + strlen(name) >= cap) return;
    strcpy(out, dir);
    if (dir[dir_len - 1] != '/') strcat(out, "/");
    strcat(out, name);
}

static void gate_resolve_tool(const char *tool_dir,
                              const char *name,
                              char *out,
                              size_t cap) {
    char candidate[GATE_MAX_PATH];

    if (!out || cap == 0) return;
    if (tool_dir && *tool_dir) {
        gate_join_path(tool_dir, name, candidate, sizeof(candidate));
        if (candidate[0] && access(candidate, X_OK) == 0) {
            gate_copy(out, candidate, cap);
            return;
        }
    }
    gate_copy(out, name, cap);
}

static int gate_make_temp(const char *label, char *out, size_t cap) {
    const char *tmpdir = getenv("TMPDIR");
    int fd;

    if (!label || !out || cap == 0) return -1;
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
    if (snprintf(out,
                 cap,
                 "%s/apsis-gate-%s-XXXXXX",
                 tmpdir,
                 label) >= (int)cap) {
        return -1;
    }
    fd = mkstemp(out);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

static int gate_init_paths(const char *argv0, gate_paths *paths) {
    if (!paths) return -1;
    memset(paths, 0, sizeof(*paths));
    gate_dirname(argv0, paths->tool_dir, sizeof(paths->tool_dir));
    gate_resolve_tool(paths->tool_dir, "atlas", paths->atlas, sizeof(paths->atlas));
    gate_resolve_tool(paths->tool_dir, "bind", paths->bind, sizeof(paths->bind));
    gate_resolve_tool(paths->tool_dir, "probe", paths->probe, sizeof(paths->probe));
    gate_resolve_tool(paths->tool_dir, "trip", paths->trip, sizeof(paths->trip));
    if (gate_make_temp("rules-trip", paths->rules, sizeof(paths->rules)) != 0) {
        return -1;
    }
    paths->have_rules = 1;
    if (gate_make_temp("watch-args", paths->watch, sizeof(paths->watch)) != 0) {
        return -1;
    }
    paths->have_watch = 1;
    return 0;
}

static void gate_cleanup(const gate_options *opt, gate_paths *paths) {
    if (!paths) return;
    if (opt && opt->keep_temp) {
        fprintf(stderr, "gate: keeping rules file: %s\n", paths->rules);
        fprintf(stderr, "gate: keeping watch file: %s\n", paths->watch);
        return;
    }
    if (paths->have_rules) unlink(paths->rules);
    if (paths->have_watch) unlink(paths->watch);
}

static int gate_arg_safe(const char *s) {
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

static void gate_print_shell_arg(FILE *out, const char *s) {
    if (gate_arg_safe(s)) {
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

static void gate_print_command(FILE *out, const char *const argv[]) {
    int i;

    if (!argv || !argv[0]) return;
    for (i = 0; argv[i]; ++i) {
        if (i > 0) fputc(' ', out);
        gate_print_shell_arg(out, argv[i]);
    }
    fputc('\n', out);
}

static void gate_print_failed_command(const char *const argv[]) {
    fputs("gate: command: ", stderr);
    gate_print_command(stderr, argv);
}

static int gate_wait_status(pid_t pid) {
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return GATE_EXIT_USAGE;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return GATE_EXIT_USAGE;
}

static void gate_exec_child(const char *const argv[]) {
    if (!argv || !argv[0]) _exit(127);
    if (strchr(argv[0], '/')) {
        execv(argv[0], (char *const *)argv);
    } else {
        execvp(argv[0], (char *const *)argv);
    }
    fprintf(stderr, "gate: exec failed for %s: %s\n", argv[0], strerror(errno));
    _exit(127);
}

static int gate_run_redirect(const char *const argv[],
                             gate_stdout_mode mode,
                             const char *stdout_path) {
    pid_t pid;

    pid = fork();
    if (pid < 0) return GATE_EXIT_USAGE;
    if (pid == 0) {
        if (mode == GATE_STDOUT_STDERR) {
            if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(127);
        } else if (mode == GATE_STDOUT_FILE) {
            int fd;

            fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                fprintf(stderr,
                        "gate: cannot write %s: %s\n",
                        stdout_path,
                        strerror(errno));
                _exit(127);
            }
            if (dup2(fd, STDOUT_FILENO) < 0) _exit(127);
            close(fd);
        }
        gate_exec_child(argv);
    }

    return gate_wait_status(pid);
}

static int gate_load_watch_plan(const char *path, gate_watch_plan *plan) {
    FILE *in;
    char line[GATE_LINE_MAX];

    if (!path || !plan) return -1;
    memset(plan, 0, sizeof(*plan));

    in = fopen(path, "r");
    if (!in) return -1;
    while (fgets(line, sizeof(line), in)) {
        char *spec;

        if (!strchr(line, '\n') && !feof(in)) {
            fclose(in);
            return -1;
        }
        gate_rtrim(line);
        if (!line[0]) continue;
        if (strncmp(line, "--watch ", 8) != 0) {
            fclose(in);
            return -1;
        }
        if (plan->count >= GATE_MAX_WATCHES) {
            fclose(in);
            return -1;
        }
        spec = line + 8;
        if (!*spec || strlen(spec) >= GATE_MAX_ARG) {
            fclose(in);
            return -1;
        }
        gate_copy(plan->specs[plan->count],
                  spec,
                  sizeof(plan->specs[plan->count]));
        plan->count++;
    }
    if (ferror(in)) {
        fclose(in);
        return -1;
    }
    fclose(in);
    return plan->count > 0 ? 0 : -1;
}

static void gate_json_string(FILE *out, const char *s) {
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

static void gate_emit_json_event(char *line) {
    char *fields[6];
    char *cursor = line;
    int i;

    gate_rtrim(line);
    for (i = 0; i < 6; ++i) {
        char *tab;

        fields[i] = cursor;
        tab = strchr(cursor, '\t');
        if (i < 5) {
            if (!tab) {
                fputs("{\"raw\":", stdout);
                gate_json_string(stdout, line);
                fputs("}\n", stdout);
                return;
            }
            *tab = '\0';
            cursor = tab + 1;
        }
    }

    fputs("{\"level\":", stdout);
    gate_json_string(stdout, fields[0]);
    fputs(",\"event_id\":", stdout);
    gate_json_string(stdout, fields[1]);
    fputs(",\"key\":", stdout);
    gate_json_string(stdout, fields[2]);
    fputs(",\"op\":", stdout);
    gate_json_string(stdout, fields[3]);
    fputs(",\"threshold\":", stdout);
    gate_json_string(stdout, fields[4]);
    fputs(",\"value\":", stdout);
    gate_json_string(stdout, fields[5]);
    fputs("}\n", stdout);
}

static int gate_transform_trip_jsonl(int fd) {
    FILE *in;
    char line[GATE_LINE_MAX];
    int failed = 0;

    in = fdopen(fd, "r");
    if (!in) {
        close(fd);
        return -1;
    }
    while (fgets(line, sizeof(line), in)) {
        if (!strchr(line, '\n') && !feof(in)) failed = 1;
        gate_emit_json_event(line);
    }
    if (ferror(in)) failed = 1;
    fclose(in);
    return failed ? -1 : 0;
}

static int gate_build_probe_argv(const gate_options *opt,
                                 const gate_paths *paths,
                                 const gate_watch_plan *plan,
                                 const char *probe_argv[],
                                 size_t cap) {
    size_t i;
    size_t idx = 0;
    int command_i;

    if (!opt || !paths || !plan || !probe_argv || cap == 0) return -1;
    if (idx >= cap) return -1;
    probe_argv[idx++] = paths->probe;
    if (idx >= cap) return -1;
    probe_argv[idx++] = "run";
    for (i = 0; i < plan->count; ++i) {
        if (idx + 2 >= cap) return -1;
        probe_argv[idx++] = "--watch";
        probe_argv[idx++] = plan->specs[i];
    }
    if (idx + 8 >= cap) return -1;
    probe_argv[idx++] = "--emit";
    probe_argv[idx++] = "samples";
    probe_argv[idx++] = "--count";
    probe_argv[idx++] = opt->count;
    probe_argv[idx++] = "--interval-ms";
    probe_argv[idx++] = opt->interval_ms;
    probe_argv[idx++] = "--";
    for (command_i = 0; opt->command[command_i]; ++command_i) {
        if (idx + 1 >= cap) return -1;
        probe_argv[idx++] = opt->command[command_i];
    }
    probe_argv[idx] = NULL;
    return 0;
}

static int gate_build_trip_argv(const gate_options *opt,
                                const gate_paths *paths,
                                const char *trip_argv[],
                                size_t cap,
                                const char **summary_path_out) {
    const char *summary = NULL;
    size_t idx = 0;

    if (!opt || !paths || !trip_argv || cap < 12) return -1;
    trip_argv[idx++] = paths->trip;
    trip_argv[idx++] = "check";
    trip_argv[idx++] = "--rules";
    trip_argv[idx++] = paths->rules;
    trip_argv[idx++] = "--fail-on";
    trip_argv[idx++] = opt->fail_on;

    if (opt->format == GATE_FORMAT_GITHUB) {
        summary = opt->summary_file;
        if (!summary || !*summary) summary = getenv("GITHUB_STEP_SUMMARY");
        if (!summary || !*summary) {
            fprintf(stderr,
                    "gate: --format github needs --summary-file "
                    "or GITHUB_STEP_SUMMARY\n");
            return -1;
        }
    }
    if (summary) {
        trip_argv[idx++] = "--github-summary";
        trip_argv[idx++] = summary;
    }
    trip_argv[idx] = NULL;
    if (summary_path_out) *summary_path_out = summary;
    return 0;
}

static int gate_run_pipeline(const char *const probe_argv[],
                             const char *const trip_argv[],
                             gate_format format) {
    int probe_pipe[2];
    int trip_pipe[2] = {-1, -1};
    pid_t probe_pid;
    pid_t trip_pid;
    int probe_rc;
    int trip_rc;
    int transform_rc = 0;

    if (pipe(probe_pipe) != 0) return GATE_EXIT_USAGE;
    if (format == GATE_FORMAT_JSONL && pipe(trip_pipe) != 0) {
        close(probe_pipe[0]);
        close(probe_pipe[1]);
        return GATE_EXIT_USAGE;
    }

    probe_pid = fork();
    if (probe_pid < 0) {
        close(probe_pipe[0]);
        close(probe_pipe[1]);
        if (trip_pipe[0] >= 0) close(trip_pipe[0]);
        if (trip_pipe[1] >= 0) close(trip_pipe[1]);
        return GATE_EXIT_USAGE;
    }
    if (probe_pid == 0) {
        close(probe_pipe[0]);
        if (dup2(probe_pipe[1], STDOUT_FILENO) < 0) _exit(127);
        close(probe_pipe[1]);
        if (trip_pipe[0] >= 0) close(trip_pipe[0]);
        if (trip_pipe[1] >= 0) close(trip_pipe[1]);
        gate_exec_child(probe_argv);
    }

    trip_pid = fork();
    if (trip_pid < 0) {
        close(probe_pipe[0]);
        close(probe_pipe[1]);
        if (trip_pipe[0] >= 0) close(trip_pipe[0]);
        if (trip_pipe[1] >= 0) close(trip_pipe[1]);
        (void)gate_wait_status(probe_pid);
        return GATE_EXIT_USAGE;
    }
    if (trip_pid == 0) {
        close(probe_pipe[1]);
        if (dup2(probe_pipe[0], STDIN_FILENO) < 0) _exit(127);
        close(probe_pipe[0]);
        if (format == GATE_FORMAT_JSONL) {
            close(trip_pipe[0]);
            if (dup2(trip_pipe[1], STDOUT_FILENO) < 0) _exit(127);
            close(trip_pipe[1]);
        }
        gate_exec_child(trip_argv);
    }

    close(probe_pipe[0]);
    close(probe_pipe[1]);
    if (format == GATE_FORMAT_JSONL) {
        close(trip_pipe[1]);
        transform_rc = gate_transform_trip_jsonl(trip_pipe[0]);
    }

    probe_rc = gate_wait_status(probe_pid);
    trip_rc = gate_wait_status(trip_pid);

    if (probe_rc != 0) {
        fprintf(stderr, "gate: probe failed\n");
        fputs("gate: command: ", stderr);
        gate_print_command(stderr, probe_argv);
        fprintf(stderr,
                "gate: try running bind emit watch and probe run manually\n");
        return GATE_EXIT_USAGE;
    }
    if (transform_rc != 0) return GATE_EXIT_USAGE;
    if (trip_rc == GATE_EXIT_CONTRACT) return GATE_EXIT_CONTRACT;
    if (trip_rc != 0) {
        fprintf(stderr, "gate: trip failed\n");
        gate_print_failed_command(trip_argv);
        return GATE_EXIT_USAGE;
    }
    return GATE_EXIT_OK;
}

static void gate_explain(const char *const atlas_check_argv[],
                         const char *const atlas_rules_argv[],
                         const char *const bind_argv[],
                         const char *const probe_argv[],
                         const char *const trip_argv[],
                         const gate_paths *paths,
                         const gate_watch_plan *plan) {
    fprintf(stderr, "gate plan:\n");
    fputs("  ", stderr);
    gate_print_command(stderr, atlas_check_argv);
    fputs("  ", stderr);
    gate_print_command(stderr, atlas_rules_argv);
    fprintf(stderr, "    > %s\n", paths->rules);
    fputs("  ", stderr);
    gate_print_command(stderr, bind_argv);
    fprintf(stderr, "    > %s\n", paths->watch);
    fprintf(stderr, "  watches=%lu\n", (unsigned long)plan->count);
    fputs("  ", stderr);
    gate_print_command(stderr, probe_argv);
    fputs("  ", stderr);
    gate_print_command(stderr, trip_argv);
}

int main(int argc, char **argv) {
    gate_options opt;
    gate_paths paths;
    gate_watch_plan plan;
    const char *atlas_check_argv[4];
    const char *atlas_rules_argv[5];
    const char *bind_argv[7];
    const char *probe_argv[GATE_MAX_ARGS];
    const char *trip_argv[16];
    const char *summary_path = NULL;
    int rc;

    if (gate_parse_args(argc, argv, &opt) != 0) {
        gate_usage(stderr);
        return GATE_EXIT_USAGE;
    }
    if (gate_init_paths(argv[0], &paths) != 0) {
        fprintf(stderr, "gate: could not create temporary files\n");
        return GATE_EXIT_USAGE;
    }

    atlas_check_argv[0] = paths.atlas;
    atlas_check_argv[1] = "check";
    atlas_check_argv[2] = opt.atlas_path;
    atlas_check_argv[3] = NULL;

    atlas_rules_argv[0] = paths.atlas;
    atlas_rules_argv[1] = "emit";
    atlas_rules_argv[2] = "rules";
    atlas_rules_argv[3] = opt.atlas_path;
    atlas_rules_argv[4] = NULL;

    bind_argv[0] = paths.bind;
    bind_argv[1] = "emit";
    bind_argv[2] = "watch";
    bind_argv[3] = opt.binary_path;
    bind_argv[4] = opt.atlas_path;
    bind_argv[5] = "--verify-types";
    bind_argv[6] = NULL;

    rc = gate_run_redirect(atlas_check_argv, GATE_STDOUT_STDERR, NULL);
    if (rc != 0) {
        fprintf(stderr, "gate: atlas failed\n");
        gate_print_failed_command(atlas_check_argv);
        gate_cleanup(&opt, &paths);
        return GATE_EXIT_USAGE;
    }
    rc = gate_run_redirect(atlas_rules_argv, GATE_STDOUT_FILE, paths.rules);
    if (rc != 0) {
        fprintf(stderr, "gate: atlas failed\n");
        gate_print_failed_command(atlas_rules_argv);
        gate_cleanup(&opt, &paths);
        return GATE_EXIT_USAGE;
    }
    if (access(opt.binary_path, R_OK) != 0) {
        fprintf(stderr,
                "gate: binary path not readable: %s\n",
                opt.binary_path);
        gate_cleanup(&opt, &paths);
        return GATE_EXIT_USAGE;
    }
    rc = gate_run_redirect(bind_argv, GATE_STDOUT_FILE, paths.watch);
    if (rc != 0) {
        fprintf(stderr, "gate: bind failed\n");
        gate_print_failed_command(bind_argv);
        fprintf(stderr,
                "gate: check symbols with: %s symbols ",
                paths.probe);
        gate_print_shell_arg(stderr, opt.binary_path);
        fputs(" --types\n", stderr);
        gate_cleanup(&opt, &paths);
        return GATE_EXIT_USAGE;
    }
    if (gate_load_watch_plan(paths.watch, &plan) != 0) {
        fprintf(stderr, "gate: bind produced no usable watch plan\n");
        gate_cleanup(&opt, &paths);
        return GATE_EXIT_USAGE;
    }
    if (gate_build_probe_argv(&opt,
                              &paths,
                              &plan,
                              probe_argv,
                              GATE_MAX_ARGS) != 0 ||
        gate_build_trip_argv(&opt,
                             &paths,
                             trip_argv,
                             16,
                             &summary_path) != 0) {
        gate_cleanup(&opt, &paths);
        return GATE_EXIT_USAGE;
    }
    if (opt.explain) {
        gate_explain(atlas_check_argv,
                     atlas_rules_argv,
                     bind_argv,
                     probe_argv,
                     trip_argv,
                     &paths,
                     &plan);
        if (summary_path && *summary_path) {
            fprintf(stderr, "  github-summary=%s\n", summary_path);
        }
    }

    rc = gate_run_pipeline(probe_argv, trip_argv, opt.format);
    gate_cleanup(&opt, &paths);
    return rc;
}
