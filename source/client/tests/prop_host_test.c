// Deterministic embedded-host frame checks, without opening listeners or sockets.
// Build with the ordinary client LIB except src/sst_host.c (included here).
#include "../src/sst_host.c"
#include <assert.h>

static const SstScene area={.stage="F001r",.room=1,.layer=1,.entrance=5};
static SstProp sample(void) {
    return (SstProp){.scene=area,.key={.placed_id=9,.profile=SST_PROP_PROFILE,.room=1,.layer=1},
        .sequence=1,.pose_tick=100,.phase=SST_PROP_CARRY,.matrix={1,0,0,0,0,1,0,0,0,0,1,0}};
}
static void setup(Host *h) {
    memset(h,0,sizeof *h);
    for(unsigned i=0;i<CONNECTIONS;i++)h->peer[i].fd=SST_INVALID_SOCKET;
    for(unsigned i=0;i<4;i++) {
        Peer *p=&h->peer[i];p->fd=1;p->id=i+1;p->scene=area;p->has_pose=true;
        p->last_pose=sst_now_ms();sst_udp_put32(p->pose,100);p->pose[30]=p->pose[32]=24;
        snprintf(p->team,sizeof p->team,"team");
    }
    h->peer[2].scene.room=2;
    snprintf(h->peer[3].team,sizeof h->peer[3].team,"other");
}
static void event(Host *h, const SstProp *p) {
    uint8_t body[SST_PROP_WIRE_MAX];size_t n=sst_prop_encode(p,body,sizeof body);assert(n);
    receive_frame(h,&h->peer[0],PROP,body,n);
}
int main(void) {
    Host *h=malloc(sizeof *h);assert(h);SstProp p=sample();setup(h);event(h,&p);
    Peer *to=&h->peer[1];assert(to->tx_n && to->tx[4]==PROP && rd16(to->tx+5)==1);
    SstProp decoded;assert(sst_prop_decode(to->tx+7,to->tx_n-7,&decoded));
    assert(decoded.phase==SST_PROP_CARRY && decoded.sequence==1);
    assert(!h->peer[2].tx_n && !h->peer[3].tx_n); // exact area and team only
    p.phase=SST_PROP_PLACE;p.sequence=2;to->tx_n=0;event(h,&p);
    assert(to->tx_n && sst_prop_decode(to->tx+7,to->tx_n-7,&decoded) && decoded.phase==SST_PROP_PLACE);
    for(unsigned i=0;i<sizeof h->flags;i++)assert(!h->flags[i]);
    for(unsigned i=0;i<SST_CHESTS;i++)assert(!h->chests[i]);

    setup(h);h->peer[0].has_pose=false;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].last_pose=sst_now_ms()-SST_PROP_FRESH_MS-1;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].last_pose=sst_now_ms()+10000;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].pose[30]=0;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].pose[34]=0x40;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].pose[34]=0x80;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].scene.room=2;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);p.pose_tick=146;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);p.pose_tick=54;event(h,&p);assert(!h->peer[1].tx_n);
    setup(h);p.pose_tick=101;event(h,&p);assert(h->peer[1].tx_n);

    setup(h);p=sample();for(unsigned i=0;i<30;i++){p.sequence++;event(h,&p);}
    size_t before=h->peer[1].tx_n;event(h,&p);assert(h->peer[1].tx_n==before);
    p.phase=SST_PROP_PLACE;p.sequence++;event(h,&p);assert(h->peer[1].tx_n>before);
    setup(h);uint8_t bad[]={0};receive_frame(h,&h->peer[0],PROP,bad,sizeof bad);
    assert(h->peer[0].closing && !h->peer[1].tx_n);
    setup(h);receive_frame(h,&h->peer[0],0x7f,bad,sizeof bad);assert(!h->peer[0].closing);
    setup(h);p=sample();p.key.profile=0x1f1;
    uint8_t all[SST_PROP_WIRE_MAX];size_t all_n=sst_prop_encode(&p,all,sizeof all);assert(all_n);
    receive_frame(h,&h->peer[0],PROPS_ALL,all,all_n);
    assert(h->peer[1].tx_n && h->peer[1].tx[4]==PROPS_ALL && !h->peer[0].closing);
    setup(h);receive_frame(h,&h->peer[0],PROP,all,all_n);
    assert(h->peer[0].closing && !h->peer[1].tx_n);
    free(h);
    puts("prop host PASS: validated optional TCP frames, authenticated sender, exact-area/team forwarding, live-pose/event/age/rate gates, no world-cache writes");
    return 0;
}
