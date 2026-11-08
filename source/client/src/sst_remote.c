#include "sst_remote.h"
#include "sst_pose_buffer.h"
#include "sst_animation.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
typedef struct {
    uint16_t id;
    uint32_t presence_generation;
    bool used;
    SstScene scene;
    SstPoseBuffer poses;
} Slot;
static Slot slots[SST_MAX_PLAYERS];
static SstScene last_local;
static bool frame_mode, frame_ready;
static uint64_t frame_id, frame_at;
static uint32_t frame_generation;
static SstScene frame_scene;
static pthread_mutex_t display_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    SstScene scene;
    SstRemote peers[SST_MAX_PLAYERS];
    uint32_t presence[SST_MAX_PLAYERS], generation;
    int count;
    uint64_t at;
    uint64_t source_at[SST_MAX_PLAYERS];
    bool source_carry[SST_MAX_PLAYERS]; // latest sampled packet, independent of playout delay
} display;
static bool carrying(const SstPose *p);
static struct {
    bool valid;
    uint16_t id;
    uint32_t presence, generation;
    SstScene scene;
    SstPose pose;
    uint64_t at, source_at;
} carry_draws[SST_MAX_PLAYERS];
void sst_remote_reset(void) {
    // Once scheduling owns sampling, reset must not revive legacy lazy sampling.
    frame_ready = false;
    memset(slots, 0, sizeof slots);
    memset(&last_local, 0, sizeof last_local);
    pthread_mutex_lock(&display_mu);
    memset(&display, 0, sizeof display);
    memset(carry_draws, 0, sizeof carry_draws);
    pthread_mutex_unlock(&display_mu);
}
static bool same_scene(const SstScene *a, const SstScene *b) {
    return !strcmp(a->stage, b->stage) && a->room == b->room && a->layer == b->layer;
}
static bool same_frame_scene(const SstScene *a, const SstScene *b) {
    return same_scene(a,b) && a->entrance == b->entrance;
}
static bool valid_pose(const SstPose *p) {
    for (int i = 0; i < 3; i++)
        if (!isfinite(p->pos[i]) || fabsf(p->pos[i]) > 1e6f)
            return false;
    return isfinite(p->anim_frame) && p->max_health <= 80 && p->health <= p->max_health;
}
int sst_remote_sample(const SstScene *local, const SstPlayer *ps, int count, double now, SstRemote *out,
                      int cap) {
    if (!local || !local->stage[0]) {
        sst_remote_reset();
        return 0;
    }
    if (!same_scene(local, &last_local)) {
        sst_remote_reset();
        last_local = *local;
    }
    bool live[SST_MAX_PLAYERS] = {0};
    int n = 0;
    for (int j = 0; j < SST_MAX_PLAYERS; j++)
        if (slots[j].used) {
            bool found = false;
            for (int i = 0; i < count; i++)
                if (ps[i].active && ps[i].id == slots[j].id)
                    found = true;
            if (!found)
                slots[j].used = false;
        }
    for (int i = 0; i < count && n < cap; i++) {
        const SstPlayer *p = &ps[i];
        if (!p->active || !p->has_pose || !same_scene(local, &p->scene) || now < p->pose_time_ms ||
            now - p->pose_time_ms > 1000 || !valid_pose(&p->pose))
            continue;
        int j;
        for (j = 0; j < SST_MAX_PLAYERS; j++)
            if (slots[j].used && slots[j].id == p->id)
                break;
        if (j == SST_MAX_PLAYERS)
            for (j = 0; j < SST_MAX_PLAYERS; j++)
                if (!slots[j].used)
                    break;
        if (j == SST_MAX_PLAYERS)
            continue;
        Slot *s = &slots[j];
        if (!s->used || s->presence_generation != p->presence_generation || !same_scene(&s->scene, &p->scene)) {
            memset(s, 0, sizeof *s);
            s->used = true;
            s->id = p->id;
            s->presence_generation = p->presence_generation;
            s->scene = p->scene;
            sst_pose_buffer_reset(&s->poses);
        }
        live[j] = true;
        sst_pose_buffer_push(&s->poses, &p->pose, p->pose_time_ms);
        float length = 0;
        bool loop = false;
        sst_animation_clip(p->pose.anim_id, &length, &loop);
        sst_pose_buffer_animation(&s->poses, p->pose.anim_id, length, loop);
        SstPose smooth;
        if (!sst_pose_buffer_sample(&s->poses, now, &smooth))
            continue;
        out[n].id = p->id;
        out[n].presence_generation = p->presence_generation;
        snprintf(out[n].name, sizeof out[n].name, "%s", p->name);
        out[n++].pose = smooth;
    }
    for (int j = 0; j < SST_MAX_PLAYERS; j++)
        if (!live[j])
            slots[j].used = false;
    return n;
}

