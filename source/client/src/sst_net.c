// Skyward Sword Together network client. See sst_net.h and docs/protocol.md.
#include "sst_net.h"
#include "sst_socket.h"
#include "sst_enemy_motion.h"
#include "sst_world.h"
#include "sst_udp.h"
#include "sst_props.h"
#include "sst_pickup.h"
#include "sst_debug.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    T_HELLO = 0x01, T_WELCOME = 0x02, T_PLAYER_JOIN = 0x03, T_PLAYER_LEAVE = 0x04, T_ERROR = 0x05,
    T_PING = 0x06, T_PONG = 0x07, T_SCENE = 0x10, T_POSE = 0x11, T_FLAG = 0x20, T_WORLD_REQUEST = 0x21,
    T_RESYNC = 0x22, T_CHEST = 0x23, T_WORLD_DONE = 0x24, T_ENEMY = 0x30, T_MOTION = 0x31, T_PROP = 0x32,
    T_PICKUP = 0x33, T_PROPS_ALL = 0x34,
};
#define MAX_FRAME 65536
#define FLAG_QUEUE 4096
#define FLAG_SLOTS (FLAG_QUEUE * 2)

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static atomic_bool running;
static SstConfig cfg;
static atomic_int status = SST_OFFLINE;
static atomic_uint generation;
static char last_error[160];
static uint16_t my_id;
static uint8_t session[16];
static SstSocket sock = SST_INVALID_SOCKET;
static SstSocket udp_sock = SST_INVALID_SOCKET;
static uint8_t udp_token[16];
static uint32_t udp_tx_seq, udp_rx_seq, udp_ack_seq, udp_cookie, udp_scene, udp_probe;
static uint64_t udp_ack_at, udp_probe_at;
static uint64_t udp_probe_times[4];
static bool udp_offered, udp_active, udp_probe_pending;
static unsigned udp_sent, udp_received, udp_fallbacks;
static SstUdpBudget udp_budget;
typedef struct { uint8_t type, n; uint32_t updated; uint64_t key; uint8_t body[128]; } Live;
static Live udp_out[SST_ENEMIES + 1]; // one pose + the bridge's bounded actor set, never offline
static unsigned udp_out_n;
static unsigned udp_cursor;
static uint64_t udp_pose_at;
static SstPlayer players[SST_MAX_PLAYERS];
static uint32_t presence_serial;
static bool have_scene;
static SstScene my_scene;

// outgoing reliable flags (kept until written to a live socket) and incoming flags
typedef struct { uint8_t kind, value; uint16_t index, from; } Flag;
static Flag out_flags[FLAG_QUEUE], in_flags[FLAG_QUEUE];
static int out_n, in_n;
// A repeated snapshot occupies one entry per logical flag, rather than one per packet.
static uint16_t out_flag_slots[FLAG_SLOTS], in_flag_slots[FLAG_SLOTS];
static bool recovery_needed;
static uint64_t recovery_at;
static unsigned queue_recoveries;
static SstEnemyEvent in_enemies[512];
static int enemy_n;
static SstEnemyMotion in_motion[512];
static int motion_n;
#define PROP_QUEUE 128
#define PROP_POSE_TICKS 45 // 1.5 s of the 30 Hz pose source; tolerate TCP/UDP reordering
static SstProp in_props[PROP_QUEUE];
static int prop_n;
static uint64_t prop_rate_at;
static unsigned prop_carries, prop_places;
#define PICKUP_QUEUE 256
static SstPickupCommit in_pickups[PICKUP_QUEUE];
static unsigned pickup_n;
static SstChestReward in_chests[SST_CHESTS];
static int chest_n;
static bool world_request;
static uint32_t refresh_serial, refresh_active, refresh_done;
static bool ping_pending, have_rtt;
static uint32_t ping_token;
static uint64_t ping_sent_at, packet_at;
static unsigned round_trip_ms;
static atomic_int phase = SST_PHASE_IDLE;
static SstNetFailure failure;
static unsigned attempts, consecutive_failures, handshakes;
static uint64_t retry_at, handshake_at;
static unsigned connect_timeout_ms = 5000, handshake_timeout_ms = 10000;
static unsigned udp_probes_sent, udp_probes_acked;
static bool udp_probe_acked[4];
static unsigned enemy_rx_n, motion_rx_n;
static uint64_t enemy_rx_at, motion_rx_at;

// send buffer: written by any thread, drained by the network thread
static uint8_t *sendbuf;
static size_t send_len, send_cap;

uint64_t sst_now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static void set_error(SstNetFailure kind, const char *fmt, ...) {
    pthread_mutex_lock(&mu);
    va_list ap; va_start(ap, fmt);
    vsnprintf(last_error, sizeof last_error, fmt, ap);
    va_end(ap);
    failure = kind;
    pthread_mutex_unlock(&mu);
    // Exportable ring: omit endpoint, room, player name and received free text.
    sst_debug_log(SST_DBG_NET,"connection failure class %u",(unsigned)kind);
}
static SstNetFailure connect_failure(int code) {
    switch(code) {
    case ECONNREFUSED:return SST_FAIL_CONNECT_REFUSED;
    case ENETUNREACH:case EHOSTUNREACH:return SST_FAIL_CONNECT_UNREACHABLE;
    case ETIMEDOUT:return SST_FAIL_CONNECT_TIMEOUT;
    default:return SST_FAIL_CONNECT_OTHER;
    }
}
static unsigned env_ms(const char *name,unsigned fallback) {
    const char *value=getenv(name);if(!value || !*value)return fallback;
    char *end;errno=0;long ms=strtol(value,&end,10);
    return !errno && end!=value && !*end && ms>=500 && ms<=60000?(unsigned)ms:fallback;
}

// ---- encoding
typedef struct { uint8_t b[512]; size_t n; } Buf;
static void put8(Buf *b, uint8_t v) { if (b->n < sizeof b->b) b->b[b->n++] = v; }
static void put16(Buf *b, uint16_t v) { put8(b, v & 0xff); put8(b, v >> 8); }
static void put32(Buf *b, uint32_t v) { put16(b, v & 0xffff); put16(b, v >> 16); }
static void putf(Buf *b, float f) { uint32_t v; memcpy(&v, &f, 4); put32(b, v); }
static void putstr(Buf *b, const char *s) {
    size_t n = strnlen(s, 255);
    put8(b, (uint8_t)n);
    for (size_t i = 0; i < n; i++) put8(b, (uint8_t)s[i]);
}

typedef struct { const uint8_t *p; size_t n, o; bool bad; } Rd;
static uint8_t get8(Rd *r) { if (r->o + 1 > r->n) { r->bad = true; return 0; } return r->p[r->o++]; }
static uint16_t get16(Rd *r) { uint16_t lo = get8(r); return lo | (uint16_t)get8(r) << 8; }
static uint32_t get32(Rd *r) { uint32_t lo = get16(r); return lo | (uint32_t)get16(r) << 16; }
static float getf(Rd *r) { uint32_t v = get32(r); float f; memcpy(&f, &v, 4); return f; }
static void getstr(Rd *r, char *out, size_t cap) {
    size_t n = get8(r), k = 0;
    for (size_t i = 0; i < n; i++) { uint8_t c = get8(r); if (k + 1 < cap) out[k++] = (char)c; }
    out[k] = 0;
}

// Queue a frame for the network thread. Caller holds mu.
static bool queue_frame(uint8_t type, const Buf *body) {
    if(!body || body->n>sizeof body->b || send_len>(4u<<20)-5-body->n)return false;
    size_t need = send_len + 5 + body->n;
    if (need > send_cap) {
        size_t capacity=need*2;
        uint8_t *next=realloc(sendbuf,capacity);
        if(!next) {
            // Caller owns mu: never recurse through set_error or discard queued bytes.
            failure=SST_FAIL_SOCKET;
            snprintf(last_error,sizeof last_error,"Network send queue allocation failed");
            sst_debug_log(SST_DBG_NET,"send queue allocation failed; existing bytes retained");
            return false;
        }
        send_cap=capacity;sendbuf=next;
    }
    uint32_t len = (uint32_t)body->n + 1;
    memcpy(sendbuf + send_len, &len, 4);
    sendbuf[send_len + 4] = type;
    memcpy(sendbuf + send_len + 5, body->b, body->n);
    send_len = need;
    return true;
}

