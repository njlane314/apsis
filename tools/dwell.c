#include "apsis_dwell.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DWELL_VERSION "0.1.0"

static void dwell_usage(FILE *f) {
    fprintf(f,
        "usage: dwell --demo [-r rules.trip]\n"
        "\n"
        "Run a tiny in-process sampler demonstration. For production use,\n"
        "include <apsis.h>, register C variables with apsis_dwell_watch_*(),\n"
        "and call apsis_dwell_tick() at explicit sampling points.\n"
        "\n"
        "Options:\n"
        "  --demo                run the demonstration sampler\n"
        "  -r, --rules PATH      use rules file for the demonstration\n"
        "  -h, --help            show help\n"
        "  --version             show version\n");
}

static void dwell_put_event(const char *line, void *user) {
    (void)user;
    if (line) puts(line);
}

int main(int argc, char **argv) {
    const char *rules_path = NULL;
    int demo = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--demo") == 0) {
            demo = 1;
        } else if ((strcmp(argv[i], "-r") == 0 ||
                    strcmp(argv[i], "--rules") == 0) &&
                   i + 1 < argc) {
            rules_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            dwell_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("dwell " DWELL_VERSION);
            return 0;
        } else {
            fprintf(stderr, "dwell: unknown argument '%s'\n", argv[i]);
            dwell_usage(stderr);
            return 2;
        }
    }

    if (!demo) {
        dwell_usage(stdout);
        return 0;
    }

    {
        apsis_dwell_ctx ctx;
        volatile uint32_t queue_depth = 1201;
        char err[256];
        int emitted;

        apsis_dwell_init(&ctx);
        apsis_dwell_set_event_callback(&ctx, dwell_put_event, NULL);

        if (rules_path) {
            if (apsis_dwell_load_rules(&ctx, rules_path, err, sizeof(err)) != 0) {
                fprintf(stderr, "dwell: %s\n", err);
                return 2;
            }
        } else if (apsis_dwell_add_rule(&ctx,
                                      "worker.queue.depth",
                                      APSIS_GT,
                                      1000.0,
                                      APSIS_ERROR,
                                      "queue.backpressure") != 0) {
            fprintf(stderr, "dwell: failed to install demo rule\n");
            return 2;
        }

        if (apsis_dwell_watch_u32(&ctx,
                                "worker.queue.depth",
                                &queue_depth) != 0) {
            fprintf(stderr, "dwell: failed to watch demo variable\n");
            return 2;
        }

        emitted = apsis_dwell_tick(&ctx);
        if (emitted < 0) return 2;
        return emitted > 0 ? 1 : 0;
    }
}
