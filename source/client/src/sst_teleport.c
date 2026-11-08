#include "sst_teleport.h"
#include <math.h>
#include <string.h>

static bool scene_valid(const SstScene *s) {
    if (!s) return false;
    const char *end=memchr(s->stage,0,sizeof s->stage);
    if (!end || end==s->stage || end-s->stage>7) return false;
    for (const char *p=s->stage;p<end;p++) {
        unsigned char c=(unsigned char)*p;
        if (!((c>='A' && c<='Z') || (c>='a' && c<='z') ||
              (c>='0' && c<='9') || c=='_')) return false;
    }
    return true; // native catalogue membership remains the caller's responsibility
}
static bool same_area(const SstScene *a,const SstScene *b) {
    return scene_valid(a) && scene_valid(b) && !strcmp(a->stage,b->stage) &&
        a->room==b->room && a->layer==b->layer;
}
static bool pose_valid(const SstPose *p) {
    for (unsigned i=0;i<3;i++) if (!isfinite(p->pos[i]) || fabsf(p->pos[i])>1e6f) return false;
    return p->health<=p->max_health && p->max_health<=80 && isfinite(p->anim_frame);
}
static SstTeleportDecision decision(SstTeleport *s,SstTeleportAction action,SstTeleportReason reason) {
    SstTeleportDecision d={.action=action,.reason=reason,.nonce=s?s->nonce:0};
    if (s) {s->last_action=action;s->reason=reason;}
    return d;
}
static SstTeleportDecision cancel(SstTeleport *s,SstTeleportReason reason) {
    if (s) s->active=false;
    return decision(s,SST_TELEPORT_CANCEL,reason);
}
void sst_teleport_reset(SstTeleport *s) {if (s) memset(s,0,sizeof *s);}
void sst_teleport_cancel(SstTeleport *s,SstTeleportReason reason) {
    if (s) (void)cancel(s,reason==SST_TELEPORT_REASON_NONE?SST_TELEPORT_REASON_CANCELLED:reason);
}
bool sst_teleport_begin(SstTeleport *s,const SstPlayer *p,uint32_t generation,uint64_t now) {
    if (!s) return false;
    uint32_t nonce=s->nonce+1;if (!nonce) nonce=1;
    memset(s,0,sizeof *s);s->nonce=nonce;
    if (!p || !p->active || !p->id || !scene_valid(&p->scene)) {
        s->reason=SST_TELEPORT_REASON_INVALID;s->last_action=SST_TELEPORT_CANCEL;return false;
    }
    s->active=true;s->peer_id=p->id;s->presence_generation=p->presence_generation;
    s->connection_generation=generation;s->peer_scene_generation=p->udp_scene;
    s->target=p->scene;s->began_ms=now;s->last_step_ms=now;s->last_action=SST_TELEPORT_WAIT;
    return true;
}
SstTeleportDecision sst_teleport_step(SstTeleport *s,const SstPlayer *p,uint32_t generation,
    const SstScene *local,bool ready,uint32_t instance,uint64_t now) {
    if (!s || !s->active) return decision(s,SST_TELEPORT_IDLE,s?s->reason:SST_TELEPORT_REASON_NONE);
    if (now<s->began_ms || now<s->last_step_ms) return cancel(s,SST_TELEPORT_REASON_TIMEOUT);
    s->last_step_ms=now;
    uint64_t began=s->loading?s->load_started_ms:s->began_ms;
    uint64_t timeout=s->loading?SST_TELEPORT_LOAD_TIMEOUT_MS:SST_TELEPORT_WAIT_TIMEOUT_MS;
    if (now<began || now-began>timeout) return cancel(s,SST_TELEPORT_REASON_TIMEOUT);
    if (generation!=s->connection_generation) return cancel(s,SST_TELEPORT_REASON_CONNECTION);
    if (!p || !p->active || p->id!=s->peer_id) return cancel(s,SST_TELEPORT_REASON_LEFT);
    if (p->presence_generation!=s->presence_generation) return cancel(s,SST_TELEPORT_REASON_REJOINED);
    if (!same_area(&p->scene,&s->target)) return cancel(s,SST_TELEPORT_REASON_TARGET_AREA);
    if (s->peer_scene_generation && p->udp_scene && p->udp_scene!=s->peer_scene_generation)
        return cancel(s,SST_TELEPORT_REASON_TARGET_INSTANCE);
    // A published boss encounter is deliberately not bypassed by player travel.
    if (p->has_pose && (p->pose.equip_flags&0x80)) return cancel(s,SST_TELEPORT_REASON_BOSS);
    bool fresh=p->has_pose && now>=p->pose_time_ms && now-p->pose_time_ms<=SST_TELEPORT_POSE_MAX_AGE_MS;
    if (fresh && !pose_valid(&p->pose)) return cancel(s,SST_TELEPORT_REASON_INVALID);
    if (fresh && !p->pose.health) return cancel(s,SST_TELEPORT_REASON_DEAD);
    if (s->arrived && (!same_area(local,&s->target) || instance!=s->arrived_instance))
        return cancel(s,SST_TELEPORT_REASON_LOCAL_INSTANCE);
    if (!ready || !instance || !scene_valid(local))
        return decision(s,SST_TELEPORT_WAIT,SST_TELEPORT_REASON_WAIT_LOCAL);
    if (!same_area(local,&s->target)) {
        if (s->loading) return decision(s,SST_TELEPORT_WAIT,SST_TELEPORT_REASON_WAIT_LOAD);
        // Ordinary world/flag/chest snapshots are NOT needed. Native-load the known
        // peer SCENE, then receive the normal same-area pose stream after arrival.
        SstTeleportDecision d=decision(s,SST_TELEPORT_LOAD_AREA,SST_TELEPORT_REASON_NONE);
        d.scene=s->target;return d;
    }
    if (s->loading && !s->arrived) {
        if (instance==s->before_load_instance)
            return decision(s,SST_TELEPORT_WAIT,SST_TELEPORT_REASON_WAIT_LOAD);
        s->arrived=true;s->arrived_instance=instance;s->arrived_ms=now;
        return decision(s,SST_TELEPORT_WAIT,SST_TELEPORT_REASON_WAIT_POSE);
    }
    if (!fresh || (s->loading && p->pose_time_ms<=s->arrived_ms))
        return decision(s,SST_TELEPORT_WAIT,SST_TELEPORT_REASON_WAIT_POSE);
    if ((p->pose.equip_flags&0x40) || (p->pose.action!=0 && p->pose.action!=2 && p->pose.action!=3))
        return decision(s,SST_TELEPORT_WAIT,SST_TELEPORT_REASON_WAIT_PLAYER);
    SstTeleportDecision d=decision(s,SST_TELEPORT_PLACE,SST_TELEPORT_REASON_NONE);
    d.pose=p->pose;return d;
}
bool sst_teleport_load_accepted(SstTeleport *s,uint32_t nonce,uint32_t instance) {
    if (!s || !s->active || s->loading || s->nonce!=nonce || !instance || s->last_action!=SST_TELEPORT_LOAD_AREA) return false;
    s->loading=true;s->before_load_instance=instance;s->load_started_ms=s->last_step_ms;
    s->last_action=SST_TELEPORT_WAIT;return true;
}
bool sst_teleport_complete(SstTeleport *s,uint32_t nonce) {
    if (!s || !s->active || s->nonce!=nonce || s->last_action!=SST_TELEPORT_PLACE) return false;
    s->active=false;s->reason=SST_TELEPORT_REASON_DONE;s->last_action=SST_TELEPORT_IDLE;return true;
}
const char *sst_teleport_reason_text(SstTeleportReason reason) {
    switch (reason) {
    case SST_TELEPORT_REASON_NONE:return "Teleport ready";
    case SST_TELEPORT_REASON_WAIT_LOCAL:return "Waiting for normal local gameplay";
    case SST_TELEPORT_REASON_WAIT_POSE:return "Waiting for a fresh player position";
    case SST_TELEPORT_REASON_WAIT_PLAYER:return "Waiting for the player to finish an action or menu";
    case SST_TELEPORT_REASON_WAIT_LOAD:return "Waiting for the destination area to load";
    case SST_TELEPORT_REASON_LEFT:return "Player left the session";
    case SST_TELEPORT_REASON_REJOINED:return "Player rejoined; select them again";
    case SST_TELEPORT_REASON_CONNECTION:return "Connection changed; select the player again";
    case SST_TELEPORT_REASON_TARGET_AREA:return "Player changed areas; select them again";
    case SST_TELEPORT_REASON_TARGET_INSTANCE:return "Player reloaded the area; select them again";
    case SST_TELEPORT_REASON_LOCAL_INSTANCE:return "Local area changed during teleport";
    case SST_TELEPORT_REASON_DEAD:return "Player is defeated";
    case SST_TELEPORT_REASON_INVALID:return "Player position is unavailable or invalid";
    case SST_TELEPORT_REASON_BOSS:return "Player is in a boss encounter";
    case SST_TELEPORT_REASON_TIMEOUT:return "Teleport timed out; try again after loading";
    case SST_TELEPORT_REASON_CANCELLED:return "Teleport cancelled";
    case SST_TELEPORT_REASON_DONE:return "Teleported to player";
    }
    return "Teleport unavailable";
}
