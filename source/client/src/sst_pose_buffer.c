// See sst_pose_buffer.h. Design follows Twili-Together's playout (adaptive 2-8 tick delay, bounded
// extrapolation, snap on large jumps); the code is our own.
#include "sst_pose_buffer.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#define MIN_DELAY 2.0
#define MAX_DELAY 8.0
#define MAX_EXTRAPOLATE 2.0   // ticks past the newest sample
#define SNAP_DIST 400.0f      // world units between two samples: teleport, don't slide
#define REANCHOR_TICKS 45.0   // clock drifted this far from the target: jump instead of catching up
#define CADENCE_WINDOW_MS 2000.0
#define CADENCE_MIN_SPAN_MS 150.0
#define CADENCE_MIN_TICKS 4
#define PERIOD_MIN_MS (SST_TICK_MS * .75)
#define PERIOD_MAX_MS (SST_TICK_MS * 6)
#define STALE_HISTORY_MS 1000.0 // same freshness horizon as remote-player visibility

void sst_pose_buffer_reset(SstPoseBuffer *b) {
    memset(b, 0, sizeof *b);
    b->delay_ticks = 3.0;
    b->source_period_ms = SST_TICK_MS;
}

static const SstPose *at(const SstPoseBuffer *b, int age) {  // age 0 = newest
    return &b->ring[(b->head - age + SST_POSE_RING) % SST_POSE_RING];
}
static int64_t time_at(const SstPoseBuffer *b,int age) {
    return b->timeline[(b->head-age+SST_POSE_RING)%SST_POSE_RING];
}
static SstPoseArrival *arrival_at(SstPoseBuffer *b,unsigned age) {
    return &b->cadence[(b->cadence_head+SST_CADENCE_RING-age)%SST_CADENCE_RING];
}
static void observe_cadence(SstPoseBuffer *b,int64_t tick,double now) {
    if(b->cadence_count) {
        SstPoseArrival *last=arrival_at(b,0);
        if(now<last->ms)return; // no rate evidence from a backwards local clock
        if(now==last->ms) {last->tick=tick;return;} // one timestamp for a coalesced batch
    }
    b->cadence_head=(b->cadence_head+1)%SST_CADENCE_RING;
    b->cadence[b->cadence_head]=(SstPoseArrival){tick,now};
    if(b->cadence_count<SST_CADENCE_RING)b->cadence_count++;
}
static int compare_period(const void *left,const void *right) {
    double a=*(const double *)left,c=*(const double *)right;
    return (a>c)-(a<c);
}
static void update_cadence(SstPoseBuffer *b) {
    if(!b->cadence_count)return;
    SstPoseArrival latest=*arrival_at(b,0);
    if(b->cadence_processed && latest.tick==b->processed_tick)return;
    // Long-span slopes distinguish slow observation counters from packet loss.
    // Median pair slopes and one record per arrival batch avoid giving a burst
    // or a single short inter-arrival disproportionate clock influence.
    double periods[SST_CADENCE_RING*(SST_CADENCE_RING-1)/2];unsigned count=0;
    for(unsigned old=1;old<b->cadence_count;old++) {
        SstPoseArrival a=*arrival_at(b,old);
        if(latest.ms-a.ms>CADENCE_WINDOW_MS)continue;
        for(unsigned young=0;young<old;young++) {
            SstPoseArrival c=*arrival_at(b,young);
            int64_t span=c.tick-a.tick;double elapsed=c.ms-a.ms;
            if(span<CADENCE_MIN_TICKS || elapsed<CADENCE_MIN_SPAN_MS)continue;
            double period=elapsed/(double)span;
            if(isfinite(period) && period>=PERIOD_MIN_MS && period<=PERIOD_MAX_MS)periods[count++]=period;
        }
    }
    if(count>=3) {
        qsort(periods,count,sizeof periods[0],compare_period);
        double median=count%2?periods[count/2]:(periods[count/2-1]+periods[count/2])*.5;
        b->source_period_ms+=(median-b->source_period_ms)*.2;
    }
    // Process each distinct, forward arrival once. Late packets never steer the
    // clock. Batches are represented by their final tick rather than zero gaps.
    for(unsigned age=b->cadence_count;age>0;age--) {
        SstPoseArrival p=*arrival_at(b,age-1);
        if(b->cadence_processed && p.tick<=b->processed_tick)continue;
        if(b->cadence_processed && p.ms>=b->processed_ms) {
            double expect=(double)(p.tick-b->processed_tick)*b->source_period_ms;
            double actual=p.ms-b->processed_ms;
            b->jitter_ms+=(fabs(actual-expect)-b->jitter_ms)*.1;
        }
        b->processed_tick=p.tick;b->processed_ms=p.ms;b->cadence_processed=true;
    }
}