static void udp_clear_live(void) {
    memset(udp_out,0,sizeof udp_out); udp_out_n=0; udp_cursor=0;
}
static void udp_scene_request(void) {
    udp_scene=0; udp_clear_live();
    if (!++udp_cookie) ++udp_cookie;
    if (cfg.use_udp) { Buf b={0}; put32(&b,udp_cookie); queue_frame(SST_UDP_SCENE,&b); }
}
static void udp_ready(bool ready) {
    if (udp_active && !ready) udp_fallbacks++;
    if (udp_active!=ready && getenv("SST_TRACE")) fprintf(stderr,"[sst] Movement transport: %s\n",ready ? "UDP" : "TCP fallback");
    if(udp_active!=ready)sst_debug_log(SST_DBG_NET,"movement transport: %s",ready?"UDP":"TCP fallback");
    udp_active=ready;
    Buf b={0}; put32(&b,ready ? udp_probe : 0); queue_frame(SST_UDP_READY,&b);
}
// Caller holds mu. Only the network thread touches the datagram socket.
static void queue_live(uint8_t type, const Buf *body, uint64_t key) {
    if (!udp_active || !udp_scene) { queue_frame(type,body); return; }
    unsigned slot=SST_ENEMIES+1;
    unsigned motions=0;
    for (unsigned i=0;i<SST_ENEMIES+1;i++) {
        motions+=udp_out[i].type==T_MOTION;
        if (!udp_out[i].type && slot==SST_ENEMIES+1) slot=i;
        if (udp_out[i].type==type && udp_out[i].key==key) { slot=i; break; }
    }
    if (slot>=SST_ENEMIES+1 || body->n>sizeof udp_out[slot].body) return;
    Live *live=&udp_out[slot];
    if (!live->type && type==T_MOTION && motions>=SST_ENEMIES) return; // reserve the avatar's slot
    if (!live->type) udp_out_n++;
    live->type=type; live->key=key; live->n=(uint8_t)body->n; live->updated=(uint32_t)sst_now_ms();
    memcpy(live->body,body->b,body->n);
}
static bool udp_write(uint8_t type, const uint8_t *body, size_t size) {
    uint8_t out[SST_UDP_MAX];
    size_t n=sst_udp_pack(out,udp_token,++udp_tx_seq,type,body,size);
    if (sst_socket_send(udp_sock,out,n)!=(ptrdiff_t)n) return false;
    udp_sent++; return true;
}
static void udp_tick(uint64_t now) {
    pthread_mutex_lock(&mu);
    if (udp_offered && udp_sock!=SST_INVALID_SOCKET) {
        if (udp_active && now-udp_ack_at>=SST_UDP_TIMEOUT) udp_ready(false);
        if (now-udp_probe_at>=SST_UDP_PROBE_MS) {
            if (udp_probe_pending) sst_udp_feedback(&udp_budget,false);
            udp_probe_pending=true; udp_probe_at=now;
            if (!++udp_probe) ++udp_probe;
            udp_probes_sent++;udp_probe_acked[udp_probe%4]=false;
            udp_probe_times[udp_probe%4]=now;
            uint8_t b[4]; sst_udp_put32(b,udp_probe); udp_write(0,b,sizeof b);
        }
        // Keep unsent samples through a budget shortage. Rotate after actual sends,
        // independently of empty network polls or the producer's snapshot interval.
        // New samples replace pending data; actors no longer observed expire in 500 ms.
        for (unsigned pass=0;pass<2 && udp_out_n;pass++) {
          unsigned start=pass ? udp_cursor : 0;
          for (unsigned i=0;i<SST_ENEMIES+1;i++) {
            unsigned slot=(i+start)%(SST_ENEMIES+1);
            Live *live=&udp_out[slot];
            if (!live->type) continue;
            if ((int32_t)((uint32_t)now-live->updated)>SST_MOTION_TIMEOUT_MS) {
                live->type=0; udp_out_n--; continue;
            }
            if ((live->type==T_POSE)!=(pass==0)) continue;
            if (udp_active && live->type==T_POSE && udp_budget.rate<=3000 && now-udp_pose_at<100) continue;
            bool sent=false;
            if (!udp_active || !udp_scene) {
                Buf b={0}; memcpy(b.b,live->body,live->n); b.n=live->n;
                if (send_len<64*1024) { queue_frame(live->type,&b); sent=true; }
            } else if (sst_udp_spend(&udp_budget,now,SST_UDP_HEADER+4+live->n)) {
                uint8_t body[132]; sst_udp_put32(body,udp_scene); memcpy(body+4,live->body,live->n);
                sent=udp_write(live->type,body,live->n+4);
                if (sent && live->type==T_POSE) udp_pose_at=now;
            }
            if (sent) {
                if (live->type==T_MOTION) udp_cursor=(slot+1)%(SST_ENEMIES+1);
                live->type=0; udp_out_n--;
            }
          }
        }
    }
    pthread_mutex_unlock(&mu);
}

// Flag values are OR-merged by the hosts too. Keep the latest source attribution.
// The fixed hash tables add 32 KiB for both queues and never allocate per packet.
static bool queue_flag(Flag *flags, int *count, uint16_t *slots, Flag flag) {
    uint32_t key = (uint32_t)flag.kind * 65536 + flag.index;
    unsigned slot = (key * 2654435761u) >> 19;
    while (slots[slot]) {
        Flag *old = &flags[slots[slot] - 1];
        if (old->kind == flag.kind && old->index == flag.index) {
            old->value |= flag.value; old->from = flag.from; return true;
        }
        slot = (slot + 1) & (FLAG_SLOTS - 1);
    }
    if (*count == FLAG_QUEUE) return false;
    flags[*count] = flag; slots[slot] = ++*count; return true;
}

// Caller holds mu. Drain old flags AND damage before asking for replacement snapshots.
// Retain a pending refresh across the cooldown or a stalled outgoing socket.
static void recover_after_drain(void) {
    uint64_t now = sst_now_ms();
    if (!recovery_needed || in_n || enemy_n || status != SST_ONLINE ||
        send_len > 64 * 1024 || (recovery_at && now - recovery_at < 1000)) return;
    Buf empty = {0}; queue_frame(T_WORLD_REQUEST, &empty); queue_frame(T_RESYNC, &empty);
    recovery_needed = false; recovery_at = now; queue_recoveries++;
}

static void put_pose(Buf *b, const SstPose *p) {
    put32(b, p->tick);
    for (int i = 0; i < 3; i++) putf(b, p->pos[i]);
    for (int i = 0; i < 3; i++) put16(b, (uint16_t)p->rot[i]);
    put16(b, p->anim_id);
    putf(b, p->anim_frame);
    put16(b, p->action);
    put16(b, p->health);
    put16(b, p->max_health);
    put8(b, p->equip_flags);
    put8(b, 0);
}
static void get_pose(Rd *r, SstPose *p) {
    p->tick = get32(r);
    for (int i = 0; i < 3; i++) p->pos[i] = getf(r);
    for (int i = 0; i < 3; i++) p->rot[i] = (int16_t)get16(r);
    p->anim_id = get16(r);
    p->anim_frame = getf(r);
    p->action = get16(r);
    p->health = get16(r);
    p->max_health = get16(r);
    p->equip_flags = get8(r);
    get8(r);
}
static void put_scene(Buf *b, const SstScene *s) {
    putstr(b, s->stage);
    put8(b, s->room);
    put8(b, s->layer);
    put8(b, s->entrance);
}

// ---- players (caller holds mu)
static SstPlayer *player(uint16_t id, bool create) {
    for (int i = 0; i < SST_MAX_PLAYERS; i++) if (players[i].active && players[i].id == id) return &players[i];
    if (!create) return NULL;
    for (int i = 0; i < SST_MAX_PLAYERS; i++)
        if (!players[i].active) {
            memset(&players[i], 0, sizeof players[i]);
            players[i].active = true; players[i].id = id;
            players[i].presence_generation = ++presence_serial;
            return &players[i];
        }
    return NULL;
}

