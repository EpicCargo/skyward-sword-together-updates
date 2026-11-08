// Link the ordinary client LIB except sst_host.c, which is included here.
#include "../src/sst_host.c"
#include <assert.h>

static const SstScene area={.stage="F001r",.room=0,.layer=1};
static SstPickup sample(unsigned id,unsigned sequence) {
    return (SstPickup){.scene=area,.placed_id=id,.params=4|(255u<<10),.item=4,.sequence=sequence};
}
static void setup(Host *h) {
    memset(h,0,sizeof *h);
    for(unsigned i=0;i<CONNECTIONS;i++)h->peer[i].fd=SST_INVALID_SOCKET;
    for(unsigned i=0;i<4;i++) {
        Peer *p=&h->peer[i];p->id=i+1;p->fd=1;p->scene=area;p->has_pose=true;p->last_pose=sst_now_ms();
        sst_udp_put32(p->pose,100);p->pose[30]=p->pose[32]=24;snprintf(p->team,sizeof p->team,"team");
    }
    h->peer[2].scene.room=9;snprintf(h->peer[3].team,sizeof h->peer[3].team,"other");
    assert(sst_pickup_host_begin(&h->pickups,&area));
}
static void clear_tx(Host *h){for(unsigned i=0;i<CONNECTIONS;i++)h->peer[i].tx_n=0;}
static void request(Host *h,unsigned peer,SstPickup p) {
    uint8_t bytes[SST_PICKUP_WIRE_MAX];size_t n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(n);
    receive_frame(h,&h->peer[peer],PICKUP,bytes,n);
}
static SstPickupCommit result(const Peer *p) {
    SstPickupCommit c;assert(p->tx_n>=5 && p->tx[4]==PICKUP);
    assert(sst_pickup_commit_decode(p->tx+5,rd32(p->tx)-1,&c));return c;
}
int main(void) {
    Host *h=malloc(sizeof *h);assert(h);setup(h);SstPickup p=sample(1,1);request(h,0,p);
    assert(h->peer[0].tx_n && h->peer[1].tx_n && h->peer[2].tx_n && !h->peer[3].tx_n);
    SstPickupCommit c=result(&h->peer[0]);assert(c.from==1 && c.epoch && !c.snapshot);
    uint32_t epoch=c.epoch;
    // Exact retries and competing placed claims return no-award receipts only.
    clear_tx(h);request(h,0,p);assert(result(&h->peer[0]).snapshot && !h->peer[1].tx_n);
    clear_tx(h);request(h,1,p);c=result(&h->peer[1]);assert(c.snapshot && c.from==1 && !h->peer[0].tx_n);
    clear_tx(h);p=sample(2,4);request(h,0,p);assert(!result(&h->peer[0]).snapshot);
    clear_tx(h);request(h,0,sample(3,3));assert(!h->peer[0].tx_n); // stale monotonic sequence
    request(h,0,sample(3,4));assert(!h->peer[0].tx_n); // equal sequence, different claim
    // New SCENE receives snapshots only; existing occupants retain the area epoch.
    clear_tx(h);Packet b={0};str(&b,area.stage);u8(&b,area.room);u8(&b,area.layer);u8(&b,0);
    receive_frame(h,&h->peer[2],SCENE,b.b,b.n);c=result(&h->peer[2]);assert(c.snapshot && c.epoch==epoch);
    // Source collection must flush after events and requires a fresh native-safe pose.
    setup(h);h->peer[0].pose[34]=0x40;request(h,0,sample(1,1));assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].pose[34]=0x80;request(h,0,sample(1,1));assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].last_pose=sst_now_ms()-5001;request(h,0,sample(1,1));assert(!h->peer[1].tx_n);
    setup(h);h->peer[0].scene.room=8;request(h,0,sample(1,1));assert(!h->peer[1].tx_n);
    // A client cannot manufacture the server's commit direction.
    setup(h);c=(SstPickupCommit){.pickup=sample(1,1),.from=1,.epoch=1};
    uint8_t bytes[SST_PICKUP_WIRE_MAX];size_t n=sst_pickup_commit_encode(&c,bytes,sizeof bytes);assert(n);
    receive_frame(h,&h->peer[0],PICKUP,bytes,n);assert(h->peer[0].closing && !h->peer[1].tx_n);
    // Empty occupied-area set expires old claims; the next visit has a fresh epoch.
    setup(h);request(h,0,sample(1,1));epoch=result(&h->peer[0]).epoch;
    for(unsigned i=0;i<4;i++)h->peer[i].scene.room=8;
    pickup_prune(h);assert(sst_pickup_host_begin(&h->pickups,&area)!=epoch);
    free(h);puts("pickup host PASS: authenticated claims, collector ACK, cross-area team gain, no-award retries/snapshots, sequence and area epochs");
    return 0;
}
