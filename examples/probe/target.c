#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Globals are intentionally non-static and volatile for the probe demo. */
volatile float imu_temperature_c = 72.0f;
volatile float battery_voltage = 12.4f;
volatile double control_loop_ms = 2.1;
volatile uint32_t dropped_packets = 0;

int main(void) {
    fprintf(stderr, "target pid=%ld\n", (long)getpid());
    fflush(stderr);

    for (int i = 0; i < 1000; ++i) {
        imu_temperature_c += 0.25f;
        battery_voltage -= 0.01f;
        control_loop_ms += 0.02;
        if (i % 17 == 0) dropped_packets++;

        struct timespec req;
        req.tv_sec = 0;
        req.tv_nsec = 50 * 1000 * 1000;
        nanosleep(&req, NULL);
    }

    return 0;
}