// Caller holds mu. A JOIN/scene boundary must not leave an event from the old
// sender incarnation waiting for a later gameplay drain.
static void forget_props(uint16_t from) {
    if (!from) { prop_n = 0; return; }
    int kept = 0;
    for (int i = 0; i < prop_n; i++) if (in_props[i].from != from) in_props[kept++] = in_props[i];
    prop_n = kept;
}
static void forget_pickups(uint16_t from) {
    if (!from) { pickup_n = 0; return; }
    unsigned kept = 0;
    for (unsigned i = 0; i < pickup_n; i++)
        if (in_pickups[i].from != from) in_pickups[kept++] = in_pickups[i];
    pickup_n = kept;
}
static bool pickup_source_current(const SstPickupCommit *commit, uint64_t now, bool arrival) {
    if (status != SST_ONLINE || !have_scene || !sst_pickup_commit_valid(commit)) return false;
    // The host scopes area replays; directed duplicate ACKs can refer to an old
    // area after we leave. All snapshots remain no-award receipts globally.
    if (commit->snapshot) return true;
    if (now < commit->received_ms) return false;
    // A commit authenticated on arrival is a gain already accepted by the host.
    // Keep it until consumed, even if its collector leaves or rejoins meanwhile.
    if (!arrival) return true;
    if (commit->from == my_id) return commit->presence_generation == atomic_load(&generation);
    SstPlayer *source = player(commit->from, false);
    if (!source || !source->active || strcmp(source->team, cfg.team) ||
        source->presence_generation != commit->presence_generation) return false;
    // The host authenticated the collector's pose/area before committing. A peer
    // in another area need not have any local pose, and unordered UDP poses may
    // still show its collection animation. Presence is authenticated only here.
    return true;
}
static bool prop_source_current(const SstProp *prop, uint64_t now) {
    SstPlayer *source = player(prop->from, false);
    int32_t ticks = source ? (int32_t)(source->pose.tick - prop->pose_tick) : 0;
    return status == SST_ONLINE && have_scene && source && source->has_pose &&
        source->presence_generation == prop->presence_generation &&
        !strcmp(source->team, cfg.team) && source->pose.health && !(source->pose.equip_flags & 0xc0) &&
        now >= source->pose_time_ms && now - source->pose_time_ms <= SST_PROP_FRESH_MS &&
        now >= prop->received_ms && now - prop->received_ms <= SST_PROP_FRESH_MS &&
        ticks >= -PROP_POSE_TICKS && ticks <= PROP_POSE_TICKS &&
        sst_world_same_area(&source->scene, &prop->scene) && sst_world_same_area(&my_scene, &prop->scene) &&
        !sst_world_boss_stage(prop->scene.stage);
}

