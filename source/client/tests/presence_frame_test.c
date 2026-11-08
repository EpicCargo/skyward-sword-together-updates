// Actual receiver parser; synthetic state, no sockets, network thread or game.
#include "../src/sst_net.c"
#include <assert.h>
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(label, condition) do { checks++; if (!(condition)) { failures++; fprintf(stderr,"FAIL %s\n",label); } } while (0)
typedef struct {
    SstPlayer peers[SST_MAX_PLAYERS];
    SstProp queued;
    uint32_t presence, connection;
    uint16_t local;
    int props;
} Snapshot;
static void setup(void) {
    free(sendbuf);sendbuf=NULL;send_len=send_cap=0;
    memset(players,0,sizeof players);prop_n=1;out_n=0;pickup_n=0;
    memset(in_props,0,sizeof in_props);
    players[0]=(SstPlayer){.id=2,.active=true,.has_pose=true,.presence_generation=7,
        .scene={.stage="F001r",.room=1,.layer=1,.entrance=5},.pose={.tick=100,.health=24,.max_health=24},
        .pose_time_ms=sst_now_ms(),.udp_scene=17};
    strcpy(players[0].name,"Existing");strcpy(players[0].team,"team");
    in_props[0]=(SstProp){.from=2,.presence_generation=7,.sequence=51};
    presence_serial=10;atomic_store(&generation,3);my_id=1;
    status=SST_ONLINE;phase=SST_PHASE_ONLINE;failure=SST_FAIL_NONE;
    sock=udp_sock=SST_INVALID_SOCKET;cfg.use_udp=false;have_scene=false;
    strcpy(last_error,"Existing diagnostic");
}
static Snapshot snapshot(void) {
    Snapshot s={.presence=presence_serial,.connection=atomic_load(&generation),.local=my_id,.props=prop_n,.queued=in_props[0]};
    memcpy(s.peers,players,sizeof players);return s;
}
static bool unchanged(const Snapshot *s) {
    return !memcmp(s->peers,players,sizeof players) && s->presence==presence_serial &&
        s->connection==atomic_load(&generation) && s->local==my_id && s->props==prop_n &&
        !memcmp(&s->queued,&in_props[0],sizeof s->queued);
}
static void prefixes(uint8_t kind,const Buf *valid,const char *label) {
    for(size_t n=0;n<valid->n;n++) {
        setup();Snapshot before=snapshot();on_frame(kind,valid->b,n);
        CHECK(label,unchanged(&before) && status==SST_ONLINE);
    }
    Buf trailing=*valid;put8(&trailing,0x7f);
    setup();Snapshot before=snapshot();on_frame(kind,trailing.b,trailing.n);
    CHECK(label,unchanged(&before) && status==SST_ONLINE);
}
static void welcome(uint16_t assigned,uint16_t first,uint16_t second) {
    Buf b={0};put16(&b,assigned);put8(&b,2);
    put16(&b,first);putstr(&b,"one");putstr(&b,"team");putstr(&b,"F001r");put8(&b,1);
    put16(&b,second);putstr(&b,"two");putstr(&b,"team");putstr(&b,"F001r");put8(&b,1);
    Snapshot before=snapshot();on_frame(T_WELCOME,b.b,b.n);
    CHECK("invalid welcome identity leaves presence/props intact",unchanged(&before) && status==SST_FAILED);
}
int main(void) {
    Buf joined={0};put16(&joined,2);putstr(&joined,"Replacement");putstr(&joined,"team");
    prefixes(T_PLAYER_JOIN,&joined,"malformed JOIN cannot replace presence or discard queued props");
    setup();Snapshot before=snapshot();Buf unknown={0};put16(&unknown,77);put8(&unknown,20);
    on_frame(T_PLAYER_JOIN,unknown.b,unknown.n);CHECK("malformed unknown JOIN cannot allocate a peer",unchanged(&before));
    Buf left={0};put16(&left,2);prefixes(T_PLAYER_LEAVE,&left,"malformed LEAVE cannot retire a peer");
    Buf scene={0};put16(&scene,2);SstScene area={.stage="F002",.room=3,.layer=2,.entrance=9};put_scene(&scene,&area);
    prefixes(T_SCENE,&scene,"malformed SCENE cannot clear pose/UDP context or queued props");
    Buf error={0};putstr(&error,"Host rejected the room");
    prefixes(T_ERROR,&error,"malformed ERROR cannot fail a healthy connection");
    setup();welcome(1,0,3);setup();welcome(1,2,2);setup();welcome(1,1,2);

    setup();on_frame(T_PLAYER_JOIN,joined.b,joined.n);
    CHECK("valid JOIN replaces incarnation and drains only retired props",players[0].id==2 && players[0].active &&
          !players[0].has_pose && players[0].presence_generation>7 && !prop_n && !strcmp(players[0].name,"Replacement"));
    setup();on_frame(T_SCENE,scene.b,scene.n);
    CHECK("valid SCENE updates full area and clears old context",!players[0].has_pose && !players[0].udp_scene &&
          !prop_n && !strcmp(players[0].scene.stage,"F002") && players[0].scene.room==3 && players[0].scene.layer==2 && players[0].scene.entrance==9);
    setup();on_frame(T_PLAYER_LEAVE,left.b,left.n);CHECK("valid LEAVE retires peer",!players[0].active && !prop_n);
    setup();on_frame(T_ERROR,error.b,error.n);CHECK("valid ERROR remains terminal",status==SST_FAILED && !strcmp(last_error,"Host rejected the room"));
    setup();Buf maximum={0};put16(&maximum,65535);put8(&maximum,SST_MAX_PLAYERS);
    for(unsigned i=0;i<SST_MAX_PLAYERS;i++) {put16(&maximum,(uint16_t)(i+2));putstr(&maximum,"peer");putstr(&maximum,"team");putstr(&maximum,"");put8(&maximum,0);}
    on_frame(T_WELCOME,maximum.b,maximum.n);
    CHECK("legacy full receiver-cache welcome remains accepted",status==SST_ONLINE && my_id==65535 && atomic_load(&generation)==4);
    for(unsigned i=0;i<SST_MAX_PLAYERS;i++) CHECK("all valid welcome entries retained",players[i].active && players[i].id==i+2);
    free(sendbuf);sendbuf=NULL;send_len=send_cap=0;
    printf("presence-frame checks: %u checks, %u failures; scalar parser only\n",checks,failures);
    return failures ? 1 : 0;
}
