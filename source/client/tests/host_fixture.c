#include "../src/sst_host.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    char why[160];
    if (!sst_host_start(argc>=2 ? (uint16_t)atoi(argv[1]) : 0, argc>=3 ? argv[2] : "coop", why, sizeof why)) {
        fprintf(stderr, "%s\n", why);
        return 1;
    }
    printf("%u\n", sst_host_port());
    fflush(stdout);
    // Parent closes stdin to stop. Joining the worker exercises clean shutdown with live sockets.
    getchar();
    sst_host_stop();
    return 0;
}