static void on_frame_locked(uint8_t type, const uint8_t *body, size_t n) {
    Rd r = {body, n, 0, false};
    packet_at = sst_now_ms();
    switch (type) {
    case T_WELCOME: {
        // Validate the complete legacy list before replacing presence/receipt context.
        Rd check={body,n,0,false};uint16_t assigned=get16(&check);unsigned listed=get8(&check);
        char text[SST_STR];uint16_t seen[SST_MAX_PLAYERS]={0};bool invalid=!assigned || listed>SST_MAX_PLAYERS;
        if(listed<=SST_MAX_PLAYERS)for(unsigned i=0;i<listed && !check.bad;i++) {
            uint16_t id=get16(&check);
            if(!id || id==assigned)invalid=true;
            for(unsigned j=0;j<i;j++)if(seen[j]==id)invalid=true;
            seen[i]=id;getstr(&check,text,sizeof text);getstr(&check,text,sizeof text);
            getstr(&check,text,sizeof text);get8(&check);
        }
        if(invalid || check.bad || check.o!=n) {
            snprintf(last_error,sizeof last_error,"Invalid co-op welcome");failure=SST_FAIL_REJECTED;status=SST_FAILED;
            sst_debug_log(SST_DBG_NET,"invalid co-op welcome rejected");break;
        }
        my_id = get16(&r);
        memset(players, 0, sizeof players);
        forget_props(0); prop_rate_at = 0; prop_carries = prop_places = 0;
        forget_pickups(0);
        int count = get8(&r);
        for (int i = 0; i < count && !r.bad; i++) {
            SstPlayer *p = player(get16(&r), true);
            SstPlayer tmp;
            if (!p) p = &tmp;
            getstr(&r, p->name, sizeof p->name);
            getstr(&r, p->team, sizeof p->team);
            getstr(&r, p->scene.stage, sizeof p->scene.stage);
            p->scene.room = get8(&r);
        }
        atomic_fetch_add(&generation, 1);
        status = SST_ONLINE;
        last_error[0] = 0;
        phase=SST_PHASE_ONLINE;failure=SST_FAIL_NONE;handshakes++;consecutive_failures=0;handshake_at=packet_at;
        sst_debug_log(SST_DBG_NET,"handshake complete: player %u, protocol %d",my_id,SST_PROTO);
        sst_debug_enemies_clear();
        // resend our state after a (re)connect, then flush flags queued while offline and catch up
        Buf b = {.n = 0};
        if (have_scene) { put_scene(&b, &my_scene); queue_frame(T_SCENE, &b); }
        udp_scene_request();
        for (int i = 0; i < out_n; i++) {
            b.n = 0;
            put8(&b, out_flags[i].kind); put16(&b, out_flags[i].index); put8(&b, out_flags[i].value);
            queue_frame(T_FLAG, &b);
        }
        out_n = 0;
        memset(out_flag_slots, 0, sizeof out_flag_slots);
        b.n = 0;
        queue_frame(T_WORLD_REQUEST, &b);
        queue_frame(T_RESYNC, &b);
        break;
    }
    case T_PONG: {
        uint32_t token = get32(&r);
        if (n == 4 && !r.bad && ping_pending && token == ping_token &&
            packet_at >= ping_sent_at && packet_at - ping_sent_at <= 20000) {
            round_trip_ms = (unsigned)(packet_at - ping_sent_at);
            have_rtt = true; ping_pending = false;
        }
        break;
    }
    case SST_UDP_OFFER:
        if (n==16 && cfg.use_udp && udp_sock!=SST_INVALID_SOCKET) {
            memcpy(udp_token,body,16); udp_offered=true;
        }
        break;
    case SST_UDP_SCENE: {
        uint16_t id=get16(&r); uint32_t cookie=get32(&r), epoch=get32(&r);
        if (r.bad || r.o!=n) break;
        if (id==my_id) { if (cookie==udp_cookie) udp_scene=epoch; }
        else { SstPlayer *p=player(id,false); if (p) p->udp_scene=epoch; }
        break;
    }
    case T_PLAYER_JOIN: {
        SstPlayer joined={.active=true};joined.id=get16(&r);
        getstr(&r,joined.name,sizeof joined.name);getstr(&r,joined.team,sizeof joined.team);
        if(r.bad || r.o!=n || !joined.id || joined.id==my_id)break;
        SstPlayer *p = player(joined.id, true);
        if (p) {
            // Replacing a live socket retains its ID and sends JOIN without LEAVE. Its tick
            // counter may restart; neither the old pose nor its interpolation history survives.
            uint16_t id = p->id;
            forget_props(id);
            joined.presence_generation=++presence_serial;*p=joined;
            sst_debug_log(SST_DBG_PLAYER,"player %u joined",id);sst_debug_enemies_clear();
        }
        break;
    }
    case T_PLAYER_LEAVE: {
        uint16_t id=get16(&r);
        if(r.bad || r.o!=n || !id || id==my_id)break;
        SstPlayer *p = player(id, false);
        if (p) { forget_props(p->id); p->active = false;
            sst_debug_log(SST_DBG_PLAYER,"player %u left",p->id);sst_debug_enemies_clear(); }
        break;
    }
    case T_ERROR: {
        char reason[sizeof last_error]={0};getstr(&r,reason,sizeof reason);
        if(r.bad || r.o!=n)break;
        memcpy(last_error,reason,sizeof last_error);
        status = SST_FAILED;
        failure=SST_FAIL_REJECTED;sst_debug_log(SST_DBG_NET,"host rejected join");
        break;
    }
    case T_SCENE: {
        uint16_t id=get16(&r);SstScene scene={0};
        getstr(&r,scene.stage,sizeof scene.stage);
        scene.room=get8(&r);scene.layer=get8(&r);scene.entrance=get8(&r);
        if(r.bad || r.o!=n || !id || id==my_id)break;
        SstPlayer *p = player(id, false);
        if (p) {
            forget_props(p->id);
            // Consumers may miss an intermediate departure, or receive a
            // same-area reload. Retire accepted targets and playout history too.
            p->presence_generation = ++presence_serial;
            p->scene=scene;
            p->has_pose = false;  // a pose from the old stage must not be drawn in the new one
            p->udp_scene=0;
        }
        break;
    }
    case T_POSE: {
        SstPlayer *p = player(get16(&r), false);
        SstPose pose;
        get_pose(&r, &pose);
        if (p && !r.bad && r.o==n && sst_udp_pose_valid(body+2,n-2) &&
            (!p->has_pose || (int32_t)(pose.tick - p->pose.tick) > 0)) {
            p->pose = pose;
            p->has_pose = true;
            p->pose_time_ms = sst_now_ms();
        }
        break;
    }
    case T_FLAG: {
        uint16_t from = get16(&r);
        Flag f = {.from = from};
        f.kind = get8(&r); f.index = get16(&r); f.value = get8(&r);
        if (!r.bad && r.o == n && !queue_flag(in_flags, &in_n, in_flag_slots, f)) {
            recovery_needed = true;
            refresh_active = refresh_done = 0; // a partial replay cannot acknowledge recovery
        }
        break;
    }
    case T_WORLD_DONE: {
        uint32_t token = get32(&r);
        if (!r.bad && n == 4 && token && token == refresh_active) refresh_done = token;
        break;
    }
    case T_RESYNC:
        if (n == 2) world_request = true;
        break;
    case T_CHEST: {
        SstChestReward reward = {.from = get16(&r)};
        reward.index = get16(&r); reward.item = get16(&r);
        if (r.bad || r.o != n || !sst_chest_valid(&reward)) break;
        int i;
        for (i = 0; i < chest_n; i++) if (in_chests[i].index == reward.index) break;
        if (i == chest_n && chest_n < SST_CHESTS) in_chests[chest_n++] = reward;
        break;
    }
    case T_ENEMY: {
        SstEnemyEvent e = {0};
        e.from = get16(&r);
        SstPlayer *source = player(e.from, false);
        e.presence_generation = source ? source->presence_generation : 0;
        getstr(&r, e.scene.stage, sizeof e.scene.stage);
        e.scene.room = get8(&r); e.scene.layer = get8(&r);
        uint64_t lo = get32(&r); e.key = lo | (uint64_t)get32(&r) << 32;
        e.sequence = get32(&r); e.profile = get16(&r);
        e.damage = get16(&r); e.health = get16(&r);
        uint8_t mode = get8(&r); e.snapshot = mode == 1;
        if (!r.bad && r.o == n && mode <= 1) {
            enemy_rx_n++;enemy_rx_at=packet_at;sst_debug_enemy_event(&e);
            if (enemy_n < 512) in_enemies[enemy_n++] = e;
            else recovery_needed = true;
        }
        break;
    }
    case T_MOTION: {
        SstEnemyMotion m = {0}; m.from = get16(&r);
        SstPlayer *source = player(m.from, false);
        m.presence_generation = source ? source->presence_generation : 0;
        getstr(&r, m.scene.stage, sizeof m.scene.stage); m.scene.room = get8(&r); m.scene.layer = get8(&r);
        uint64_t lo = get32(&r); m.key = lo | (uint64_t)get32(&r) << 32;
        m.sequence = get32(&r); m.profile = get16(&r);
        for (int i = 0; i < 3; i++) m.pos[i] = getf(&r);
        for (int i = 0; i < 3; i++) m.rot[i] = (int16_t)get16(&r);
        for (int i = 0; i < 3; i++) m.velocity[i] = getf(&r);
        m.received_ms = sst_now_ms();
        if (r.bad || r.o != n || !sst_motion_valid(&m)) break;
        motion_rx_n++;motion_rx_at=m.received_ms;sst_debug_enemy_motion(&m);
        int slot = motion_n;
        for (int i = 0; i < motion_n; i++)
            if (in_motion[i].from == m.from && in_motion[i].key == m.key &&
                sst_world_same_area(&in_motion[i].scene, &m.scene)) { slot = i; break; }
        // JOIN can retain the sender ID while restarting its sequence. A fresh
        // incarnation replaces its pending old stream even with a lower sequence.
        if (slot < 512 && (slot == motion_n ||
            (m.presence_generation == in_motion[slot].presence_generation ?
                (int32_t)(m.sequence - in_motion[slot].sequence) > 0 :
                (int32_t)(m.presence_generation - in_motion[slot].presence_generation) > 0))) {
            in_motion[slot] = m; if (slot == motion_n) motion_n++;
        }
        break;
    }
    case T_PROP:
    case T_PROPS_ALL: {
        if (n < 2 || n > SST_PROP_WIRE_MAX + 2) break;
        SstProp prop;
        if (!sst_prop_decode(body + 2, n - 2, &prop) ||
            (type == T_PROP && prop.key.profile != 0x1f0)) break;
        prop.from = (uint16_t)(body[0] | (uint16_t)body[1] << 8);
        SstPlayer *source = player(prop.from, false);
        prop.presence_generation = source ? source->presence_generation : 0;
        prop.received_ms = packet_at;
        if (!prop_source_current(&prop, packet_at)) break;
        int slot = prop_n;
        for (int i = 0; i < prop_n; i++)
            if (in_props[i].from == prop.from && sst_prop_key_equal(&in_props[i].key, &prop.key) &&
                sst_world_same_area(&in_props[i].scene, &prop.scene)) { slot = i; break; }
        if (slot < PROP_QUEUE && (slot == prop_n || (int32_t)(prop.sequence - in_props[slot].sequence) > 0)) {
            // CARRY -> PLACE replaces the same sender/key entry; an older delayed
            // carry cannot overwrite its placement. New acquisition may replace it.
            in_props[slot] = prop;
            if (slot == prop_n) prop_n++;
        }
        break;
    }
    case T_PICKUP: {
        SstPickupCommit commit;
        if (!sst_pickup_commit_decode(body, n, &commit)) break;
        SstPlayer *source = player(commit.from, false);
        commit.presence_generation = commit.from == my_id ? atomic_load(&generation) :
            source ? source->presence_generation : 0;
        commit.received_ms = packet_at;
        if (!pickup_source_current(&commit, packet_at, true)) break;
        unsigned slot = pickup_n;
        for (unsigned i = 0; i < pickup_n; i++)
            if (in_pickups[i].epoch == commit.epoch &&
                sst_pickup_key_equal(&in_pickups[i].pickup, in_pickups[i].from,
                                     &commit.pickup, commit.from)) { slot = i; break; }
        if (slot < pickup_n) {
            // An announcement can never replace an undelivered award.
            if (in_pickups[slot].snapshot && !commit.snapshot) in_pickups[slot] = commit;
        } else if (pickup_n < PICKUP_QUEUE) in_pickups[pickup_n++] = commit;
        break;
    }
    default: break;
    }
}
static void on_frame(uint8_t type, const uint8_t *body, size_t n) {
    pthread_mutex_lock(&mu); on_frame_locked(type,body,n); pthread_mutex_unlock(&mu);
}

