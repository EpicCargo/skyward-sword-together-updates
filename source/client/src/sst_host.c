#include "sst_host.h"
#include "sst_net.h"
#include "sst_socket.h"
#include "sst_enemy_motion.h"
#include "sst_udp.h"
#include "sst_props.h"
#include "sst_pickup.h"
#include "sst_debug.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { HELLO = 1, WELCOME, JOIN, LEAVE, HOST_ERROR, PING, PONG, SCENE = 0x10, POSE,
       FLAG = 0x20, WORLD_REQUEST, RESYNC, CHEST, WORLD_DONE, ENEMY = 0x30, MOTION = 0x31, PROP = 0x32,
       PICKUP = 0x33, PROPS_ALL = 0x34 };
#define CONNECTIONS 8 // includes pending handshakes and a reconnect replacing an old socket
#define FRAME_MAX 512
#define TX_MAX (128 * 1024)
#define HISTORY 32

typedef struct {
    SstSocket fd;
    uint16_t id;
    char name[SST_STR], team[SST_STR];
    uint8_t session[16];
    SstScene scene;
    uint8_t pose[36];
    bool has_pose;
    uint8_t rx[FRAME_MAX + 4], tx[TX_MAX];
    size_t rx_n, tx_n;
    uint64_t last_rx, last_pose, last_request;
    int catchup; // next flag-cache index, -1 when done
    uint32_t world_cookie; // acknowledge only after the complete incremental flag replay
    bool closing;
    uint8_t udp_token[16];
    bool udp_offered, udp_ready, udp_scene_ready;
    struct sockaddr_in tcp_address, udp_address;
    uint32_t udp_rx_seq, udp_probe_seq, udp_tx_seq, udp_scene, udp_cookie, udp_probe;
    uint64_t udp_at, udp_probe_at;
    SstUdpBudget udp_budget;
    uint64_t prop_rate_at;
    unsigned prop_carries, prop_places;
    bool pickup_seen;
    SstPickup pickup_last;
} Peer;
typedef struct {
    uint8_t session[16];
    uint16_t id;
} Identity;
typedef struct {
    SstSocket listener, udp;
    uint32_t scene_serial;
    uint16_t next_id;
    char room[SST_STR];
    Peer peer[CONNECTIONS];
    Identity history[HISTORY];
    unsigned next_history;
    // One co-op team per hosted game. Keep the full u16 flag-index range without truncation.
    uint8_t flags[16 * 65536];
    uint16_t chests[SST_CHESTS];
    SstPickupHost pickups;
} Host;
static Host *host;
static pthread_t worker;
static atomic_bool running;
static uint16_t bound_port;
static char bound_address[INET_ADDRSTRLEN];