static int network_sample(const SstScene *local, SstRemote *out, int cap) {
    static uint32_t last_generation;
    uint32_t generation = sst_net_generation();
    if (sst_net_status(NULL, 0) != SST_ONLINE) {
        sst_remote_reset();
        return 0;
    }
    if (generation != last_generation) {
        sst_remote_reset();
        last_generation = generation;
    }
    SstPlayer ps[SST_MAX_PLAYERS];
    int n = sst_net_players(ps, SST_MAX_PLAYERS);
    uint64_t now = sst_now_ms();
    int count = sst_remote_sample(local, ps, n, now, out, cap);
    pthread_mutex_lock(&display_mu);
    display.count = count < SST_MAX_PLAYERS ? count : SST_MAX_PLAYERS;
    display.scene = local ? *local : (SstScene){0};
    display.generation = generation; display.at = now;
    for (int i = 0; i < display.count; i++) {
        display.peers[i] = out[i]; display.presence[i] = 0; display.source_at[i] = 0;display.source_carry[i]=false;
        for (int j = 0; j < n; j++) if (ps[j].id == out[i].id) {
            display.presence[i] = ps[j].presence_generation;
            display.source_at[i] = ps[j].pose_time_ms;
            display.source_carry[i]=carrying(&ps[j].pose);
        }
    }
    pthread_mutex_unlock(&display_mu);
    return count;
}

void sst_remote_frame(const SstScene *local, uint64_t frame) {
    frame_mode = true;
    uint64_t now = sst_now_ms(); uint32_t gen = sst_net_generation();
    if (!local || !local->stage[0] || sst_net_status(NULL,0) != SST_ONLINE) {
        sst_remote_reset(); return;
    }
    if (frame_ready && frame == frame_id && gen == frame_generation &&
        same_frame_scene(local,&frame_scene) && now >= frame_at) return;
    if (frame_ready && (frame < frame_id || now < frame_at || gen != frame_generation ||
        !same_frame_scene(local,&frame_scene)))
        sst_remote_reset();
    SstRemote peers[SST_MAX_PLAYERS];
    network_sample(local,peers,SST_MAX_PLAYERS);
    frame_id=frame; frame_generation=gen; frame_scene=*local;
    frame_at=sst_now_ms(); frame_ready=true;
}

int sst_remote_network_sample(const SstScene *local, SstRemote *out, int cap) {
    if (!out || cap <= 0) return 0;
    if (cap > SST_MAX_PLAYERS) cap=SST_MAX_PLAYERS;
    if (!frame_mode) return network_sample(local,out,cap);
    uint64_t now=sst_now_ms(); uint32_t gen=sst_net_generation();
    if (!frame_ready || !local || !same_frame_scene(local,&frame_scene) ||
        gen != frame_generation || sst_net_status(NULL,0) != SST_ONLINE) return 0;
    int count=0;
    pthread_mutex_lock(&display_mu);
    if (display.generation == gen && same_frame_scene(local,&display.scene) &&
        now >= display.at && now-display.at <= 250) {
        for (int i=0;i<display.count && count<cap;i++)
            if (now >= display.source_at[i] && now-display.source_at[i] <= 1000)
                out[count++]=display.peers[i];
    }
    pthread_mutex_unlock(&display_mu);
    return count;
}

bool sst_remote_displayed(const SstScene *scene, uint16_t id, uint32_t presence, SstPose *out) {
    if (!scene || !out || !id || !presence) return false;
    uint64_t now = sst_now_ms(); uint32_t gen = sst_net_generation(); bool found = false;
    pthread_mutex_lock(&display_mu);
    if (gen == display.generation && now >= display.at && now - display.at <= 250 &&
        same_scene(scene, &display.scene)) {
        for (int i = 0; i < display.count; i++)
            if (display.peers[i].id == id && display.presence[i] == presence &&
                now >= display.source_at[i] && now-display.source_at[i] <= 1000) {
                *out = display.peers[i].pose; found = true; break;
            }
    }
    pthread_mutex_unlock(&display_mu);
    return found;
}