// ---- socket thread
static SstSocket dial(void) {
    char port[8];
    snprintf(port, sizeof port, "%u", cfg.port);
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *res = NULL;
    phase=SST_PHASE_RESOLVE;
    int err = getaddrinfo(cfg.host, port, &hints, &res);
    if (err) { set_error(SST_FAIL_RESOLVE,"%s: %s", cfg.host, gai_strerror(err)); return SST_INVALID_SOCKET; }
    phase=SST_PHASE_TCP_CONNECT;
    pthread_mutex_lock(&mu);unsigned attempt=++attempts;pthread_mutex_unlock(&mu);
    sst_debug_log(SST_DBG_NET,"TCP connect attempt %u (%u ms limit)",attempt,connect_timeout_ms);
    SstSocket fd = SST_INVALID_SOCKET;
    for (struct addrinfo *a = res; a && running; a = a->ai_next) {
        fd = sst_socket_open(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == SST_INVALID_SOCKET) {set_error(SST_FAIL_SOCKET,"socket: %s",strerror(errno));continue;}
        // connect with a timeout so stop() stays responsive
        int connected=sst_socket_connect(fd,a->ai_addr,(SstSocklen)a->ai_addrlen);
        if(!connected)break;
        if(errno==EINPROGRESS) {
            uint64_t deadline=sst_now_ms()+connect_timeout_ms;int ready=0;
            while(running && sst_now_ms()<deadline) {
                SstPoll pf={fd,POLLOUT,0};uint64_t left=deadline-sst_now_ms();
                ready=sst_socket_poll(&pf,1,(int)(left<100?left:100));
                if(ready<0 && errno==EINTR)continue;
                if(ready)break;
            }
            int soerr=0;
            if(!running){sst_socket_close(fd);fd=SST_INVALID_SOCKET;break;}
            if(!ready)set_error(SST_FAIL_CONNECT_TIMEOUT,"connect %s:%u: timed out",cfg.host,cfg.port);
            else if(ready<0 || sst_socket_error(fd,&soerr))set_error(SST_FAIL_SOCKET,"connect: %s",strerror(errno));
            else if(soerr)set_error(connect_failure(soerr),"connect %s:%u: %s",cfg.host,cfg.port,strerror(soerr));
            else break;
        } else {
            int code=errno;set_error(connect_failure(code),"connect %s:%u: %s",cfg.host,cfg.port,strerror(code));
        }
        sst_socket_close(fd);
        fd = SST_INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (fd != SST_INVALID_SOCKET) { int one = 1; sst_socket_option(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); }
    return fd;
}

static void udp_read(void) {
    for (int attempt=0;attempt<64;attempt++) {
        uint8_t b[SST_UDP_MAX+1];
        ptrdiff_t n=sst_socket_recv(udp_sock,b,sizeof b);
        if (n<0) break;
        pthread_mutex_lock(&mu);
        if (!udp_offered || n<SST_UDP_HEADER ||
            !sst_udp_valid(b,(size_t)n,udp_token,b[24] ? udp_rx_seq : udp_ack_seq)) { pthread_mutex_unlock(&mu); continue; }
        const uint8_t *body=b+SST_UDP_HEADER; size_t size=(size_t)n-SST_UDP_HEADER;
        uint8_t type=b[24]; bool deliver=false;
        uint32_t nonce=size==4 ? sst_udp_u32(body) : 0;
        if (!type && size==4 && nonce && (int32_t)(udp_probe-nonce)>=0 && udp_probe-nonce<4 &&
            sst_now_ms()-udp_probe_times[nonce%4]<SST_UDP_TIMEOUT) {
            udp_ack_seq=sst_udp_u32(b+20); udp_ack_at=sst_now_ms();
            if(!udp_probe_acked[nonce%4]){udp_probe_acked[nonce%4]=true;udp_probes_acked++;}
            if (nonce==udp_probe) udp_probe_pending=false;
            sst_udp_feedback(&udp_budget,true);
            // Confirm the acknowledged challenge, including on paths slower than one probe period.
            if (!udp_active) {
                udp_active=true;
                sst_debug_log(SST_DBG_NET,"movement transport: UDP");
                if (getenv("SST_TRACE")) fprintf(stderr,"[sst] Movement transport: UDP\n");
            }
            Buf confirm={0}; put32(&confirm,nonce); queue_frame(SST_UDP_READY,&confirm); udp_received++;
        } else if ((type==T_POSE || type==T_MOTION) && size>=10 && udp_scene &&
                   sst_udp_u32(body+6)==udp_scene) {
            uint16_t from=body[0]|(uint16_t)body[1]<<8;
            SstPlayer *p=player(from,false);
            if (p && p->udp_scene==sst_udp_u32(body+2) && p->udp_scene &&
                sst_world_same_area(&my_scene,&p->scene)) {
                udp_rx_seq=sst_udp_u32(b+20); udp_received++; deliver=true;
            }
        }
        if (deliver) {
            // Reuse the bounded parsers with their normal source-ID prefix.
            uint8_t payload[SST_UDP_MAX]; payload[0]=body[0]; payload[1]=body[1];
            memcpy(payload+2,body+10,size-10); on_frame_locked(type,payload,size-8);
        }
        pthread_mutex_unlock(&mu);
    }
}

static void run_connection(SstSocket fd) {
    pthread_mutex_lock(&mu);
    sock = fd;
    udp_sock=SST_INVALID_SOCKET;
    udp_offered=udp_active=udp_probe_pending=false; udp_clear_live();
    udp_tx_seq=udp_rx_seq=udp_ack_seq=udp_cookie=udp_scene=udp_probe=0; udp_ack_at=udp_probe_at=0;
    memset(udp_probe_times,0,sizeof udp_probe_times);
    udp_sent=udp_received=udp_fallbacks=0; memset(&udp_budget,0,sizeof udp_budget);
    udp_probes_sent=udp_probes_acked=0;memset(udp_probe_acked,0,sizeof udp_probe_acked);
    enemy_rx_n=motion_rx_n=0;enemy_rx_at=motion_rx_at=0;sst_debug_enemies_clear();
    udp_cursor=0; udp_pose_at=0;
    if (cfg.use_udp) {
        struct sockaddr_storage address; SstSocklen len=sizeof address;
        if (!getpeername(fd,(void *)&address,&len)) {
            udp_sock=sst_socket_open(address.ss_family,SOCK_DGRAM,0);
            if (udp_sock!=SST_INVALID_SOCKET && sst_socket_connect(udp_sock,(void *)&address,len)<0) {
                sst_socket_close(udp_sock); udp_sock=SST_INVALID_SOCKET;
            }
        }
    }
    ping_pending = have_rtt = false; packet_at = 0; round_trip_ms = 0;
    recovery_needed = false; recovery_at = 0; queue_recoveries = 0;
    forget_props(0); prop_rate_at = 0; prop_carries = prop_places = 0;
    forget_pickups(0);
    send_len = 0;  // nothing from a previous connection is valid here (flags live in out_flags)
    Buf b = {.n = 0};
    put16(&b, SST_PROTO);
    putstr(&b, cfg.name); putstr(&b, cfg.room); putstr(&b, cfg.team);
    for (int i = 0; i < 16; i++) put8(&b, session[i]);
    queue_frame(T_HELLO, &b);
    phase=SST_PHASE_HANDSHAKE;
    sst_debug_log(SST_DBG_NET,"TCP connected; HELLO queued (protocol %d)",SST_PROTO);
    pthread_mutex_unlock(&mu);

    static uint8_t rbuf[MAX_FRAME + 8];
    size_t rlen = 0;
    bool pickup_blocked = false;
    uint64_t last_rx = sst_now_ms(), connected_at=last_rx, last_ping = 0;
    while (running) {
        uint64_t now = sst_now_ms();
        if(status!=SST_ONLINE && now-connected_at>=handshake_timeout_ms) {
            set_error(SST_FAIL_HANDSHAKE_TIMEOUT,"Host accepted TCP but sent no co-op welcome within %u ms",handshake_timeout_ms);
            break;
        }
        udp_tick(now);
        if (now - last_ping > 5000) {
            pthread_mutex_lock(&mu);
            ping_sent_at = now; ping_token = (uint32_t)now; ping_pending = true;
            Buf p = {.n = 0}; put32(&p, ping_token); queue_frame(T_PING, &p);
            pthread_mutex_unlock(&mu);
            last_ping = now;
        }
        if (pickup_blocked) last_rx = now; // local consumer backpressure is not a dead relay
        else if (now - last_rx > 20000) { set_error(SST_FAIL_SILENT,"relay stopped answering"); break; }

        pthread_mutex_lock(&mu);
        bool want_write = send_len > 0;
        pthread_mutex_unlock(&mu);
        SstPoll polls[2]={{fd,(pickup_blocked ? 0 : POLLIN) | (want_write ? POLLOUT : 0),0},{udp_sock,POLLIN,0}};
        if (sst_socket_poll(polls, 2, want_write ? 5 : 15) < 0 && errno != EINTR) {
            set_error(SST_FAIL_SOCKET,"poll: %s",strerror(errno));break;
        }
        if (polls[1].revents & (POLLIN|POLLERR)) udp_read();
        SstPoll pf=polls[0];
        if (pf.revents & (POLLERR | POLLHUP) && !(pf.revents & POLLIN)) { set_error(SST_FAIL_CLOSED,"connection lost"); break; }
        if (pf.revents & POLLOUT) {
            pthread_mutex_lock(&mu);
            ptrdiff_t w = sst_socket_send(fd, sendbuf, send_len);
            if (w > 0) { memmove(sendbuf, sendbuf + w, send_len - (size_t)w); send_len -= (size_t)w; }
            pthread_mutex_unlock(&mu);
            if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { set_error(SST_FAIL_CLOSED,"send: %s", strerror(errno)); break; }
        }
        if ((pf.revents & POLLIN) && rlen < sizeof rbuf) {
            ptrdiff_t got = sst_socket_recv(fd, rbuf + rlen, sizeof rbuf - rlen);
            if (got == 0) { if (status != SST_FAILED) set_error(SST_FAIL_CLOSED,"relay closed the connection"); break; }
            if (got < 0) { if (errno == EAGAIN || errno == EINTR) continue; set_error(SST_FAIL_CLOSED,"recv: %s", strerror(errno)); break; }
            rlen += (size_t)got;
            last_rx = sst_now_ms();
        }
        if (rlen) {
            size_t off = 0; pickup_blocked = false;
            while (rlen - off >= 4) {
                uint32_t len;
                memcpy(&len, rbuf + off, 4);
                if (len < 1 || len > MAX_FRAME) { set_error(SST_FAIL_SOCKET,"bad frame from relay"); goto out; }
                if (rlen - off < 4 + len) break;
                if (rbuf[off + 4] == T_PICKUP) {
                    pthread_mutex_lock(&mu);
                    pickup_blocked = pickup_n == PICKUP_QUEUE;
                    pthread_mutex_unlock(&mu);
                    if (pickup_blocked) break; // bounded TCP backpressure, never drop an award
                }
                on_frame(rbuf[off + 4], rbuf + off + 5, len - 1);
                off += 4 + len;
            }
            memmove(rbuf, rbuf + off, rlen - off);
            rlen -= off;
            if (status == SST_FAILED) goto out;
        }
    }
out:
    pthread_mutex_lock(&mu);
    sock = SST_INVALID_SOCKET;
    if (udp_sock!=SST_INVALID_SOCKET) sst_socket_close(udp_sock);
    udp_sock=SST_INVALID_SOCKET; udp_active=udp_offered=false; udp_clear_live();
    memset(players, 0, sizeof players);
    in_n = enemy_n = motion_n = chest_n = prop_n = 0; world_request = false;
    forget_pickups(0);
    refresh_active = refresh_done = 0;
    memset(in_flag_slots, 0, sizeof in_flag_slots); recovery_needed = false;
    pthread_mutex_unlock(&mu);
    sst_socket_close(fd);
}

static void *net_main(void *arg) {
    (void)arg;
    static const int backoff_s[] = {1, 2, 4, 8, 15, 30};
    int attempt = 0;
    while (running) {
        status = SST_CONNECTING;
        SstSocket fd = dial();
        if (fd != SST_INVALID_SOCKET) {
            uint64_t t0 = sst_now_ms();
            run_connection(fd);
            if (sst_now_ms() - t0 > 60000) attempt = 0;  // was stable: retry quickly
        }
        if(!running)break;
        pthread_mutex_lock(&mu);consecutive_failures++;pthread_mutex_unlock(&mu);
        if (status == SST_FAILED || !cfg.auto_reconnect) { if (status != SST_FAILED) status = SST_OFFLINE; break; }
        status = SST_CONNECTING;
        int wait = backoff_s[attempt < 5 ? attempt : 5];
        attempt++;
        phase=SST_PHASE_BACKOFF;
        pthread_mutex_lock(&mu);retry_at=sst_now_ms()+(uint64_t)wait*1000;pthread_mutex_unlock(&mu);
        sst_debug_log(SST_DBG_NET,"retrying in %d s",wait);
        for (int i = 0; i < wait * 10 && running; i++) usleep(100000);
    }
    phase=SST_PHASE_IDLE;
    if (!running) status = SST_OFFLINE;
    return NULL;
}

void sst_net_start(const SstConfig *c) {
    sst_net_stop();
    if (!c || !c->port || !c->host[0] || !memchr(c->host,0,sizeof c->host)) {
        sst_net_set_offline_error(SST_FAIL_ADDRESS,"Invalid host address or port");return;
    }
    if (!sst_socket_init()) { set_error(SST_FAIL_SOCKET,"Could not initialize networking: %s",strerror(errno)); status=SST_FAILED; return; }
    pthread_mutex_lock(&mu); cfg = *c; pthread_mutex_unlock(&mu);
#ifdef _WIN32
    bool random = true;
    for (unsigned i=0;i<4;i++) {
        unsigned value;
        if (rand_s(&value)) { random=false; break; }
        memcpy(session+i*4,&value,4);
    }
    if (!random) {
        uint64_t t=sst_now_ms() ^ (uint64_t)(uintptr_t)&cfg;
        uint64_t other=((uint64_t)GetCurrentProcessId()<<32) ^ GetTickCount64();
        memcpy(session,&t,8); memcpy(session+8,&other,8);
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(session, 1, sizeof session, f) != sizeof session) {
        uint64_t t = sst_now_ms() ^ (uint64_t)(uintptr_t)&cfg;
        memcpy(session, &t, 8);
    }
    if (f) fclose(f);
#endif
    pthread_mutex_lock(&mu);
    last_error[0] = 0;
    my_id = 0;
    have_scene = false;
    out_n = in_n = 0;
    memset(out_flag_slots, 0, sizeof out_flag_slots); memset(in_flag_slots, 0, sizeof in_flag_slots);
    recovery_needed = false; recovery_at = 0; queue_recoveries = 0;
    enemy_n = motion_n = chest_n = prop_n = 0; world_request = false;
    forget_pickups(0);
    prop_rate_at = 0; prop_carries = prop_places = 0;
    refresh_active = refresh_done = 0;
    memset(players, 0, sizeof players);
    failure=SST_FAIL_NONE;attempts=consecutive_failures=handshakes=0;retry_at=handshake_at=0;
    connect_timeout_ms=env_ms("SST_CONNECT_TIMEOUT_MS",5000);
    handshake_timeout_ms=env_ms("SST_HANDSHAKE_TIMEOUT_MS",10000);
    pthread_mutex_unlock(&mu);
    status = SST_CONNECTING;
    phase=SST_PHASE_RESOLVE;
    sst_debug_log(SST_DBG_NET,"joining (protocol %d, UDP %s, reconnect %s)",SST_PROTO,c->use_udp?"on":"off",c->auto_reconnect?"on":"off");
    running = true;
    int err = pthread_create(&thread, NULL, net_main, NULL);
    if (err) { running = false;phase=SST_PHASE_IDLE;set_error(SST_FAIL_SOCKET,"Could not start networking: %s", strerror(err)); status = SST_FAILED; }
}

void sst_net_stop(void) {
    if (!running) return;
    running = false;
    pthread_join(thread, NULL);
    status = SST_OFFLINE;
    phase=SST_PHASE_IDLE;sst_debug_log(SST_DBG_NET,"networking stopped");
}

void sst_net_set_offline_error(SstNetFailure kind,const char *text) {
    if(status==SST_ONLINE || status==SST_CONNECTING)return;
    sst_net_stop();
    set_error(kind,"%s",text?text:"Invalid host address");
    status=SST_OFFLINE;phase=SST_PHASE_IDLE;
}
const char *sst_net_phase_name(SstNetPhase p) {
    static const char *const names[]={"idle","resolving address","TCP connect","protocol handshake","online","waiting to retry"};
    return (unsigned)p<sizeof names/sizeof *names?names[p]:"?";
}
const char *sst_net_failure_hint(SstNetFailure f) {
    switch(f) {
    case SST_FAIL_NONE:return "";
    case SST_FAIL_ADDRESS:return "Enter a valid host address and port, for example 192.0.2.1:7623.";
    case SST_FAIL_RESOLVE:return "The host name could not be resolved. Check its spelling or use the host's current numeric IPv4 address.";
    case SST_FAIL_CONNECT_TIMEOUT:return "No TCP reply arrived before any co-op handshake. Check the current host address, hosting listener, firewall and configured TCP port forwarding; save files cannot cause this TCP timeout.";
    case SST_FAIL_CONNECT_REFUSED:return "The TCP connection was refused. Confirm the host pressed Host game and check the address, port and firewall.";
    case SST_FAIL_CONNECT_UNREACHABLE:return "No network route was available. Check your connection and routing or VPN settings.";
    case SST_FAIL_CONNECT_OTHER:case SST_FAIL_SOCKET:return "A local socket error occurred. Inspect the connection error and developer diagnostics.";
    case SST_FAIL_HANDSHAKE_TIMEOUT:return "TCP connected, but no valid co-op welcome arrived. Check the listening program, host responsiveness and matching builds.";
    case SST_FAIL_REJECTED:return "The host rejected the join. Check its message and match the build/protocol, Room and Team.";
    case SST_FAIL_CLOSED:return "The connection closed. Check whether the host is still hosting and reachable.";
    case SST_FAIL_SILENT:return "No host traffic arrived for 20 seconds. Check the host's responsiveness and network connection.";
    }
    return "";
}
SstNetDiag sst_net_diag(void) {
    SstNetDiag d={0};uint64_t now=sst_now_ms();pthread_mutex_lock(&mu);
    d.status=status;d.phase=phase;d.failure=failure;
    snprintf(d.host,sizeof d.host,"%s",cfg.host);d.port=cfg.port;d.protocol=SST_PROTO;
    d.my_id=status==SST_ONLINE?my_id:0;d.auto_reconnect=cfg.auto_reconnect;d.use_udp=cfg.use_udp;
    snprintf(d.last_error,sizeof d.last_error,"%s",last_error);
    d.attempts=attempts;d.consecutive_failures=consecutive_failures;d.handshakes=handshakes;
    d.next_retry_in_ms=phase==SST_PHASE_BACKOFF && retry_at>now?retry_at-now:0;
    d.has_handshake=handshakes>0;d.since_handshake_ms=handshakes && now>=handshake_at?now-handshake_at:0;
    d.connect_timeout_ms=connect_timeout_ms;d.handshake_timeout_ms=handshake_timeout_ms;
    d.udp_probes_sent=udp_probes_sent;d.udp_probes_acked=udp_probes_acked;
    d.enemy_events=enemy_rx_n;d.motion_updates=motion_rx_n;
    d.has_enemy_rx=enemy_rx_at!=0;d.has_motion_rx=motion_rx_at!=0;
    d.enemy_rx_age_ms=enemy_rx_at && now>=enemy_rx_at?now-enemy_rx_at:0;
    d.motion_rx_age_ms=motion_rx_at && now>=motion_rx_at?now-motion_rx_at:0;
    pthread_mutex_unlock(&mu);return d;
}

SstStatus sst_net_status(char *why, int why_size) {
    if (why && why_size > 0) { pthread_mutex_lock(&mu); snprintf(why, why_size, "%s", last_error); pthread_mutex_unlock(&mu); }
    return status;
}
uint16_t sst_net_my_id(void) {
    pthread_mutex_lock(&mu);
    uint16_t id = status == SST_ONLINE ? my_id : 0;
    pthread_mutex_unlock(&mu);
    return id;
}
uint32_t sst_net_generation(void) { return atomic_load(&generation); }
SstConnectionStats sst_net_stats(void) {
    pthread_mutex_lock(&mu);
    SstConnectionStats s = {0};
    if (status == SST_ONLINE && sock != SST_INVALID_SOCKET) {
        s.online = true; s.has_rtt = have_rtt; s.rtt_ms = round_trip_ms;
        uint64_t now = sst_now_ms();
        s.last_packet_age_ms = packet_at && now >= packet_at ? now - packet_at : 0;
        s.queued_bytes = send_len;
        s.queue_recoveries = queue_recoveries;
        s.udp_available=udp_offered; s.udp_active=udp_active && udp_scene!=0;
        s.udp_sent=udp_sent; s.udp_received=udp_received; s.udp_fallbacks=udp_fallbacks;
    }
    pthread_mutex_unlock(&mu); return s;
}

void sst_net_send_scene(const SstScene *s) {
    pthread_mutex_lock(&mu);
    forget_props(0); prop_rate_at = 0; prop_carries = prop_places = 0;
    // Pickup commits and directed no-award ACKs survive ordinary area changes.
    my_scene = *s;
    have_scene = true;
    if (sock != SST_INVALID_SOCKET && status == SST_ONLINE) {
        Buf b = {.n = 0}; put_scene(&b, s); queue_frame(T_SCENE, &b); udp_scene_request();
    }
    pthread_mutex_unlock(&mu);
}

void sst_net_send_pose(const SstPose *p) {
    pthread_mutex_lock(&mu);
    if (sock != SST_INVALID_SOCKET && status == SST_ONLINE &&
        ((udp_active && udp_scene) || send_len < 64 * 1024)) {
        Buf b = {.n = 0}; put_pose(&b, p); queue_live(T_POSE, &b,0);
    }
    pthread_mutex_unlock(&mu);
}

void sst_net_send_flag(uint8_t kind, uint16_t index, uint8_t value) {
    pthread_mutex_lock(&mu);
    if (sock != SST_INVALID_SOCKET && status == SST_ONLINE) {
        Buf b = {.n = 0}; put8(&b, kind); put16(&b, index); put8(&b, value); queue_frame(T_FLAG, &b);
    } else {
        queue_flag(out_flags, &out_n, out_flag_slots, (Flag){.kind = kind, .index = index, .value = value});
    }
    pthread_mutex_unlock(&mu);
}

int sst_net_players(SstPlayer *out, int max) {
    int n = 0;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < SST_MAX_PLAYERS && n < max; i++) if (players[i].active) out[n++] = players[i];
    pthread_mutex_unlock(&mu);
    return n;
}