void sst_pose_buffer_push(SstPoseBuffer *b, const SstPose *p, double now_ms) {
    if(!b || !p || !isfinite(now_ms) || now_ms<0)return;
    int64_t timeline=p->tick;
    if (b->count) {
        const SstPose *newest = at(b, 0);
        int32_t d = (int32_t)(p->tick - newest->tick);
        if (d == 0) return;
        if (d < -1000 || d > 1000 ||
            (d > 0 && now_ms-b->recv_ms[b->head]>STALE_HISTORY_MS)) {
            // A genuine new observation after the visible stream expired starts
            // fresh. Never interpolate a resumed source through obsolete history.
            sst_pose_buffer_reset(b);
        }
        else if (d < 0) {
            // late packet: insert in order if it still falls inside the ring
            int n = b->count < SST_POSE_RING ? b->count : SST_POSE_RING;
            int age = 0;
            while (age < n && (int32_t)(p->tick - at(b, age)->tick) < 0) age++;
            if (age >= n || at(b, age)->tick == p->tick) return;
            if (b->count < SST_POSE_RING) b->count++;
            // shift [0, age) one slot newer
            b->head = (b->head + 1) % SST_POSE_RING;
            for (int i = 0; i < age; i++) {
                int dst = (b->head - i + SST_POSE_RING) % SST_POSE_RING, src = (dst - 1 + SST_POSE_RING) % SST_POSE_RING;
                b->ring[dst] = b->ring[src];
                b->recv_ms[dst] = b->recv_ms[src];
                b->timeline[dst] = b->timeline[src];
            }
            int slot = (b->head - age + SST_POSE_RING) % SST_POSE_RING;
            b->ring[slot] = *p;
            b->recv_ms[slot] = now_ms;
            b->timeline[slot] = time_at(b,0)+d;
            return;
        } else {
            timeline=time_at(b,0)+d;
        }
    }
    b->head = (b->head + 1) % SST_POSE_RING;
    b->ring[b->head] = *p;
    b->recv_ms[b->head] = now_ms;
    b->timeline[b->head] = timeline;
    if (b->count < SST_POSE_RING) b->count++;
    observe_cadence(b,timeline,now_ms);
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static int16_t lerp_angle(int16_t a, int16_t b, float t) {
    int16_t d = (int16_t)(b - a);  // shortest way round
    return (int16_t)(a + (int16_t)lrintf(d * t));
}

void sst_pose_buffer_animation(SstPoseBuffer *b, uint16_t clip, float length, bool loop) {
    b->animation_clip = clip;
    b->animation_length = isfinite(length) && length > 0 && length <= 65536 ? length : 0;
    b->animation_loop = loop && b->animation_length > 0;
}

static void interpolate_animation(const SstPoseBuffer *b, const SstPose *a, const SstPose *c,
                                  double span, float t, SstPose *out) {
    if (((a->equip_flags^c->equip_flags)&SST_POSE_IDENTITY_MASK) ||
        a->anim_id != c->anim_id || a->action != c->action || !isfinite(a->anim_frame) ||
        !isfinite(c->anim_frame) || a->anim_frame < 0 || c->anim_frame < 0) return;
    float advance = c->anim_frame - a->anim_frame;
    float length = b->animation_clip == a->anim_id ? b->animation_length : 0;
    if (length) {
        if (a->anim_frame > length || c->anim_frame > length) return;
        if (advance < 0 && b->animation_loop) advance += length;
        // A distant phase jump/restart is discrete. At most four native frames per network tick;
        // known locomotion advances about one. Never extrapolate attacks beyond received samples.
        if (advance < 0 || advance > 4 * span) return;
    } else if (advance < 0) return;
    out->anim_frame = a->anim_frame + advance * t;
    if (length && b->animation_loop && out->anim_frame >= length) out->anim_frame -= length;
}

bool sst_pose_buffer_sample(SstPoseBuffer *b, double now_ms, SstPose *out) {
    if (!b || !out || !b->count || !isfinite(now_ms) || now_ms<0 ||
        (b->started && now_ms<b->last_update_ms)) return false;
    update_cadence(b);
    b->delay_ticks = fmin(MAX_DELAY, fmax(MIN_DELAY, 2.0 + 2.0 * b->jitter_ms / b->source_period_ms));
    double target = (double)time_at(b,0) - b->delay_ticks;
    if (!b->started) {
        b->clock_tick = target;
        b->started = true;
    } else {
        double dt = (now_ms - b->last_update_ms) / b->source_period_ms;
        if (dt < 0) dt = 0;
        double err = target - (b->clock_tick + dt);
        if (fabs(err) > REANCHOR_TICKS) b->clock_tick = target;
        else b->clock_tick += dt * fmin(3.0, fmax(0.9, 1.0 + err * 0.1));  // gently speed up / slow down
    }
    b->last_update_ms = now_ms;
    if (b->clock_tick > (double)time_at(b,0) + MAX_EXTRAPOLATE) b->clock_tick = (double)time_at(b,0) + MAX_EXTRAPOLATE;

    // find samples a (older) and c (newer) around clock_tick
    int n = b->count;
    int age = 0;
    while (age < n - 1 && (double)time_at(b,age) > b->clock_tick) age++;
    const SstPose *a = at(b, age);
    if ((double)time_at(b,age) >= b->clock_tick || age == 0) {
        // before the oldest sample, or at/after the newest: hold (no extrapolation of animation state)
        *out = *a;
        if (age == 0 && n > 1 && b->clock_tick > (double)time_at(b,0)) {
            // short positional extrapolation from the last velocity
            const SstPose *p = at(b, 1);
            double span = (double)(int32_t)(a->tick - p->tick);
            float dx=a->pos[0]-p->pos[0],dy=a->pos[1]-p->pos[1],dz=a->pos[2]-p->pos[2];
            // A teleport is not velocity. Keep the newest position instead of
            // projecting beyond it while rebuilding interpolation history.
            if (span > 0 && span < 10 && dx*dx+dy*dy+dz*dz<=SNAP_DIST*SNAP_DIST) {
                float t = (float)((b->clock_tick - (double)time_at(b,0)) / span);
                for (int i = 0; i < 3; i++) out->pos[i] = a->pos[i] + (a->pos[i] - p->pos[i]) * t;
            }
        }
        return true;
    }
    const SstPose *c = at(b, age - 1);
    double span = (double)(int32_t)(c->tick - a->tick);
    float t = (float)((b->clock_tick - (double)time_at(b,age)) / span);
    float dx = c->pos[0] - a->pos[0], dy = c->pos[1] - a->pos[1], dz = c->pos[2] - a->pos[2];
    *out = t < 0.5f ? *a : *c;  // discrete fields (animation id, action, equipment) from the nearer sample
    if (dx * dx + dy * dy + dz * dz > SNAP_DIST * SNAP_DIST) return true;
    for (int i = 0; i < 3; i++) out->pos[i] = lerpf(a->pos[i], c->pos[i], t);
    for (int i = 0; i < 3; i++) out->rot[i] = lerp_angle(a->rot[i], c->rot[i], t);
    interpolate_animation(b, a, c, span, t, out);
    out->tick = (uint32_t)(int64_t)floor(b->clock_tick);
    return true;
}