static bool carrying(const SstPose *p) {
    return valid_pose(p) && p->health && p->max_health && !(p->equip_flags & 0xc0) &&
        (p->equip_flags & SST_POSE_IDENTITY_MASK) == SST_POSE_CONTEXT_SMALL_CARRY;
}
static bool same_placement(const SstPose *a,const SstPose *b) {
    if(a->tick!=b->tick || a->health!=b->health || a->max_health!=b->max_health)return false;
    for(unsigned i=0;i<3;i++)if(a->pos[i]!=b->pos[i] || a->rot[i]!=b->rot[i])return false;
    return true;
}
static bool same_source_pose(const SstPose *a,const SstPose *b) {
    return same_placement(a,b) && a->anim_id==b->anim_id && a->action==b->action &&
        a->anim_frame==b->anim_frame && a->equip_flags==b->equip_flags;
}
void sst_remote_carry_drawn(const SstScene *scene,const SstRemote *source,const SstPose *rendered) {
    if(!scene || !source || !source->id || !source->presence_generation)return;
    uint64_t now=sst_now_ms();uint32_t gen=sst_net_generation();
    if(sst_net_status(NULL,0)!=SST_ONLINE)return;
    pthread_mutex_lock(&display_mu);
    if(gen!=display.generation || !same_frame_scene(scene,&display.scene) ||
       now<display.at || now-display.at>250)goto done;
    int match=-1;
    for(int i=0;i<display.count;i++)if(display.peers[i].id==source->id &&
        display.presence[i]==source->presence_generation &&
        same_source_pose(&display.peers[i].pose,&source->pose)){match=i;break;}
    if(match<0)goto done; // a late callback cannot authorize a new incarnation/frame
    int slot=-1;
    for(int i=0;i<SST_MAX_PLAYERS;i++)if(carry_draws[i].id==source->id){slot=i;break;}
    if(!rendered || !display.source_carry[match] || !carrying(&source->pose) || !carrying(rendered) ||
       !same_placement(&source->pose,rendered) || now<display.source_at[match] ||
       now-display.source_at[match]>1000) {
        if(slot>=0)carry_draws[slot].valid=false;
        goto done;
    }
    if(slot<0)for(int i=0;i<SST_MAX_PLAYERS;i++)if(!carry_draws[i].valid){slot=i;break;}
    if(slot<0) {
        slot=0;
        for(int i=1;i<SST_MAX_PLAYERS;i++)if(carry_draws[i].at<carry_draws[slot].at)slot=i;
    }
    carry_draws[slot].valid=true;carry_draws[slot].id=source->id;
    carry_draws[slot].presence=source->presence_generation;carry_draws[slot].generation=gen;
    carry_draws[slot].scene=*scene;carry_draws[slot].pose=*rendered;
    // Repeated command lists must not extend the immutable frame/source clock.
    carry_draws[slot].at=display.at;carry_draws[slot].source_at=display.source_at[match];
done:
    pthread_mutex_unlock(&display_mu);
}
bool sst_remote_carry_displayed(const SstScene *scene,uint16_t id,uint32_t presence,SstPose *out) {
    if(!scene || !out || !id || !presence)return false;
    uint64_t now=sst_now_ms();uint32_t gen=sst_net_generation();bool found=false;
    if(sst_net_status(NULL,0)!=SST_ONLINE)return false;
    pthread_mutex_lock(&display_mu);
    if(gen!=display.generation || !same_scene(scene,&display.scene) ||
       now<display.at || now-display.at>250)goto done;
    bool live=false;
    for(int i=0;i<display.count;i++)if(display.peers[i].id==id && display.presence[i]==presence &&
        display.source_carry[i] && carrying(&display.peers[i].pose) && now>=display.source_at[i] &&
        now-display.source_at[i]<=1000){live=true;break;}
    if(!live)goto done;
    for(int i=0;i<SST_MAX_PLAYERS;i++)if(carry_draws[i].valid && carry_draws[i].id==id &&
        carry_draws[i].presence==presence && carry_draws[i].generation==gen &&
        same_frame_scene(&carry_draws[i].scene,&display.scene) &&
        now>=carry_draws[i].at && now-carry_draws[i].at<=250 &&
        now>=carry_draws[i].source_at && now-carry_draws[i].source_at<=1000) {
        *out=carry_draws[i].pose;found=true;break;
    }
done:
    pthread_mutex_unlock(&display_mu);return found;
}