void sst_net_poll_flags(SstFlagFn fn, void *user) {
    static Flag local[FLAG_QUEUE];
    pthread_mutex_lock(&mu);
    int n = in_n;
    memcpy(local, in_flags, n * sizeof *local);
    in_n = 0;
    memset(in_flag_slots, 0, sizeof in_flag_slots); recover_after_drain();
    pthread_mutex_unlock(&mu);
    for (int i = 0; i < n; i++) fn(local[i].kind, local[i].index, local[i].value, local[i].from, user);
}

void sst_net_request_world(void) {
    pthread_mutex_lock(&mu);
    if (status == SST_ONLINE) {
        Buf b = {0}; queue_frame(T_WORLD_REQUEST, &b); queue_frame(T_RESYNC, &b);
    }
    pthread_mutex_unlock(&mu);
}
uint32_t sst_net_request_world_tracked(void) {
    pthread_mutex_lock(&mu);
    uint32_t token = 0;
    if (status == SST_ONLINE && !recovery_needed && send_len <= 64 * 1024) {
        if (!++refresh_serial) ++refresh_serial;
        token = refresh_active = refresh_serial; refresh_done = 0;
        Buf b = {0}; put32(&b, token); queue_frame(T_WORLD_REQUEST, &b);
        b.n = 0; queue_frame(T_RESYNC, &b);
    }
    pthread_mutex_unlock(&mu);
    return token;
}
SstWorldRefresh sst_net_world_refresh(uint32_t token) {
    pthread_mutex_lock(&mu);
    SstWorldRefresh state = !token || status != SST_ONLINE || token != refresh_active || recovery_needed ?
        SST_REFRESH_INVALID : refresh_done != token || in_n || chest_n ? SST_REFRESH_WAITING : SST_REFRESH_READY;
    pthread_mutex_unlock(&mu);
    return state;
}
bool sst_net_take_world_request(void) {
    pthread_mutex_lock(&mu);
    bool v = world_request; world_request = false;
    pthread_mutex_unlock(&mu);
    return v;
}
void sst_net_send_enemy(const SstEnemyEvent *e) {
    pthread_mutex_lock(&mu);
    if (status == SST_ONLINE && sock != SST_INVALID_SOCKET) {
        Buf b = {0}; putstr(&b, e->scene.stage); put8(&b, e->scene.room); put8(&b, e->scene.layer);
        put32(&b, (uint32_t)e->key); put32(&b, (uint32_t)(e->key >> 32));
        put32(&b, e->sequence); put16(&b, e->profile); put16(&b, e->damage);
        put16(&b, e->health); put8(&b, e->snapshot ? 1 : 0);
        queue_frame(T_ENEMY, &b);
    }
    pthread_mutex_unlock(&mu);
}
void sst_net_poll_enemies(SstEnemyFn fn, void *user) {
    SstEnemyEvent events[512];
    pthread_mutex_lock(&mu);
    int n = enemy_n; memcpy(events, in_enemies, n * sizeof *events); enemy_n = 0;
    recover_after_drain();
    pthread_mutex_unlock(&mu);
    for (int i = 0; i < n; i++) fn(&events[i], user);
}
void sst_net_send_motion(const SstEnemyMotion *m) {
    if (!sst_motion_valid(m)) return;
    pthread_mutex_lock(&mu);
    if (status == SST_ONLINE && sock != SST_INVALID_SOCKET &&
        ((udp_active && udp_scene) || send_len < 64 * 1024)) {
        Buf b = {0}; putstr(&b, m->scene.stage); put8(&b, m->scene.room); put8(&b, m->scene.layer);
        put32(&b, (uint32_t)m->key); put32(&b, (uint32_t)(m->key >> 32));
        put32(&b, m->sequence); put16(&b, m->profile);
        for (int i = 0; i < 3; i++) putf(&b, m->pos[i]);
        for (int i = 0; i < 3; i++) put16(&b, (uint16_t)m->rot[i]);
        for (int i = 0; i < 3; i++) putf(&b, m->velocity[i]);
        queue_live(T_MOTION, &b,m->key);
    }
    pthread_mutex_unlock(&mu);
}
void sst_net_poll_motion(SstMotionFn fn, void *user) {
    SstEnemyMotion batch[512];
    pthread_mutex_lock(&mu);
    int n = motion_n; memcpy(batch, in_motion, n * sizeof *batch); motion_n = 0;
    pthread_mutex_unlock(&mu);
    if (fn) for (int i = 0; i < n; i++) fn(&batch[i], user);
}
void sst_net_send_prop(const SstProp *prop) {
    if (!sst_prop_valid(prop)) return;
    pthread_mutex_lock(&mu);
    uint64_t now = sst_now_ms();
    if (now < prop_rate_at || now - prop_rate_at >= 1000) {
        prop_rate_at = now; prop_carries = prop_places = 0;
    }
    unsigned *count = prop->phase == SST_PROP_PLACE ? &prop_places : &prop_carries;
    unsigned limit = prop->phase == SST_PROP_PLACE ? 16 : 30;
    // Placement has its own rate allowance and an extra bounded queue reserve:
    // a busy carry stream cannot consume the transition's send allowance.
    size_t budget = prop->phase == SST_PROP_PLACE ? 128 * 1024 : 64 * 1024;
    if (status == SST_ONLINE && sock != SST_INVALID_SOCKET && have_scene &&
        sst_world_same_area(&my_scene, &prop->scene) && send_len < budget && *count < limit) {
        Buf body = {0};
        body.n = sst_prop_encode(prop, body.b, sizeof body.b);
        if (body.n) { queue_frame(T_PROPS_ALL, &body); (*count)++; }
    }
    pthread_mutex_unlock(&mu);
}
void sst_net_poll_props(SstPropFn fn, void *user) {
    SstProp batch[PROP_QUEUE];
    int count = 0;
    pthread_mutex_lock(&mu);
    uint64_t now = sst_now_ms();
    for (int i = 0; i < prop_n; i++)
        if (prop_source_current(&in_props[i], now)) batch[count++] = in_props[i];
    prop_n = 0;
    pthread_mutex_unlock(&mu);
    if (fn) for (int i = 0; i < count; i++) fn(&batch[i], user);
}
bool sst_net_send_pickup(const SstPickup *pickup) {
    if (!sst_pickup_valid(pickup)) return false;
    bool queued = false;
    pthread_mutex_lock(&mu);
    if (status == SST_ONLINE && sock != SST_INVALID_SOCKET && have_scene &&
        sst_world_same_area(&my_scene, &pickup->scene) && send_len < 128 * 1024) {
        Buf body = {0};
        body.n = sst_pickup_encode(pickup, body.b, sizeof body.b);
        if (body.n) { size_t before = send_len; queue_frame(T_PICKUP, &body); queued = send_len > before; }
    }
    pthread_mutex_unlock(&mu);
    return queued;
}
void sst_net_poll_pickups(SstPickupFn fn, void *user) {
    SstPickupCommit batch[PICKUP_QUEUE]; unsigned count = 0;
    pthread_mutex_lock(&mu);
    uint64_t now = sst_now_ms();
    for (unsigned i = 0; i < pickup_n; i++)
        if (pickup_source_current(&in_pickups[i], now, false)) batch[count++] = in_pickups[i];
    pickup_n = 0;
    pthread_mutex_unlock(&mu);
    if (fn) for (unsigned i = 0; i < count; i++) fn(&batch[i], user);
}
void sst_net_send_chest(const SstChestReward *r) {
    if (!sst_chest_valid(r)) return;
    pthread_mutex_lock(&mu);
    if (status == SST_ONLINE && sock != SST_INVALID_SOCKET) {
        Buf b = {0}; put16(&b, r->index); put16(&b, r->item); queue_frame(T_CHEST, &b);
    }
    pthread_mutex_unlock(&mu);
}
void sst_net_poll_chests(SstChestFn fn, void *user) {
    SstChestReward batch[SST_CHESTS];
    pthread_mutex_lock(&mu);
    int n = chest_n; memcpy(batch, in_chests, n * sizeof *batch); chest_n = 0;
    pthread_mutex_unlock(&mu);
    if (fn) for (int i = 0; i < n; i++) fn(&batch[i], user);
}
uint16_t sst_net_motion_owner(const SstScene *scene, bool eligible) {
    pthread_mutex_lock(&mu);
    uint16_t owner = status == SST_ONLINE ?
        sst_motion_owner(my_id, eligible, scene, cfg.team, players, SST_MAX_PLAYERS, sst_now_ms()) : 0;
    pthread_mutex_unlock(&mu);
    return owner;
}
bool sst_net_can_catchup(const SstPlayer *peer) {
    pthread_mutex_lock(&mu);
    bool ok = status == SST_ONLINE && sst_world_catchup_allowed(peer, cfg.team, sst_now_ms());
    pthread_mutex_unlock(&mu); return ok;
}
bool sst_net_can_request_catchup(const SstPlayer *peer) {
    pthread_mutex_lock(&mu);
    bool ok = status == SST_ONLINE && sst_world_catchup_request_allowed(peer, cfg.team);
    pthread_mutex_unlock(&mu); return ok;
}
