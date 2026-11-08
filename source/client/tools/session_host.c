// Asset-free launcher sidecar using the exact current native co-op host.
// Readiness is one JSON line; the owning launcher closes stdin or sends "quit".
#include "../src/sst_host.h"
#include "../src/sst_net.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fputs("Usage: sst-session-host --room SECRET [--port 0] [--bind 127.0.0.1]\n", stderr);
}
static const char *argument(const char *name, int *index, int argc, char **argv) {
    const char *arg = argv[*index]; size_t length = strlen(name);
    if (!strcmp(arg, name)) return *index + 1 < argc ? argv[++*index] : NULL;
    return !strncmp(arg, name, length) && arg[length] == '=' ? arg + length + 1 : NULL;
}
static bool room_valid(const char *room) {
    if (!room) return false;
    size_t length = strlen(room);
    if (!length || length >= SST_STR) return false;
    for (size_t i = 0; i < length; i++)
        if ((unsigned char)room[i] < 33 || (unsigned char)room[i] > 126) return false;
    return true;
}
static bool port_valid(const char *value, uint16_t *port) {
    if (!value || !*value || strlen(value) > 5) return false;
    for (const char *p = value; *p; p++) if (*p < '0' || *p > '9') return false;
    char *end; errno = 0; unsigned long parsed = strtoul(value, &end, 10);
    if (errno || *end || parsed > 65535) return false;
    *port = (uint16_t)parsed; return true;
}
static bool bind_loopback(void) {
#ifdef _WIN32
    return _putenv_s("SST_HOST_BIND", "127.0.0.1") == 0;
#else
    return setenv("SST_HOST_BIND", "127.0.0.1", 1) == 0;
#endif
}

int main(int argc, char **argv) {
    const char *room = NULL; uint16_t port = 0;
    bool have_port = false, have_bind = false;
    for (int i = 1; i < argc; i++) {
        const char *value;
        if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        if ((value = argument("--room", &i, argc, argv))) {
            if (room || !room_valid(value)) { usage(); return 2; }
            room = value;
        } else if ((value = argument("--port", &i, argc, argv))) {
            if (have_port || !port_valid(value, &port)) { usage(); return 2; }
            have_port = true;
        } else if ((value = argument("--bind", &i, argc, argv))) {
            if (have_bind || strcmp(value, "127.0.0.1")) { usage(); return 2; }
            have_bind = true;
        } else { usage(); return 2; }
    }
    if (!room_valid(room)) { usage(); return 2; }
    if (!bind_loopback()) { fputs("Could not configure the private listener.\n", stderr); return 1; }
    char why[160] = {0};
    if (!sst_host_start(port, room, why, sizeof why)) {
        fputs("Could not start the private session host.\n", stderr); return 1;
    }
    unsigned actual = sst_host_port();
    if (!actual || strcmp(sst_host_address(), "127.0.0.1")) {
        sst_host_stop(); fputs("Private listener validation failed.\n", stderr); return 1;
    }
    if (printf("{\"protocol\":%u,\"port\":%u}\n", (unsigned)SST_PROTO, actual) < 0 || fflush(stdout)) {
        sst_host_stop(); return 1;
    }
    char command[32];
    while (fgets(command, sizeof command, stdin)) {
        bool complete = strchr(command, '\n') != NULL;
        if (!complete && !feof(stdin)) {
            int ch; while ((ch = fgetc(stdin)) != '\n' && ch != EOF) {}
            continue;
        }
        command[strcspn(command, "\r\n")] = 0;
        if (!strcmp(command, "quit")) break;
    }
    sst_host_stop();
    return 0;
}
