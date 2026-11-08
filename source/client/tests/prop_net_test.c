// Deterministic transport queue checks, without sockets or a network thread.
// Build with the ordinary client LIB except src/sst_net.c (included here).
#include "../src/sst_net.c"
#include <assert.h>

static const SstScene area = {.stage="F001r",.room=1,.layer=1,.entrance=5};
static SstProp sample(unsigned key, unsigned sequence, SstPropPhase phase) {
    return (SstProp){.scene=area,.key={.placed_id=key,.profile=SST_PROP_PROFILE,.room=1,.layer=1},
        .sequence=sequence,.pose_tick=100,.phase=phase,.matrix={1,0,0,0,0,1,0,0,0,0,1,0}};
}
static void source(void) {
    memset(players,0,sizeof players);
    players[0]=(SstPlayer){.active=true,.id=2,.presence_generation=7,.scene=area,.has_pose=true,
        .pose={.tick=100,.health=24,.max_health=24},.pose_time_ms=sst_now_ms()};
    snprintf(players[0].team,sizeof players[0].team,"team");
    my_scene=area;have_scene=true;status=SST_ONLINE;
    snprintf(cfg.team,sizeof cfg.team,"team");
    prop_n=0;
}
static void incoming(const SstProp *p, uint16_t from) {
    Buf b={0};put16(&b,from);
    size_t n=sst_prop_encode(p,b.b+b.n,sizeof b.b-b.n);assert(n);
    b.n+=n;on_frame(T_PROP,b.b,b.n);
}
static unsigned deliveries;
static SstProp delivered;
static void consume(const SstProp *p, void *user) {
    (void)user;deliveries++;delivered=*p;
}
static void no_delivery(void) {
    deliveries=0;sst_net_poll_props(consume,NULL);assert(!deliveries);
}
int main(void) {
    source();SstProp p=sample(9,1,SST_PROP_CARRY);
    incoming(&p,2);assert(prop_n==1);
    p.sequence=2;p.phase=SST_PROP_PLACE;incoming(&p,2);
    assert(prop_n==1 && in_props[0].phase==SST_PROP_PLACE && in_props[0].sequence==2);
    p.sequence=1;p.phase=SST_PROP_CARRY;incoming(&p,2);
    assert(prop_n==1 && in_props[0].phase==SST_PROP_PLACE); // stale carry cannot undo placement
    deliveries=0;sst_net_poll_props(consume,NULL);
    assert(deliveries==1 && delivered.from==2 && delivered.presence_generation==7 && delivered.received_ms);
    assert(delivered.phase==SST_PROP_PLACE && !prop_n);

    // Every key component and authenticated sender remains separate.
    p=sample(9,1,SST_PROP_CARRY);incoming(&p,2);
    p.key.params=0x10000;incoming(&p,2);assert(prop_n==2);
    players[1]=players[0];players[1].id=3;players[1].presence_generation=8;
    incoming(&p,3);assert(prop_n==3);
    source();for(unsigned i=0;i<PROP_QUEUE;i++){p=sample(i,1,SST_PROP_CARRY);incoming(&p,2);}
    assert(prop_n==PROP_QUEUE);
    p=sample(500,1,SST_PROP_CARRY);incoming(&p,2);assert(prop_n==PROP_QUEUE);
    p=sample(77,2,SST_PROP_PLACE);incoming(&p,2);
    assert(prop_n==PROP_QUEUE && in_props[77].phase==SST_PROP_PLACE);
    deliveries=0;sst_net_poll_props(consume,NULL);assert(deliveries==PROP_QUEUE);

    // Unknown identities, other areas/teams, dead/event/boss and stale poses fail closed.
    source();p=sample(9,1,SST_PROP_CARRY);
    incoming(&p,99);assert(!prop_n);
    p.scene.room=p.key.room=2;incoming(&p,2);assert(!prop_n);p=sample(9,1,SST_PROP_CARRY);
    snprintf(players[0].team,sizeof players[0].team,"other");incoming(&p,2);assert(!prop_n);
    source();players[0].pose.health=0;incoming(&p,2);assert(!prop_n);
    source();players[0].pose.equip_flags=0x40;incoming(&p,2);assert(!prop_n);
    source();players[0].pose.equip_flags=0x80;incoming(&p,2);assert(!prop_n);
    source();players[0].pose_time_ms=sst_now_ms()-SST_PROP_FRESH_MS-1;incoming(&p,2);assert(!prop_n);
    source();players[0].pose_time_ms=sst_now_ms()+10000;incoming(&p,2);assert(!prop_n);
    source();p.pose_tick=100+46;incoming(&p,2);assert(!prop_n);
    p.pose_tick=100-46;incoming(&p,2);assert(!prop_n);
    p.pose_tick=101;incoming(&p,2);assert(prop_n==1); // one newer UDP pose can still be in flight

    // Revalidate at drain, rather than delivering an event whose owner just left/reloaded.
    source();p=sample(9,1,SST_PROP_CARRY);incoming(&p,2);
    in_props[0].received_ms=sst_now_ms()-SST_PROP_FRESH_MS-1;no_delivery();
    source();incoming(&p,2);in_props[0].received_ms=sst_now_ms()+10000;no_delivery();
    source();incoming(&p,2);players[0].presence_generation++;no_delivery();
    source();incoming(&p,2);players[0].active=false;no_delivery();
    source();incoming(&p,2);players[0].scene.layer++;no_delivery();
    source();incoming(&p,2);players[0].pose.equip_flags=0x40;no_delivery();
    source();incoming(&p,2);sst_net_send_scene(&area);assert(!prop_n);
    source();incoming(&p,2);Buf b={0};put16(&b,2);on_frame(T_PLAYER_LEAVE,b.b,b.n);assert(!prop_n);
    source();incoming(&p,2);b.n=0;put16(&b,2);put_scene(&b,&area);on_frame(T_SCENE,b.b,b.n);assert(!prop_n);
    source();incoming(&p,2);b.n=0;put16(&b,2);putstr(&b,"Returned");putstr(&b,"team");
    on_frame(T_PLAYER_JOIN,b.b,b.n);assert(!prop_n && !players[0].has_pose);
    source();incoming(&p,2);b.n=0;put16(&b,1);put8(&b,0);on_frame(T_WELCOME,b.b,b.n);assert(!prop_n);

    // Generalized peers use0x34; the legacy vase extension cannot carry new profiles.
    source();p=sample(9,1,SST_PROP_CARRY);p.key.profile=0x1f1;
    incoming(&p,2);assert(!prop_n);
    b.n=0;put16(&b,2);b.n+=sst_prop_encode(&p,b.b+b.n,sizeof b.b-b.n);
    on_frame(T_PROPS_ALL,b.b,b.n);assert(prop_n==1);
    deliveries=0;sst_net_poll_props(consume,NULL);assert(deliveries==1 && delivered.key.profile==0x1f1);

    // CARRY uses only TCP even with UDP enabled; PLACE has an independent rate/queue reserve.
    source();sock=1;udp_active=true;udp_scene=3;udp_clear_live();send_len=0;
    prop_rate_at=sst_now_ms();prop_carries=prop_places=0;
    for(unsigned i=0;i<30;i++){p=sample(9,i+1,SST_PROP_CARRY);sst_net_send_prop(&p);}
    size_t carried=send_len;assert(carried && prop_carries==30 && !udp_out_n && sendbuf[4]==T_PROPS_ALL);
    p.sequence++;sst_net_send_prop(&p);assert(send_len==carried);
    p.phase=SST_PROP_PLACE;sst_net_send_prop(&p);assert(send_len>carried && prop_places==1);
    send_len=0;status=SST_OFFLINE;sst_net_send_prop(&p);assert(!send_len);
    sock=SST_INVALID_SOCKET;udp_active=false;free(sendbuf);sendbuf=NULL;send_cap=0;
    puts("prop transport PASS: reliable optional frames, exact-key coalescing, placement preservation, bounded queues/rates, freshness and lifecycle gates");
    return 0;
}