typedef struct {
    uint8_t b[FRAME_MAX];
    size_t n;
} Packet;
static void u8(Packet *p, unsigned v) {
    p->b[p->n++] = (uint8_t)v;
}
static void u16(Packet *p, unsigned v) {
    u8(p, v);
    u8(p, v >> 8);
}
static void str(Packet *p, const char *s) {
    size_t n = strnlen(s, SST_STR - 1);
    u8(p, n);
    memcpy(p->b + p->n, s, n);
    p->n += n;
}
static unsigned rd16(const uint8_t *b) {
    return b[0] | (unsigned)b[1] << 8;
}
static uint32_t rd32(const uint8_t *b) {
    return rd16(b) | (uint32_t)rd16(b + 2) << 16;
}
static bool rdstr(const uint8_t *b, size_t n, size_t *off, char out[SST_STR]) {
    if (*off >= n)
        return false;
    unsigned k = b[(*off)++];
    if (k >= SST_STR || k > n - *off || memchr(b + *off, 0, k))
        return false;
    memcpy(out, b + *off, k);
    out[k] = 0;
    *off += k;
    return true;
}
static void queue(Peer *p, unsigned type, const uint8_t *b, size_t n) {
    if (p->fd == SST_INVALID_SOCKET || p->closing)
        return;
    if (p->tx_n + 5 + n > sizeof p->tx) {
        p->closing = true;
        return;
    }
    uint8_t *w = p->tx + p->tx_n;
    uint32_t len = n + 1;
    for (int i = 0; i < 4; i++)
        w[i] = (uint8_t)(len >> (8 * i));
    w[4] = type;
    memcpy(w + 5, b, n);
    p->tx_n += 5 + n;
}
static void packet(Peer *p, unsigned type, const Packet *b) {
    queue(p, type, b->b, b->n);
}
static void fail(Peer *p, const char *why) {
    sst_debug_log(SST_DBG_HOST,"host refused/closed player %u",p->id);
    Packet b = {0};
    str(&b, why);
    packet(p, HOST_ERROR, &b);
    p->closing = true;
}
static void udp_context(Peer *to, const Peer *source) {
    uint8_t b[10] = {source->id, source->id>>8};
    sst_udp_put32(b+2,source->udp_cookie); sst_udp_put32(b+6,source->udp_scene);
    queue(to,SST_UDP_SCENE,b,sizeof b);
}
static bool udp_live(Peer *p, uint64_t now) {
    return p->udp_ready && p->udp_scene_ready && p->udp_address.sin_port &&
        now-p->udp_at<SST_UDP_TIMEOUT;
}
static bool udp_send(Host *h, Peer *to, unsigned type, const uint8_t *body, size_t n) {
    uint8_t out[SST_UDP_MAX];
    size_t len=sst_udp_pack(out,to->udp_token,++to->udp_tx_seq,type,body,n);
    if (!len) return false;
    return sst_socket_sendto(h->udp,out,len,(void *)&to->udp_address,sizeof to->udp_address)==(ptrdiff_t)len;
}
static void forward(Host *h, Peer *from, unsigned type, const uint8_t *body, size_t n) {
    Packet b = {0};
    u16(&b, from->id);
    memcpy(b.b + b.n, body, n);
    b.n += n;
    for (int i = 0; i < CONNECTIONS; i++) {
        Peer *to = &h->peer[i];
        if (to == from || !to->id || to->fd == SST_INVALID_SOCKET)
            continue;
        if (type == MOTION && to->tx_n >= TX_MAX / 2 && !udp_live(to,sst_now_ms())) continue;
        if ((type == POSE || type == ENEMY || type == MOTION || type == PROP || type == PROPS_ALL) && (strcmp(to->scene.stage, from->scene.stage) ||
                             to->scene.room != from->scene.room || to->scene.layer != from->scene.layer))
            continue;
        if ((type == PROP || type == PROPS_ALL) && strcmp(to->team, from->team)) continue;
        if ((type==POSE || type==MOTION) && from->udp_scene_ready && udp_live(to,sst_now_ms())) {
            uint8_t live[128];
            live[0]=from->id; live[1]=from->id>>8;
            sst_udp_put32(live+2,from->udp_scene); sst_udp_put32(live+6,to->udp_scene);
            memcpy(live+10,body,n);
            if (sst_udp_spend(&to->udp_budget,sst_now_ms(),SST_UDP_HEADER+10+n))
                udp_send(h,to,type,live,10+n);
            // Movement is replaceable. Rate limiting must not spill into TCP.
        } else packet(to, type, &b);
    }
}
static Packet scene_packet(const Peer *p) {
    Packet b = {0};
    u16(&b, p->id);
    str(&b, p->scene.stage);
    u8(&b, p->scene.room);
    u8(&b, p->scene.layer);
    u8(&b, p->scene.entrance);
    return b;
}
static bool motion_eligible(const Peer *p, uint64_t now) {
    return p->fd != SST_INVALID_SOCKET && p->id && !p->closing && p->has_pose &&
        sst_motion_eligible(p->pose[34],rd16(p->pose+30),p->last_pose,now);
}
static bool motion_owner(Host *h, Peer *p) {
    uint64_t now = sst_now_ms();
    if (!motion_eligible(p, now)) return false;
    SstMotionCandidate candidates[CONNECTIONS];int count=0;
    for (int i = 0; i < CONNECTIONS; i++) {
        Peer *o = &h->peer[i];
        if(!motion_eligible(o,now))continue;
        candidates[count++]=(SstMotionCandidate){.id=o->id,.team=o->team,.scene=&o->scene,
            .pose_ms=o->last_pose,.pose_flags=o->pose[34],.health=rd16(o->pose+30)};
    }
    return sst_motion_pick_owner(candidates,count,p->team,&p->scene,now)==p->id;
}
static bool prop_eligible(const Peer *p, const SstProp *prop, uint64_t now) {
    int32_t ticks = (int32_t)(rd32(p->pose) - prop->pose_tick);
    return p->has_pose && p->id && p->fd != SST_INVALID_SOCKET && !p->closing &&
        now >= p->last_pose && now - p->last_pose <= SST_PROP_FRESH_MS &&
        sst_udp_pose_valid(p->pose, sizeof p->pose) && rd16(p->pose + 30) > 0 && !(p->pose[34] & 0xc0) &&
        ticks >= -45 && ticks <= 45 && sst_world_same_area(&p->scene, &prop->scene) &&
        !sst_world_boss_stage(prop->scene.stage);
}
static bool prop_rate(Peer *p, SstPropPhase phase, uint64_t now) {
    if (now < p->prop_rate_at || now - p->prop_rate_at >= 1000) {
        p->prop_rate_at = now; p->prop_carries = p->prop_places = 0;
    }
    unsigned *count = phase == SST_PROP_PLACE ? &p->prop_places : &p->prop_carries;
    unsigned limit = phase == SST_PROP_PLACE ? 16 : 30;
    if (*count >= limit) return false;
    (*count)++;
    return true;
}
static void pickup_prune(Host *h) {
    SstScene occupied[CONNECTIONS]; size_t count = 0;
    for (unsigned i = 0; i < CONNECTIONS; i++) {
        Peer *p = &h->peer[i];
        if (p->id && p->fd != SST_INVALID_SOCKET && !p->closing && p->scene.stage[0])
            occupied[count++] = p->scene;
    }
    sst_pickup_host_prune(&h->pickups, occupied, count);
}
static void pickup_snapshot(const SstPickupCommit *commit, void *user) {
    Peer *to = user; uint8_t body[SST_PICKUP_WIRE_MAX];
    size_t size = sst_pickup_commit_encode(commit, body, sizeof body);
    if (size) queue(to, PICKUP, body, size);
}
static bool pickup_eligible(const Peer *p, const SstPickup *pickup, uint64_t now) {
    return p->has_pose && p->id && p->fd != SST_INVALID_SOCKET && !p->closing &&
        now >= p->last_pose && now - p->last_pose <= 5000 &&
        sst_udp_pose_valid(p->pose, sizeof p->pose) && rd16(p->pose + 30) > 0 &&
        !(p->pose[34] & 0xc0) && sst_world_same_area(&p->scene, &pickup->scene) &&
        !sst_world_boss_stage(pickup->scene.stage);
}
static void pickup_request(Host *h, Peer *p, const uint8_t *body, size_t size) {
    SstPickup pickup; SstPickupCommit commit;
    if (!sst_pickup_decode(body, size, &pickup)) { fail(p, "Invalid pickup request"); return; }
    if (!pickup.sequence || !pickup_eligible(p, &pickup, sst_now_ms())) return;
    if (p->pickup_seen) {
        uint32_t delta = pickup.sequence - p->pickup_last.sequence;
        if (!delta) {
            if (!sst_pickup_key_equal(&pickup, p->id, &p->pickup_last, p->id) ||
                pickup.item != p->pickup_last.item) return;
        } else if (delta >= 0x80000000u) return;
    }
    p->pickup_seen = true; p->pickup_last = pickup;
    SstPickupClaim result = sst_pickup_host_claim(&h->pickups, p->id, &pickup, &commit);
    if (result != SST_PICKUP_CLAIM_NEW && result != SST_PICKUP_CLAIM_DUPLICATE) return;
    if (result == SST_PICKUP_CLAIM_DUPLICATE) {
        // The original gain is never replayed, including retries from its collector.
        pickup_snapshot(&commit, p);
        return;
    }
    uint8_t encoded[SST_PICKUP_WIRE_MAX];
    size_t n = sst_pickup_commit_encode(&commit, encoded, sizeof encoded);
    if (!n) return;
    for (unsigned i = 0; i < CONNECTIONS; i++) {
        Peer *to = &h->peer[i];
        // A live team gain is awarded across areas, including its collector ACK.
        if (to->id && to->fd != SST_INVALID_SOCKET && !strcmp(to->team, p->team))
            queue(to, PICKUP, encoded, n);
    }
}
static void drop(Host *h, Peer *p) {
    if (p->fd == SST_INVALID_SOCKET)
        return;
    if (p->id) {
        sst_debug_log(SST_DBG_HOST,"player %u disconnected",p->id);
        Packet b = {0};
        u16(&b, p->id);
        for (int i = 0; i < CONNECTIONS; i++)
            if (&h->peer[i] != p && h->peer[i].id)
                packet(&h->peer[i], LEAVE, &b);
    }
    sst_socket_close(p->fd);
    memset(p, 0, sizeof *p);
    p->fd = SST_INVALID_SOCKET;
    p->catchup = -1;
    pickup_prune(h);
}
static void hello(Host *h, Peer *p, const uint8_t *b, size_t n) {
    char room[SST_STR];
    size_t off = 2;
    if (n < 2 || rd16(b) != SST_PROTO) {
        fail(p, "Update the mod: protocol mismatch");
        return;
    }
    if (!rdstr(b, n, &off, p->name) || !rdstr(b, n, &off, room) || !rdstr(b, n, &off, p->team) ||
        n - off != 16) {
        fail(p, "Invalid join request");
        return;
    }
    // The UI labels an empty Room as public. Accept either spelling of that
    // default without changing wire strings or case-sensitive custom rooms.
    if (strcmp(room[0] ? room : "public", h->room[0] ? h->room : "public")) {
        fail(p, "Room does not match the host");
        return;
    }
    memcpy(p->session, b + off, 16);
    int count = 0;
    for (int i = 0; i < CONNECTIONS; i++) {
        Peer *o = &h->peer[i];
        if (o == p || o->fd == SST_INVALID_SOCKET || !o->id)
            continue;
        if (!memcmp(o->session, p->session, 16)) {
            p->id = o->id;
            o->id = 0;
            drop(h, o);
            continue;
        }
        count++;
        if (strcmp(o->team, p->team)) {
            fail(p, "Team must match the host");
            return;
        }
    }
    if (count >= SST_COOP_PLAYERS) {
        fail(p, "Session full (4 players)");
        return;
    }
    if (!p->id)
        for (int i = 0; i < HISTORY; i++)
            if (h->history[i].id && !memcmp(h->history[i].session, p->session, 16)) {
                p->id = h->history[i].id;
                break;
            }
    if (!p->id) {
        p->id = h->next_id++;
        if (!h->next_id)
            h->next_id = 1;
        Identity *identity = &h->history[h->next_history++ % HISTORY];
        memcpy(identity->session, p->session, 16);
        identity->id = p->id;
    }
    Packet w = {0};
    sst_debug_log(SST_DBG_HOST,"welcomed player %u (%d already joined)",p->id,count);
    u16(&w, p->id);
    u8(&w, count);
    // WELCOME's legacy list fits in FRAME_MAX for three peers only if names are short.
    // Send an empty list, then JOIN + complete SCENE messages, which also preserve layer/entrance.
    w.b[2] = 0;
    packet(p, WELCOME, &w);
    if (h->udp!=SST_INVALID_SOCKET && sst_socket_random(p->udp_token,16)) {
        p->udp_offered=true;
        queue(p,SST_UDP_OFFER,p->udp_token,16);
    }
    Packet joined = {0};
    u16(&joined, p->id);
    str(&joined, p->name);
    str(&joined, p->team);
    for (int i = 0; i < CONNECTIONS; i++) {
        Peer *o = &h->peer[i];
        if (o == p || o->fd == SST_INVALID_SOCKET || !o->id)
            continue;
        Packet j = {0};
        u16(&j, o->id);
        str(&j, o->name);
        str(&j, o->team);
        packet(p, JOIN, &j);
        Packet s = scene_packet(o);
        packet(p, SCENE, &s);
        if (o->udp_scene_ready) udp_context(p,o);
        if (o->has_pose && sst_now_ms() - o->last_pose < 1000) {
            Packet pose = {0};
            u16(&pose, o->id);
            memcpy(pose.b + pose.n, o->pose, 36);
            pose.n += 36;
            packet(p, POSE, &pose);
        }
        packet(o, JOIN, &joined);
    }
}
static void receive_frame(Host *h, Peer *p, unsigned type, const uint8_t *b, size_t n) {
    if (!p->id) {
        if (type == HELLO)
            hello(h, p, b, n);
        else
            fail(p, "Expected join request");
        return;
    }
    switch (type) {
    case SST_UDP_READY:
        if (n==4 && p->udp_offered) {
            uint32_t probe=rd32(b);
            p->udp_ready=probe && (int32_t)(p->udp_probe-probe)>=0 && p->udp_probe-probe<4 &&
                sst_now_ms()-p->udp_at<SST_UDP_TIMEOUT;
            sst_udp_feedback(&p->udp_budget,p->udp_ready);
        }
        break;
    case SST_UDP_SCENE:
        if (n==4 && p->udp_offered) {
            p->udp_cookie=rd32(b); p->udp_scene_ready=true;
            for (int i=0;i<CONNECTIONS;i++) if (h->peer[i].id) udp_context(&h->peer[i],p);
        }
        break;
    case PING:
        if (n == 4)
            queue(p, PONG, b, n);
        else
            fail(p, "Invalid ping");
        break;
    case SCENE: {
        SstScene scene = {0};
        size_t off = 0;
        if (!rdstr(b, n, &off, scene.stage) || n - off != 3) {
            fail(p, "Invalid scene");
            break;
        }
        scene.room = b[off];
        scene.layer = b[off + 1];
        scene.entrance = b[off + 2];
        p->scene = scene;
        p->has_pose = false;
        p->prop_rate_at = 0; p->prop_carries = p->prop_places = 0;
        p->udp_scene=++h->scene_serial;
        p->udp_scene_ready=false;
        forward(h, p, type, b, n);
        pickup_prune(h);
        if (sst_pickup_host_begin(&h->pickups, &scene))
            sst_pickup_host_snapshots(&h->pickups, &scene, pickup_snapshot, p);
        break;
    }
    case POSE:
        if (n != 36) {
            fail(p, "Invalid pose");
            break;
        }
        memcpy(p->pose, b, n);
        p->has_pose = true;
        p->last_pose = sst_now_ms();
        forward(h, p, type, b, n);
        break;
    case FLAG:
        if (n != 4 || b[0] >= 16) {
            fail(p, "Invalid shared flag");
            break;
        }
        {
            unsigned index = rd16(b + 1);
            if (b[0] == 0 && (sst_chest_quest_reserved(index) || sst_chest_trial_reserved(index))) break;
            if (b[0] == 1 && (index == 180 || sst_chest_pouch_expansion(index) || sst_chest_pouch_quantity(index) || sst_chest_wallet(index))) break; // receipt/pouch transactions only
            if (b[0] == 2 && index < SST_SCENE_CHESTS &&
                (sst_chest_key_piece(SST_TBOX_CHESTS + index) || sst_chest_heart_pickup(SST_TBOX_CHESTS + index) ||
                 sst_chest_crystal_pickup(SST_TBOX_CHESTS + index) ||
                 sst_chest_saved_reward(SST_TBOX_CHESTS + index) ||
                 h->chests[SST_TBOX_CHESTS + index])) break;
            unsigned k = (unsigned)b[0] * 65536 + rd16(b + 1);
            uint8_t v = h->flags[k] | b[3];
            if (v != h->flags[k]) {
                h->flags[k] = v;
                uint8_t merged[] = {b[0], b[1], b[2], v};
                forward(h, p, type, merged, 4);
            }
        }
        break;
    case WORLD_REQUEST:
        if ((n != 0 && n != 4) || (n == 4 && !rd32(b))) { fail(p, "Invalid world request"); break; }
        if (n == 4) { p->world_cookie = rd32(b); p->catchup = 0; }
        if (p->catchup < 0) p->catchup = 0;
        for (unsigned i = 0; i < SST_CHESTS; i++) if (h->chests[i]) {
            Packet reward = {0}; u16(&reward, 0); u16(&reward, i); u16(&reward, h->chests[i]);
            packet(p, CHEST, &reward);
        }
        // Recovery needs a fresh safety sample from teammates in OTHER areas too. Ordinary
        // streaming remains area scoped; this is a requested, one-off presence snapshot.
        for (int i = 0; i < CONNECTIONS; i++) {
            Peer *o = &h->peer[i];
            if (o == p || !o->id || o->fd == SST_INVALID_SOCKET || !o->has_pose || sst_now_ms() - o->last_pose >= 1000) continue;
            Packet pose = {0}; u16(&pose, o->id);
            memcpy(pose.b + pose.n, o->pose, 36); pose.n += 36;
            packet(p, POSE, &pose);
        }
        sst_pickup_host_snapshots(&h->pickups, &p->scene, pickup_snapshot, p);
        break;
    case CHEST: {
        if (n != 4) { fail(p, "Invalid chest reward"); break; }
        SstChestReward reward = {.index = rd16(b), .item = rd16(b + 2)};
        if (!sst_chest_valid(&reward)) { fail(p, "Unsupported chest reward"); break; }
        // A location has one immutable reward. Never OR item IDs or replace cached rewards.
        if (!h->chests[reward.index]) {
            h->chests[reward.index] = reward.item;
            unsigned quest = sst_chest_quest_flag(reward.index);
            if (quest) h->flags[quest] = 0;
            else if (reward.index >= SST_TBOX_CHESTS)
                h->flags[2 * 65536 + reward.index - SST_TBOX_CHESTS] = 0;
            forward(h, p, CHEST, b, n);
        }
        break;
    }
    case RESYNC:
        if (n) { fail(p, "Invalid resync request"); break; }
        if (sst_now_ms() - p->last_request >= 1000) {
            p->last_request = sst_now_ms();
            forward(h, p, RESYNC, b, n);
        }
        break;
    case ENEMY: {
        char stage[SST_STR]; size_t off = 0;
        if (!rdstr(b, n, &off, stage) || n - off != 21 || b[n - 1] > 1) {
            fail(p, "Invalid enemy event"); break;
        }
        if (!strcmp(stage, p->scene.stage) && b[off] == p->scene.room && b[off + 1] == p->scene.layer)
            forward(h, p, ENEMY, b, n);
        break;
    }
    case MOTION: {
        SstEnemyMotion m = {0}; size_t off = 0;
        if (!rdstr(b, n, &off, m.scene.stage) || n - off != 46) { fail(p, "Invalid enemy movement"); break; }
        m.scene.room = b[off++]; m.scene.layer = b[off++];
        m.key = rd32(b + off) | (uint64_t)rd32(b + off + 4) << 32; off += 8;
        m.sequence = rd32(b + off); off += 4; m.profile = rd16(b + off); off += 2;
        memcpy(m.pos, b + off, 12); off += 12;
        memcpy(m.rot, b + off, 6); off += 6; memcpy(m.velocity, b + off, 12);
        if (sst_motion_valid(&m) && sst_world_same_area(&m.scene, &p->scene) && motion_owner(h, p))
            forward(h, p, MOTION, b, n);
        break;
    }
    case PICKUP:
        pickup_request(h, p, b, n);
        break;
    case PROP:
    case PROPS_ALL: {
        SstProp prop;
        if (n > SST_PROP_WIRE_MAX || !sst_prop_decode(b, n, &prop) ||
            (type == PROP && prop.key.profile != 0x1f0)) {
            fail(p, "Invalid prop event"); break;
        }
        uint64_t now = sst_now_ms();
        if (prop_eligible(p, &prop, now) && prop_rate(p, prop.phase, now))
            forward(h, p, type, b, n); // reliable live event only; never enters flags/chest/world caches
        break;
    }
    default:
        break;
    }
}
// Bounded work per poll prevents an unauthenticated UDP flood from starving TCP recovery.
static void receive_udp(Host *h) {
    for (int attempt=0;attempt<64;attempt++) {
        uint8_t b[SST_UDP_MAX+1]; struct sockaddr_in address={0}; SstSocklen len=sizeof address;
        ptrdiff_t n=sst_socket_recvfrom(h->udp,b,sizeof b,(void *)&address,&len);
        if (n<0) break;
        if (n<SST_UDP_HEADER || n>SST_UDP_MAX || len!=sizeof address) continue;
        Peer *p=NULL;
        for (int i=0;i<CONNECTIONS;i++) {
            Peer *candidate=&h->peer[i];
            if (candidate->id && !candidate->closing && candidate->udp_offered &&
                address.sin_addr.s_addr==candidate->tcp_address.sin_addr.s_addr &&
                sst_udp_valid(b,(size_t)n,candidate->udp_token,
                              b[24] ? candidate->udp_rx_seq : candidate->udp_probe_seq)) { p=candidate; break; }
        }
        if (!p) continue;
        uint8_t type=b[24]; size_t size=(size_t)n-SST_UDP_HEADER; const uint8_t *body=b+SST_UDP_HEADER;
        uint64_t now=sst_now_ms();
        if (!type) {
            if (size!=4 || !rd32(body) || now-p->udp_probe_at<100) continue;
            p->udp_probe_seq=rd32(b+20); p->udp_probe_at=now; p->udp_at=now;
            p->udp_address=address; p->udp_probe=rd32(body);
            udp_send(h,p,0,body,size);
        } else if ((type==POSE || type==MOTION) && p->udp_scene_ready && size>=4 &&
                   rd32(body)==p->udp_scene && address.sin_port==p->udp_address.sin_port) {
            // Check sizes before calling the reliable parser: malformed datagrams are discarded,
            // never converted to TCP errors or disconnects.
            size_t payload=size-4;
            if (type==POSE && (!sst_udp_pose_valid(body+4,payload) ||
                (p->has_pose && (int32_t)(rd32(body+4)-rd32(p->pose))<=0))) continue;
            if (type==MOTION && (payload<1 || body[4]>=SST_STR || payload!=(size_t)body[4]+47 ||
                                memchr(body+5,0,body[4]))) continue;
            p->udp_rx_seq=rd32(b+20); p->udp_at=now;
            receive_frame(h,p,type,body+4,payload);
        }
    }
}
static void catchup(Host *h, Peer *p) {
    while (p->catchup >= 0 && (unsigned)p->catchup < sizeof h->flags && p->tx_n < TX_MAX / 2) {
        unsigned k = p->catchup++;
        uint8_t v = h->flags[k];
        if (!v)
            continue;
        uint8_t b[] = {0, 0, k >> 16, k, k >> 8, v};
        queue(p, FLAG, b, sizeof b);
    }
    if (p->catchup == (int)sizeof h->flags) {
        p->catchup = -1;
        if (p->world_cookie) {
            uint8_t b[4]; sst_udp_put32(b, p->world_cookie);
            queue(p, WORLD_DONE, b, sizeof b); p->world_cookie = 0;
        }
    }
}
static void *serve(void *arg) {
    Host *h = arg;
    while (atomic_load(&running)) {
        SstPoll f[CONNECTIONS + 2] = {{h->listener, POLLIN, 0}};
        f[CONNECTIONS+1]=(SstPoll){h->udp,POLLIN,0};
        for (int i = 0; i < CONNECTIONS; i++) {
            Peer *p = &h->peer[i];
            if (p->fd != SST_INVALID_SOCKET && !p->closing)
                catchup(h, p);
            f[i + 1] = (SstPoll){p->fd, POLLIN | (p->tx_n ? POLLOUT : 0), 0};
        }
        if (sst_socket_poll(f, CONNECTIONS + 2, 10) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (f[CONNECTIONS+1].revents & POLLIN) receive_udp(h);
        for (int i = 0; i < CONNECTIONS; i++) {
            Peer *p = &h->peer[i];
            short ev = f[i + 1].revents;
            if (p->fd == SST_INVALID_SOCKET)
                continue;
            if (ev & POLLOUT) {
                ptrdiff_t n = sst_socket_send(p->fd, p->tx, p->tx_n);
                if (n > 0) {
                    p->tx_n -= n;
                    memmove(p->tx, p->tx + n, p->tx_n);
                } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    drop(h, p);
                    continue;
                }
            }
            if ((ev & POLLIN) && !p->closing) {
                ptrdiff_t n = sst_socket_recv(p->fd, p->rx + p->rx_n, sizeof p->rx - p->rx_n);
                if (n <= 0) {
                    if (!n || (errno != EAGAIN && errno != EINTR))
                        drop(h, p);
                    continue;
                }
                p->rx_n += n;
                p->last_rx = sst_now_ms();
                while (p->rx_n >= 4 && !p->closing) {
                    uint32_t len = rd32(p->rx);
                    if (!len || len > FRAME_MAX) {
                        fail(p, "Invalid packet size");
                        break;
                    }
                    if (p->rx_n < len + 4)
                        break;
                    receive_frame(h, p, p->rx[4], p->rx + 5, len - 1);
                    p->rx_n -= 4 + len;
                    memmove(p->rx, p->rx + 4 + len, p->rx_n);
                }
            }
            if (p->fd != SST_INVALID_SOCKET && ((ev & (POLLERR | POLLHUP | POLLNVAL)) || (p->closing && !p->tx_n) ||
                               sst_now_ms() - p->last_rx > (p->id ? 30000 : 5000)))
                drop(h, p);
        }
        if (f[0].revents & POLLIN) {
            SstSocket fd = sst_socket_accept(h->listener);
            if (fd == SST_INVALID_SOCKET)
                continue;
            int one = 1;
            sst_socket_option(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            int i;
            for (i = 0; i < CONNECTIONS; i++)
                if (h->peer[i].fd == SST_INVALID_SOCKET)
                    break;
            if (i == CONNECTIONS)
                sst_socket_close(fd);
            else {
                Peer *p = &h->peer[i];
                memset(p, 0, sizeof *p);
                p->fd = fd;
                p->catchup = -1;
                p->last_rx = sst_now_ms();
                SstSocklen len=sizeof p->tcp_address;
                if (getpeername(fd,(void *)&p->tcp_address,&len)) p->tcp_address.sin_addr.s_addr=0;
            }
        }
    }
    for (int i = 0; i < CONNECTIONS; i++)
        if (h->peer[i].fd != SST_INVALID_SOCKET)
            sst_socket_close(h->peer[i].fd);
    return NULL;
}

bool sst_host_start(uint16_t port, const char *room, char *why, size_t cap) {
    int code;
    sst_host_stop();
    struct in_addr bind_address={.s_addr=htonl(INADDR_ANY)};
    const char *bind=getenv("SST_HOST_BIND");
    if(bind && *bind && inet_pton(AF_INET,bind,&bind_address)!=1) {
        if(why && cap)snprintf(why,cap,"Invalid SST_HOST_BIND IPv4 address");
        sst_debug_log(SST_DBG_HOST,"invalid explicit host bind address");return false;
    }
    if (!sst_socket_init()) { if (cap) snprintf(why,cap,"Could not initialize networking: %s",strerror(errno)); return false; }
    Host *h = calloc(1, sizeof *h);
    if (!h) {
        if (cap)
            snprintf(why, cap, "Not enough memory");
        return false;
    }
    h->udp=SST_INVALID_SOCKET;
    h->listener = sst_socket_open(AF_INET, SOCK_STREAM, 0);
    if (h->listener == SST_INVALID_SOCKET)
        goto error;
    int one = 1;
#ifdef _WIN32
    if (sst_socket_option(h->listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,&one,sizeof one)) goto error;
#else
    if (sst_socket_option(h->listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one)) goto error;
#endif
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_addr = bind_address, .sin_port = htons(port)};
    if (sst_socket_bind(h->listener, (void *)&addr, sizeof addr) < 0 || sst_socket_listen(h->listener, CONNECTIONS) < 0)
        goto error;
    SstSocklen len = sizeof addr;
    if (sst_socket_name(h->listener, (void *)&addr, &len) < 0)
        goto error;
    h->udp=sst_socket_open(AF_INET,SOCK_DGRAM,0);
#ifdef _WIN32
    if (h->udp!=SST_INVALID_SOCKET)
        sst_socket_option(h->udp,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,&one,sizeof one);
#endif
    // UDP is optional: another application/firewall may leave only TCP usable.
    if (h->udp!=SST_INVALID_SOCKET && sst_socket_bind(h->udp,(void *)&addr,sizeof addr)<0) {
        sst_socket_close(h->udp); h->udp=SST_INVALID_SOCKET;
    }
    for (int i = 0; i < CONNECTIONS; i++)
        h->peer[i].fd = SST_INVALID_SOCKET;
    snprintf(h->room, sizeof h->room, "%s", room ? room : "");
    h->next_id = 1;
    atomic_store(&running, true);
    int err = pthread_create(&worker, NULL, serve, h);
    if (err) {
        errno = err;
        atomic_store(&running, false);
        goto error;
    }
    host = h;
    bound_port = ntohs(addr.sin_port);
    if(!inet_ntop(AF_INET,&addr.sin_addr,bound_address,sizeof bound_address))bound_address[0]=0;
    sst_debug_log(SST_DBG_HOST,"hosting on TCP port %u; UDP %s",bound_port,h->udp!=SST_INVALID_SOCKET?"available":"unavailable");
    if (cap)
        why[0] = 0;
    return true;
error:
    code=errno;
    sst_debug_log(SST_DBG_HOST,"host listener setup failed");
    if (cap)
        snprintf(why, cap, "Could not host on port %u: %s", port, strerror(code));
    if (h->listener != SST_INVALID_SOCKET)
        sst_socket_close(h->listener);
    if (h->udp!=SST_INVALID_SOCKET) sst_socket_close(h->udp);
    free(h);
    return false;
}
void sst_host_stop(void) {
    if (!host)
        return;
    atomic_store(&running, false);
    pthread_join(worker, NULL);
    sst_socket_close(host->listener);
    if (host->udp!=SST_INVALID_SOCKET) sst_socket_close(host->udp);
    free(host);
    host = NULL;
    bound_port = 0;
    bound_address[0]=0;sst_debug_log(SST_DBG_HOST,"hosting stopped");
}
uint16_t sst_host_port(void) {
    return bound_port;
}
const char *sst_host_address(void) {return bound_address;}
