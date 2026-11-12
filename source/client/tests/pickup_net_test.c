// Link the ordinary client LIB except sst_net.c, which is included here.
#include "../src/sst_net.c"
#include <assert.h>

static const SstScene area={.stage="F001r",.room=0,.layer=1};
static SstPickup sample(unsigned id,unsigned sequence) {
    return (SstPickup){.scene=area,.placed_id=id,.params=4|(255u<<10),.item=4,.sequence=sequence};
}
static void setup(void) {
    memset(players,0,sizeof players);pickup_n=prop_n=0;status=SST_ONLINE;my_id=1;
    atomic_store(&generation,42);have_scene=true;my_scene=area;my_scene.room=9;
    snprintf(cfg.team,sizeof cfg.team,"team");
    players[0]=(SstPlayer){.id=2,.active=true,.presence_generation=7,.scene=area};
    snprintf(players[0].team,sizeof players[0].team,"team");
}
static void incoming(SstPickupCommit c) {
    uint8_t bytes[SST_PICKUP_WIRE_MAX];size_t n=sst_pickup_commit_encode(&c,bytes,sizeof bytes);
    assert(n);on_frame(T_PICKUP,bytes,n);
}
static unsigned count;static SstPickupCommit delivered;
static void consume(const SstPickupCommit *c,void *user){(void)user;count++;delivered=*c;}
static void drain(unsigned expected){count=0;sst_net_poll_pickups(consume,NULL);assert(count==expected && !pickup_n);}
int main(void) {
    setup();SstPickupCommit c={.pickup=sample(1,1),.from=2,.epoch=3};
    incoming(c);assert(pickup_n==1);drain(1);
    assert(delivered.presence_generation==7 && delivered.received_ms && !delivered.snapshot);
    // Other-area gains need no local pose. Later source lifecycle cannot revoke a commit.
    incoming(c);players[0].active=false;drain(1);
    setup();incoming(c);Buf b={0};put16(&b,2);on_frame(T_PLAYER_LEAVE,b.b,b.n);drain(1);
    setup();incoming(c);b.n=0;put16(&b,2);putstr(&b,"Returned");putstr(&b,"team");
    on_frame(T_PLAYER_JOIN,b.b,b.n);drain(1);assert(delivered.presence_generation==7);
    setup();incoming(c);b.n=0;put16(&b,2);put_scene(&b,&area);on_frame(T_SCENE,b.b,b.n);
    assert(players[0].presence_generation!=7);drain(1);assert(delivered.presence_generation==7);
    setup();incoming(c);SstScene other=area;other.room=8;sst_net_send_scene(&other);drain(1);
    setup();incoming(c);in_pickups[0].received_ms=1;drain(1); // delay inventory grant safely
    setup();c.from=1;incoming(c);drain(1);assert(delivered.presence_generation==42);
    setup();c.from=99;incoming(c);drain(0);
    setup();c.from=2;snprintf(players[0].team,sizeof players[0].team,"other");incoming(c);drain(0);
    // Host-scoped snapshots/direct ACKs can name departed sources and old areas.
    setup();my_scene=area;c.from=99;c.snapshot=true;incoming(c);drain(1);assert(delivered.snapshot);
    setup();incoming(c);drain(1);assert(delivered.snapshot); // directed old-area ACK
    setup();my_scene=area;incoming(c);other=area;other.room=8;sst_net_send_scene(&other);drain(1);
    // Wrong direction cannot masquerade as a commit, nor replace an undelivered award.
    setup();SstPickup p=sample(8,9);uint8_t bytes[SST_PICKUP_WIRE_MAX];
    size_t n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(n);on_frame(T_PICKUP,bytes,n);drain(0);
    c=(SstPickupCommit){.pickup=sample(1,1),.from=2,.epoch=3};incoming(c);
    c.snapshot=true;incoming(c);assert(pickup_n==1 && !in_pickups[0].snapshot);drain(1);
    setup();for(unsigned i=0;i<PICKUP_QUEUE;i++){c.pickup=sample(i,i+1);c.snapshot=false;incoming(c);}
    assert(pickup_n==PICKUP_QUEUE);drain(PICKUP_QUEUE);
    setup();incoming(c);b.n=0;put16(&b,1);put8(&b,0);on_frame(T_WELCOME,b.b,b.n);drain(0);
    // Reliable request queue reports success to the source FIFO and never queues offline.
    setup();my_scene=area;sock=1;send_len=0;p=sample(8,9);
    assert(sst_net_send_pickup(&p) && sendbuf[4]==T_PICKUP);
    SstPickup decoded;assert(sst_pickup_decode(sendbuf+5,send_len-5,&decoded));
    status=SST_OFFLINE;size_t before=send_len;assert(!sst_net_send_pickup(&p) && send_len==before);
    sock=SST_INVALID_SOCKET;free(sendbuf);sendbuf=NULL;send_len=send_cap=0;
    puts("pickup transport PASS: direction, cross-area authority, collector ACK, retained commits, snapshot scope, bounded queue, reconnect reset");
    return 0;
}
