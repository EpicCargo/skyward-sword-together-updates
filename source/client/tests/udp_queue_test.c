// Deterministic saturation check without a timing race against the network thread.
#include "../src/sst_net.c"
#include <assert.h>

static void paced_tick(uint64_t now) {
    udp_probe_at=udp_ack_at=now;
    udp_budget=(SstUdpBudget){.at=now,.credit=SST_UDP_HEADER+4+36,.rate=6000};
    udp_tick(now);
}
int main(void) {
    udp_active=true; udp_scene=1;
    Buf body={.n=36};
    for (unsigned i=0;i<SST_ENEMIES+100;i++) queue_live(T_MOTION,&body,i+1);
    queue_live(T_POSE,&body,0);
    unsigned poses=0, motions=0;
    for (unsigned i=0;i<udp_out_n;i++) { poses+=udp_out[i].type==T_POSE; motions+=udp_out[i].type==T_MOTION; }
    assert(poses==1 && motions==SST_ENEMIES && udp_out_n==SST_ENEMIES+1);
    for (unsigned i=0;i<SST_ENEMIES;i++) assert(udp_out[i].key==i+1);
    // Latest avatar and actor samples still replace existing entries in a saturated queue.
    body.b[0]=42; queue_live(T_POSE,&body,0); queue_live(T_MOTION,&body,1);
    queue_live(T_MOTION,&body,SST_ENEMIES);
    assert(udp_out_n==SST_ENEMIES+1 && udp_out[SST_ENEMIES].body[0]==42 && udp_out[0].body[0]==42);
    assert(udp_out[SST_ENEMIES-1].body[0]==42);
    udp_scene_request(); assert(!udp_out_n && !udp_scene);
    for (unsigned i=0;i<SST_ENEMIES+1;i++) assert(!udp_out[i].type);
    assert(sst_socket_init());
    SstSocket receiver=sst_socket_open(AF_INET,SOCK_DGRAM,0);
    assert(receiver!=SST_INVALID_SOCKET);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!sst_socket_bind(receiver,(void *)&address,sizeof address));
    SstSocklen size=sizeof address;
    assert(!sst_socket_name(receiver,(void *)&address,&size));
    udp_sock=sst_socket_open(AF_INET,SOCK_DGRAM,0);
    assert(udp_sock!=SST_INVALID_SOCKET && !sst_socket_connect(udp_sock,(void *)&address,size));
    udp_offered=udp_active=true; udp_scene=1;
    // One datagram of budget per tick, with every actor refreshed in a fixed order.
    // Retention plus a cursor advanced by real sends must reach the last actor too.
    for (unsigned tick=0;tick<SST_ENEMIES;tick++) {
        for (unsigned i=0;i<SST_ENEMIES;i++) {
            body.b[0]=(uint8_t)(i+1); body.b[1]=(uint8_t)((i+1)>>8);
            queue_live(T_MOTION,&body,i+1);
        }
        paced_tick(sst_now_ms());
        assert(udp_out_n==SST_ENEMIES-1); // other fresh updates survive the budget shortage
        SstPoll poller={.fd=receiver,.events=POLLIN};
        assert(sst_socket_poll(&poller,1,100)>0);
        uint8_t packet[SST_UDP_MAX];
        assert(sst_socket_recv(receiver,packet,sizeof packet)==SST_UDP_HEADER+4+36);
        assert(packet[24]==T_MOTION && (unsigned)(packet[33]|packet[34]<<8)==tick+1);
    }
    // Disappeared actors must not leave obsolete samples waiting behind the budget.
    for (unsigned i=0;i<SST_ENEMIES+1;i++) if (udp_out[i].type)
        udp_out[i].updated=(uint32_t)sst_now_ms()-SST_MOTION_TIMEOUT_MS-1;
    paced_tick(sst_now_ms()); assert(!udp_out_n);
    unsigned cursor=udp_cursor; paced_tick(sst_now_ms()); assert(udp_cursor==cursor);
    // The producer can enqueue just after the network thread sampled its tick time.
    // A one-millisecond future timestamp is fresh, including across the 32-bit wrap.
    queue_live(T_MOTION,&body,1);
    for (unsigned i=0;i<SST_ENEMIES+1;i++) if (udp_out[i].type) udp_out[i].updated=0;
    uint64_t before_wrap=UINT32_MAX;
    udp_probe_at=udp_ack_at=before_wrap;
    udp_budget=(SstUdpBudget){.at=before_wrap,.rate=6000};
    udp_tick(before_wrap); assert(udp_out_n==1);
    udp_scene_request(); assert(!udp_out_n);
    sst_socket_close(receiver); sst_socket_close(udp_sock); udp_sock=SST_INVALID_SOCKET;
    puts("UDP saturation PASS: reserved avatar, 256 actors, fresh unsent updates retained, paced round-robin fairness, stale expiry, scene reset");
    return 0;
}
