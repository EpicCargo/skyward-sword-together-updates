// Protocol/heartbeat check without game files or a native game instance.
#include "../src/sst_game.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int main(int argc, char **argv) {
    bool check_udp=argc>1 && !strcmp(argv[argc-1],"--udp");
    if (check_udp) argc--;
    if (argc < 2 || argc > 4) {
        fprintf(stderr,"Usage: sst-network-check HOST:PORT [ROOM] [TEAM] [--udp]\n");
        return 2;
    }
    SstConfig config={.auto_reconnect=false,.use_udp=check_udp};
    if (!sst_parse_server(argv[1],config.host,sizeof config.host,&config.port)) {
        fprintf(stderr,"Enter the host IPv4 address or hostname, followed by :7623.\n");
        return 2;
    }
    snprintf(config.name,sizeof config.name,"Connection check");
    if (argc > 2) snprintf(config.room,sizeof config.room,"%s",argv[2]);
    if (argc > 3) snprintf(config.team,sizeof config.team,"%s",argv[3]);
    printf("Checking %s:%u using co-op protocol %u...\n",config.host,config.port,SST_PROTO);
    fflush(stdout);
    sst_net_start(&config);
    if (check_udp) { SstScene scene={0}; sst_net_send_scene(&scene); }
    uint64_t deadline=sst_now_ms()+12000;
    uint64_t tcp_ready_at=0;
    char why[160]={0};
    while (sst_now_ms() < deadline) {
        SstStatus status=sst_net_status(why,sizeof why);
        SstConnectionStats stats=sst_net_stats();
        if (status==SST_ONLINE && stats.has_rtt) {
            if (!tcp_ready_at) tcp_ready_at=sst_now_ms();
            if (check_udp && !stats.udp_active && sst_now_ms()-tcp_ready_at<2500) goto wait;
            printf("PASS: host accepted this mod protocol, room and team. Round trip: %u ms.\n",stats.rtt_ms);
            if (check_udp) puts(stats.udp_active ? "UDP PASS: movement datagrams travel both ways." :
                "UDP unavailable: movement will use TCP fallback. Check the host UDP port/firewall if desired.");
            sst_net_stop();
            return 0;
        }
        if (status==SST_FAILED || status==SST_OFFLINE) break;
wait:
#ifdef _WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
    }
    fprintf(stderr,"FAIL: %s\n",why[0]?why:"host did not complete the handshake and heartbeat in time");
    sst_net_stop();
    return 1;
}
