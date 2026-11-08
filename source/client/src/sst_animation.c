#include "sst_animation.h"
#include "sst_avatar.h"
#include "sst_hand.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CAPACITY 2048
#define PALETTE_MAX 0x10000u
#define SETTLE_FRAMES 6.0f // native animation frames, independent of 60/120 Hz presentation

typedef struct {
    uint64_t frame;
    uint64_t source_hash;
    const void *positions, *normals;
    size_t allocation;
    unsigned references;
    float source_model[12];
    uint16_t data[]; // binary16, after removing world/camera transforms
} Palette;
struct SstAnimationSample {
    uint16_t mesh, clip, action, context; // masked final-palette context; uses existing padding
    unsigned bucket;
    float frame;
    size_t position_size, normal_size;
    uint64_t used;
    uint64_t cohort; // a complete static idle snapshot, separate from phase samples
    uint16_t *data; // packed canonical model matrix, position palette, normal palette
    Palette *palette;
};
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(SstAnimationSample)==64,"animation sample metadata budget");
#endif
static SstAnimationSample samples[CAPACITY];
static float lengths[443];
static SstAnimationStats stats;
static SstAnimationPartStats animation_parts[64];
static SstAnimationReplayStats replay_stats;
#define REPLAY_PARTS 128
typedef struct { uint16_t mesh; size_t positions, normals; } ReplayPart;
static ReplayPart current_parts[REPLAY_PARTS], replay_parts[REPLAY_PARTS];
static unsigned current_part_count, replay_part_count;
static bool current_part_overflow, replay_part_overflow;
static uint64_t neutral_cohort, carry_cohort, building_cohort, cohort_serial;
static ReplayPart carry_parts[REPLAY_PARTS];
static unsigned carry_part_count;
static SstPose building_pose;
static bool building_failed;
static uint64_t serial, last_frame = UINT64_MAX;
static SstScene last_scene;
static uint16_t last_clip = UINT16_MAX;
static uint16_t last_action, last_context;
static uint32_t held_tick;
static float settled, last_phase;
static bool ready;
static bool source_learning=true;
static float canonical[(48 + 2 * PALETTE_MAX) / sizeof(float)];
#define ATTACH_MESHES 64
#define ATTACH_CLIPS 30
typedef struct {
    uint16_t body, joint;
    uint32_t positions, normals;
    uint32_t tick;
    unsigned confirmations;
    bool cached,blocked; // cached transforms must agree as well as live draw matrices
    uint16_t context; // uses the existing trailing padding; legacy banks0 / DRAWN2
} Attachment;
_Static_assert(sizeof(Attachment)==24,"bounded legacy attachment metadata");
static Attachment attachments[2][ATTACH_MESHES][ATTACH_CLIPS];
_Static_assert(sizeof attachments==92160,"bounded two-bank legacy table");
static struct { uint16_t mesh; size_t positions,normals; float data[PALETTE_MAX/4]; } observed_bodies[4];
static struct { uint16_t mesh; float model[12]; } observed_rigids[ATTACH_MESHES];
static unsigned observed_body_count,observed_rigid_count;
static uint16_t observed_clip;
static uint32_t observed_tick;
static SstPose observed_pose;
static bool observed_has_pose, observed_child_ready;
static SstPose capture_frame_pose;
static bool capture_frame_pose_valid;
static void finish_observations(void);
static Attachment *attachment_for(uint16_t mesh,uint16_t clip,uint16_t context);
static bool attachment_ready(const Attachment *a);
static bool cohort_complete(uint64_t cohort, const ReplayPart *parts, unsigned count, bool overflow);
static bool same_parts(const ReplayPart *a, unsigned an, const ReplayPart *b, unsigned bn) {
    if (!an || an != bn) return false;
    for (unsigned i=0;i<an;i++) {
        bool found=false;
        for (unsigned j=0;j<bn;j++)
            if (a[i].mesh==b[j].mesh && a[i].positions==b[j].positions && a[i].normals==b[j].normals)
                found=true;
        if (!found) return false;
    }
    return true;
}

// IEEE binary16 with ties-to-even rounding. Canonical translations are near Link's origin,
// not large world coordinates. Reject nonfinite/overflow values rather than corrupt a replay.
static bool pack_half(float value, uint16_t *out) {
    if (!isfinite(value) || fabsf(value) > 65504) return false;
    uint32_t bits;
    memcpy(&bits, &value, 4);
    unsigned sign = (bits >> 16) & 0x8000, exponent = (bits >> 23) & 255;
    unsigned mantissa = bits & 0x7fffff, rounded;
    if (exponent < 102) { *out = sign; return true; }
    if (exponent < 113) {
        mantissa |= 0x800000;
        unsigned shift = 126 - exponent, remainder = mantissa & ((1u << shift) - 1);
        rounded = mantissa >> shift;
        unsigned halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (rounded & 1))) rounded++;
        *out = sign | rounded;
    } else {
        rounded = (mantissa + 0xfff + ((mantissa >> 13) & 1)) >> 13;
        *out = sign | (((exponent - 112) << 10) + rounded);
    }
    return true;
}
static float unpack_half(uint16_t value) {
    unsigned exponent = (value >> 10) & 31, mantissa = value & 1023;
    if (!exponent) {
        float f = mantissa * 0x1p-24f;
        return value & 0x8000 ? -f : f;
    }
    uint32_t bits = ((uint32_t)(value & 0x8000) << 16) | ((exponent + 112) << 23) | (mantissa << 13);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static void discard(SstAnimationSample *s) {
    if (s->data) {
        if (!--s->palette->references) {
            stats.bytes -= s->palette->allocation;
            free(s->palette);
        }
        stats.samples--;
        memset(s, 0, sizeof *s);
    }
}
static void release_cohort(uint64_t cohort) {
    if(cohort)for(unsigned i=0;i<CAPACITY;i++)
        if(samples[i].data && samples[i].cohort==cohort)samples[i].cohort=0;
}
static void forget_carry_cohort(void) {
    release_cohort(carry_cohort);carry_cohort=0;carry_part_count=0;
    memset(carry_parts,0,sizeof carry_parts);
}
static void pause_learning(void) {
    last_clip = UINT16_MAX;
    last_action = 0;
    last_context = 0;
    settled = last_phase = 0;
    held_tick = 0;
    ready = false;
    observed_body_count=observed_rigid_count=0;
    observed_has_pose=observed_child_ready=false;
    capture_frame_pose_valid=false;
    release_cohort(building_cohort);building_cohort=0;
}
static void pause_capture(void) {
    pause_learning();
    current_part_count=replay_part_count=0;
    current_part_overflow=replay_part_overflow=false;
}
void sst_animation_reset(void) {
    sst_hand_reset();
    sst_avatar_equipment_reset();
    for (unsigned i = 0; i < CAPACITY; i++) discard(&samples[i]);
    memset(&stats, 0, sizeof stats);
    memset(animation_parts, 0, sizeof animation_parts);
    memset(&replay_stats, 0, sizeof replay_stats);
    memset(&last_scene, 0, sizeof last_scene);
    memset(lengths, 0, sizeof lengths);
    last_frame = UINT64_MAX;
    pause_capture();
    source_learning=true;
    serial = 0;
    neutral_cohort=carry_cohort=building_cohort=cohort_serial=0;
    carry_part_count=0;memset(carry_parts,0,sizeof carry_parts);
    memset(attachments,0,sizeof attachments);
}
static bool locomotion(uint16_t clip) {
    // Existing HD movement entries plus archive-observed sword idle19/action0.
    // Durations still come only from the local native animation controller.
    return clip == 19 || clip == 20 || clip == 21 || clip == 25 || clip == 26 || clip == 27 || clip == 189;
}
static bool movement(uint16_t clip) { return clip == 42 || clip == 44 || clip == 45; }
static bool finite_clip(uint16_t clip) { return movement(clip) || (clip >= 191 && clip <= 210); }
static bool sword(uint16_t clip) {
    // Native HD MPCutU through CutPush_Re. These are finite swings/recoveries, not loops.
    // Directional aiming (atL/atR + MPPoseFront/Around) needs more than one track and is excluded.
    return clip >= 191 && clip <= 210;
}
bool sst_animation_clip(uint16_t clip, float *length, bool *loop) {
    if ((!locomotion(clip) && !finite_clip(clip)) || !lengths[clip]) return false;
    if (length) *length = lengths[clip];
    if (loop) *loop = locomotion(clip);
    return true;
}
static uint16_t pose_context(const SstPose *p) { return p->equip_flags & SST_POSE_IDENTITY_MASK; }
static bool legacy_context(uint16_t context) { return context==0 || context==SST_EQUIP_SWORD_DRAWN; }
static bool valid_pose(const SstPose *p) {
    if (!p || (!locomotion(p->anim_id) && !finite_clip(p->anim_id)) || !isfinite(p->anim_frame) ||
        p->anim_frame < 0 || p->anim_frame > 65536) return false;
    // Selection already enforces this bound; never teach an out-of-range source
    // that can alias an in-range phase after loop wrapping or seed neutral replay.
    if (lengths[p->anim_id] && p->anim_frame > lengths[p->anim_id]) return false;
    uint16_t carry=p->equip_flags&SST_POSE_CONTEXT_MASK;
    if(carry!=0 && carry!=SST_POSE_CONTEXT_SMALL_CARRY)return false;
    if ((p->anim_id == 19 && p->action != 0) ||
        (sword(p->anim_id) && p->action != 109) || (p->anim_id == 189 && p->action != 3) ||
        (p->anim_id == 42 && p->action != 12) ||
        ((p->anim_id == 44 || p->anim_id == 45) && p->action != 16)) return false;
    for (unsigned i = 0; i < 3; i++)
        if (!isfinite(p->pos[i]) || fabsf(p->pos[i]) > 1e6f) return false;
    return true;
}
void sst_animation_frame(const SstScene *scene, const SstPose *local, uint64_t frame) {
    sst_animation_source_frame(scene,local,frame,true);
}
void sst_animation_source_frame(const SstScene *scene,const SstPose *local,uint64_t frame,bool allow_learning) {
    if (last_frame != UINT64_MAX && frame < last_frame) sst_animation_reset();
    if (!scene || !scene->stage[0] || !local) {
        // Palettes own canonical copies; their source pointers are never read after capture.
        // A loader gap must stop capture without discarding already learned peer animations.
        memset(&last_scene, 0, sizeof last_scene);
        forget_carry_cohort();
        pause_capture();
        source_learning=false;
        last_frame = frame;
        return;
    }
    if (strcmp(last_scene.stage, scene->stage) || last_scene.room != scene->room ||
        last_scene.layer != scene->layer || last_scene.entrance != scene->entrance) {
        forget_carry_cohort();
        pause_capture();
        if (stats.samples) stats.resumes++;
        last_scene = *scene;
    }
    // A forced draw can arrive after natural draws, or after Exit104. Discard
    // pending teaching before any frame finalizer, and deny the rest of a mixed
    // present. A later natural present must establish fresh settling again.
    if(!allow_learning && source_learning) {
        pause_learning();source_learning=false;
    } else if(allow_learning && !source_learning && last_frame!=frame) {
        pause_learning();source_learning=true;
    }
    if (last_frame == frame) return;
    if(building_cohort) {
        if(source_learning && !building_failed && cohort_complete(building_cohort,current_parts,current_part_count,current_part_overflow)) {
            if (pose_context(&building_pose)==SST_POSE_CONTEXT_SMALL_CARRY) {
                // Two complete matching manifests prevent a partial draw/family
                // transition from publishing a smaller carry snapshot.
                if (!replay_part_overflow && same_parts(current_parts,current_part_count,replay_parts,replay_part_count)) {
                    forget_carry_cohort();carry_cohort=building_cohort;
                    memcpy(carry_parts,current_parts,current_part_count*sizeof *carry_parts);
                    carry_part_count=current_part_count;
                } else release_cohort(building_cohort);
            } else { release_cohort(neutral_cohort);neutral_cohort=building_cohort; }
        } else release_cohort(building_cohort);
        building_cohort=0;
    }
    memcpy(replay_parts,current_parts,current_part_count*sizeof *replay_parts);
    replay_part_count=current_part_count;replay_part_overflow=current_part_overflow;
    current_part_count=0;current_part_overflow=false;
    if(!source_learning) {last_frame=frame;return;}
    finish_observations();
    observed_body_count=observed_rigid_count=0;
    observed_has_pose=observed_child_ready=false;
    const char *drop=getenv("SST_TEST_RIGID_DROP_FILE");
    if(drop && getenv("LARECOMP_DEBUG_DATA")) {
        FILE *f=fopen(drop,"r");
        if(f) {
            fclose(f);remove(drop);unsigned count=0;
            for(unsigned i=0;i<CAPACITY;i++)if(samples[i].data && !samples[i].position_size && !samples[i].normal_size) {discard(&samples[i]);count++;}
            fprintf(stderr,"[sst-attachment] cleared %u rigid pose samples for isolated verification\n",count);
        }
    }
    float threshold = finite_clip(local->anim_id) ? 1.5f : SETTLE_FRAMES;
    float advance = local->anim_frame - last_phase;
    if (last_clip != local->anim_id || last_action != local->action || last_context != pose_context(local) || !valid_pose(local) ||
        (finite_clip(local->anim_id) && advance < 0) || advance > 8) {
        settled = 0;
        held_tick = local->tick;
    }
    else if (local->anim_id == 45 && local->anim_frame == 0) {
        // Native jumpSEd holds frame zero in the air. Three fresh 30 Hz source samples
        // allow its copied pose to settle without inventing animation-phase advancement.
        uint32_t age = local->tick - held_tick;
        if (age >= 3 && age < 0x80000000u) settled = threshold;
    }
    else if (settled < threshold) {
        // A loop wrap is harmless, but a jump/reload must not count as a settled transition.
        if (advance > 0 && advance <= 8) settled += advance;
    }
    last_clip = local->anim_id;
    last_action = local->action;
    last_context = pose_context(local);
    last_phase = local->anim_frame;
    last_frame = frame;
    ready = valid_pose(local) && settled >= threshold;
    capture_frame_pose=*local;capture_frame_pose_valid=true;
    // waitS26/action0 is the controlled native ordinary-idle observation. Never
    // designate attacks, directional aiming, or an unverified idle ID as neutral.
    if(ready && !pose_context(local) && local->anim_id==26 && local->action==0 &&
       !cohort_complete(neutral_cohort,replay_parts,replay_part_count,replay_part_overflow)) {
        building_cohort=++cohort_serial;building_pose=*local;building_failed=false;
    } else if (ready && pose_context(local)==SST_POSE_CONTEXT_SMALL_CARRY &&
        !(local->equip_flags&0xc0) && local->anim_id==26 && local->action==67 &&
        (!same_parts(carry_parts,carry_part_count,replay_parts,replay_part_count) ||
         !cohort_complete(carry_cohort,replay_parts,replay_part_count,replay_part_overflow))) {
        // Native steady small-vase idle only. Copy one whole final palette;
        // never manufacture lifted arms from a gait or an unsupported layer.
        building_cohort=++cohort_serial;building_pose=*local;building_failed=false;
    }
}
bool sst_animation_set_length(uint16_t clip, float length) {
    if(!source_learning)return false;
    if (clip >= 443 || !isfinite(length) || length <= 0 || length > 65536) return false;
    if (lengths[clip] != length) {
        sst_hand_forget_clip(clip);
        for (unsigned i = 0; i < CAPACITY; i++)
            if (samples[i].data && samples[i].clip == clip) discard(&samples[i]);
        for(unsigned context=0;context<2;context++)for(unsigned mesh=0;mesh<ATTACH_MESHES;mesh++) {
            Attachment *a=attachment_for(mesh,clip,context?SST_EQUIP_SWORD_DRAWN:0);
            if(a) {if(attachment_ready(a))stats.attachments--;memset(a,0,sizeof *a);}
        }
        lengths[clip] = length;
    }
    return true;
}

// The canonical space is local Link's origin and yaw. It removes the capture camera as well as
// the local player's world transform, so replay works after either player or camera moves.
static bool model_view(const SstPose *p, const float view[12], float out[12]) {
    if (!valid_pose(p) || !view) return false;
    for (unsigned i = 0; i < 12; i++) if (!isfinite(view[i])) return false;
    for (unsigned i = 0; i < 3; i++) {
        float norm = 0;
        for (unsigned j = 0; j < 3; j++) norm += view[i*4+j]*view[i*4+j];
        if (fabsf(norm - 1) > .02f) return false;
        for (unsigned j = 0; j < i; j++) {
            float dot = 0;
            for (unsigned k = 0; k < 3; k++) dot += view[i*4+k]*view[j*4+k];
            if (fabsf(dot) > .02f) return false;
        }
    }
    float angle = p->rot[1] * (6.2831853071795864769f / 65536.0f);
    float c = cosf(angle), s = sinf(angle);
    for (unsigned i = 0; i < 3; i++) {
        out[i*4] = view[i*4]*c - view[i*4+2]*s;
        out[i*4+1] = view[i*4+1];
        out[i*4+2] = view[i*4]*s + view[i*4+2]*c;
        out[i*4+3] = view[i*4+3];
        for (unsigned j = 0; j < 3; j++) out[i*4+3] += view[i*4+j]*p->pos[j];
    }
    return true;
}
static void inverse(const float m[12], float out[12]) {
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 3; j++) out[i*4+j] = m[j*4+i];
        out[i*4+3] = 0;
        for (unsigned j = 0; j < 3; j++) out[i*4+3] -= m[j*4+i]*m[j*4+3];
    }
}
static void transform(void *data, size_t size, const float t[12], bool normal) {
    for (size_t at = 0; at + 48 <= size; at += 48)
        sst_avatar_matrix((float *)((uint8_t *)data + at), t, normal);
}
static SstAnimationSample *oldest(void) {
    SstAnimationSample *s = NULL;
    for (unsigned i = 0; i < CAPACITY; i++)
        if (samples[i].data && (!s || samples[i].used < s->used)) s = &samples[i];
    return s;
}
static uint64_t palette_hash(const void *positions, size_t pn, const void *normals, size_t nn) {
    uint64_t h = 14695981039346656037ull;
    const uint8_t *p = positions, *n = normals;
    for (size_t i = 0; i < pn; i++) h = (h ^ p[i]) * 1099511628211ull;
    for (size_t i = 0; i < nn; i++) h = (h ^ n[i]) * 1099511628211ull;
    return h;
}
static Attachment *attachment_for(uint16_t mesh,uint16_t clip,uint16_t context) {
    static const uint16_t clips[]={20,21,25,26,27,189,42,44,45};
    if(mesh>=ATTACH_MESHES || !legacy_context(context))return NULL;
    unsigned bank=context==SST_EQUIP_SWORD_DRAWN;
    if(clip==19)return &attachments[bank][mesh][29];
    if(clip>=191 && clip<=210)return &attachments[bank][mesh][9+clip-191];
    for(unsigned i=0;i<9;i++)if(clips[i]==clip)return &attachments[bank][mesh][i];
    return NULL;
}
static bool attachment_observed(const Attachment *a) { return a->body && a->confirmations>=3; }
static bool attachment_ready(const Attachment *a) { return attachment_observed(a) && a->cached && !a->blocked; }
static float matrix_error(const float *a,const float *b) {
    float error=0;
    for(unsigned k=0;k<12;k++) {
        if(!isfinite(a[k]) || !isfinite(b[k]))return INFINITY;
        error=fmaxf(error,fabsf(a[k]-b[k]));
    }
    return error;
}
static void validate_attachment_pair(Attachment *a,const SstAnimationSample *rigid,const SstAnimationSample *body) {
    if(!attachment_observed(a) || a->blocked || !rigid->data || !body->data ||
       !legacy_context(a->context) || rigid->context!=a->context || body->context!=a->context ||
       rigid->position_size || rigid->normal_size || body->mesh!=a->body ||
       body->clip!=rigid->clip || body->action!=rigid->action ||
       body->position_size!=a->positions || body->normal_size!=a->normals ||
       fabsf(body->frame-rigid->frame)>.001f || (a->joint+1u)*48>body->position_size)return;
    bool matches=true,was=attachment_ready(a);size_t offset=12+a->joint*12;
    for(unsigned k=0;k<12;k++) {
        float left=unpack_half(rigid->data[k]),right=unpack_half(body->data[offset+k]);
        if(!isfinite(left) || !isfinite(right) || fabsf(left-right)>(k%4==3?.5f:.01f))matches=false;
    }
    if(matches)a->cached=true;else a->blocked=true;
    if(was && !attachment_ready(a))stats.attachments--;
    if(!was && attachment_ready(a))stats.attachments++;
}
static void validate_attachment_link(uint16_t mesh,uint16_t clip,Attachment *a) {
    if(!attachment_observed(a) || a->blocked)return;
    for(unsigned i=0;i<CAPACITY && !a->blocked;i++) {
        SstAnimationSample *rigid=&samples[i];
        if(!rigid->data || rigid->mesh!=mesh || rigid->clip!=clip || rigid->context!=a->context ||
           rigid->position_size || rigid->normal_size)continue;
        for(unsigned j=0;j<CAPACITY && !a->blocked;j++)validate_attachment_pair(a,rigid,&samples[j]);
    }
}
static void validate_attachment_sample(SstAnimationSample *sample) {
    if(!sample->position_size && !sample->normal_size) {
        Attachment *a=attachment_for(sample->mesh,sample->clip,sample->context);
        if(a)for(unsigned i=0;i<CAPACITY && !a->blocked;i++)validate_attachment_pair(a,sample,&samples[i]);
    } else if(sample->position_size) {
        for(unsigned i=0;i<CAPACITY;i++) {
            SstAnimationSample *rigid=&samples[i];
            if(!rigid->data || rigid->position_size || rigid->normal_size ||
               rigid->clip!=sample->clip || rigid->action!=sample->action || fabsf(rigid->frame-sample->frame)>.001f)continue;
            Attachment *a=attachment_for(rigid->mesh,rigid->clip,rigid->context);
            if(a)validate_attachment_pair(a,rigid,sample);
        }
    }
}
static void finish_observations(void) {
    for(unsigned r=0;r<observed_rigid_count;r++) {
        if(observed_has_pose && observed_child_ready) {
            uint16_t joint;uint32_t layout;
            if(sst_avatar_hand_parent(observed_rigids[r].mesh,&joint,&layout))
                for(unsigned b=0;b<observed_body_count;b++)
                    if(observed_bodies[b].positions==layout && (joint+1u)*48<=layout)
                        sst_hand_observe(observed_rigids[r].mesh,&observed_pose,observed_bodies[b].mesh,
                            observed_bodies[b].positions,observed_bodies[b].normals,
                            observed_bodies[b].data+joint*12,observed_rigids[r].model);
        }
        // The legacy table has separate normal/drawn banks. Carry offsets still
        // require the verified child cache above, with its full-context key.
        uint16_t context=observed_has_pose?pose_context(&observed_pose):0;
        Attachment *a=attachment_for(observed_rigids[r].mesh,observed_clip,context);
        if(!a)continue;
        unsigned body=4,joint=0;bool old=false;
        // Keep an already verified match when equivalent palettes occur in several draws.
        for(unsigned b=0;b<observed_body_count;b++) {
            if(observed_bodies[b].mesh==a->body && observed_bodies[b].positions==a->positions &&
               observed_bodies[b].normals==a->normals && (a->joint+1u)*48<=a->positions &&
               matrix_error(observed_rigids[r].model,observed_bodies[b].data+a->joint*12)<=.01f) {
                body=b;joint=a->joint;old=true;break;
            }
        }
        if(!old) for(unsigned b=0;b<observed_body_count && body==4;b++)
            for(unsigned j=0;j<observed_bodies[b].positions/48;j++)
                if(matrix_error(observed_rigids[r].model,observed_bodies[b].data+j*12)<=.01f) {body=b;joint=j;break;}
        if(body==4) {
            // Missing donors can be culled; a present donor that disagrees invalidates its link.
            for(unsigned b=0;b<observed_body_count;b++)if(observed_bodies[b].mesh==a->body) {
                if(attachment_ready(a))stats.attachments--;
                bool blocked=a->blocked;memset(a,0,sizeof *a);a->blocked=blocked;a->context=context;break;
            }
            continue;
        }
        bool was=attachment_ready(a),known=attachment_observed(a);
        if(!old) {
            if(was)stats.attachments--;
            *a=(Attachment){.body=observed_bodies[body].mesh,.joint=joint,.blocked=a->blocked,.context=context,
                .positions=observed_bodies[body].positions,.normals=observed_bodies[body].normals,
                .tick=observed_tick,.confirmations=1};
        } else {
            if(a->tick!=observed_tick) {if(a->confirmations<3)a->confirmations++;a->tick=observed_tick;}
        }
        if(!known && attachment_observed(a))validate_attachment_link(observed_rigids[r].mesh,observed_clip,a);
    }
}
static void observe_attachment(uint16_t mesh,const SstPose *p,const float *model,const void *positions,size_t pn,size_t nn) {
    if(!mesh || mesh>=ATTACH_MESHES || !p || !valid_pose(p) || !model || pn>PALETTE_MAX || nn>PALETTE_MAX || pn%48 || nn%48)return;
    // A second context within one presented frame cannot join the first one's
    // observation cohort (frame() intentionally advances once per present).
    if(pose_context(p)!=last_context)return;
    for(unsigned i=0;i<12;i++)if(!isfinite(model[i]))return;
    bool child_eligible=ready && capture_frame_pose_valid && p->anim_id==last_clip && p->action==last_action &&
        pose_context(p)==pose_context(&capture_frame_pose) &&
        p->tick==capture_frame_pose.tick && p->anim_frame==capture_frame_pose.anim_frame &&
        !memcmp(p->pos,capture_frame_pose.pos,sizeof p->pos) && !memcmp(p->rot,capture_frame_pose.rot,sizeof p->rot);
    if(!observed_has_pose) {observed_pose=*p;observed_has_pose=true;observed_child_ready=child_eligible;}
    else if(!child_eligible || p->tick!=observed_pose.tick || p->anim_id!=observed_pose.anim_id ||
        pose_context(p)!=pose_context(&observed_pose) ||
        p->action!=observed_pose.action || p->anim_frame!=observed_pose.anim_frame ||
        memcmp(p->pos,observed_pose.pos,sizeof p->pos) || memcmp(p->rot,observed_pose.rot,sizeof p->rot))
        observed_child_ready=false;
    observed_clip=p->anim_id;observed_tick=p->tick;
    if(pn && positions) {
        for(unsigned i=0;i<observed_body_count;i++)if(observed_bodies[i].mesh==mesh)return;
        unsigned i=observed_body_count;
        if(i<4)observed_body_count++;
        else {
            // Keep the first palette (often the head) and the three largest later
            // palettes. Small early parts must not crowd out a full body donor.
            // The same four bounded copies and exact live/cached checks still apply.
            i=1;
            for(unsigned j=2;j<4;j++)if(observed_bodies[j].positions<observed_bodies[i].positions)i=j;
            if(pn<=observed_bodies[i].positions)return;
        }
        observed_bodies[i].mesh=mesh;observed_bodies[i].positions=pn;observed_bodies[i].normals=nn;
        memcpy(observed_bodies[i].data,positions,pn);
    } else if(!pn && !nn && observed_rigid_count<ATTACH_MESHES) {
        for(unsigned i=0;i<observed_rigid_count;i++)if(observed_rigids[i].mesh==mesh)return;
        unsigned i=observed_rigid_count++;observed_rigids[i].mesh=mesh;memcpy(observed_rigids[i].model,model,48);
    }
}
bool sst_animation_declare_part(uint16_t mesh,size_t pn,size_t nn) {
    // The caller authorizes geometry. This records only its required layout,
    // including while the viewer plays an unsupported animation.
    if(!mesh || (mesh>=55 && mesh<=60) || pn>PALETTE_MAX || nn>PALETTE_MAX || pn%48 || nn%48)return false;
    unsigned i=0;
    for(;i<current_part_count;i++)if(current_parts[i].mesh==mesh &&
        current_parts[i].positions==pn && current_parts[i].normals==nn)return true;
    if(i==REPLAY_PARTS) {
        current_part_overflow=true;return false;
    }
    current_parts[current_part_count++]=(ReplayPart){mesh,pn,nn};return true;
}
bool sst_animation_capture(uint16_t mesh, const SstPose *local, const float view[12],
                           const float model[12], const void *positions, size_t pn,
                           const void *normals, size_t nn) {
    // Equipment has independent drawn/sheathed pose IDs and is not a required
    // body part. Preserve the same declaration policy as geometry-only callers.
    sst_animation_declare_part(mesh,pn,nn);
    if(!source_learning)return false;
    observe_attachment(mesh,local,model,positions,pn,nn);
    if(building_cohort && (!local || local->tick!=building_pose.tick ||
       local->anim_id!=building_pose.anim_id || local->action!=building_pose.action ||
       pose_context(local)!=pose_context(&building_pose) ||
       (pose_context(&building_pose)==SST_POSE_CONTEXT_SMALL_CARRY && (local->equip_flags&0xc0)) ||
       local->anim_frame!=building_pose.anim_frame ||
       memcmp(local->pos,building_pose.pos,sizeof local->pos) ||
       memcmp(local->rot,building_pose.rot,sizeof local->rot)))building_failed=true;
    if (!ready || !mesh || !valid_pose(local) || local->anim_id != last_clip || local->action != last_action ||
        pose_context(local)!=last_context ||
        !model || pn > PALETTE_MAX || nn > PALETTE_MAX ||
        (pn && !positions) || (nn && !normals) || pn % 48 || nn % 48) return false;
    float mv[12], inv[12];
    if (!model_view(local, view, mv)) return false;
    unsigned bucket = (unsigned)(local->anim_frame / (finite_clip(local->anim_id) ? 1.5f : 3.0f));
    uint64_t cohort=building_cohort && pose_context(local)==pose_context(&building_pose) &&
        local->anim_id==26 && local->action==building_pose.action?building_cohort:0;
    SstAnimationSample *slot = NULL;
    for (unsigned i = 0; i < CAPACITY; i++) {
        SstAnimationSample *s = &samples[i];
        if (!s->data) { if (!slot) slot = s; continue; }
        if (s->mesh == mesh && s->clip == local->anim_id && s->action == local->action && s->context==pose_context(local) && s->bucket == bucket &&
            s->position_size == pn && s->normal_size == nn && (!cohort || s->cohort==cohort)) {
            s->used = ++serial;
            return true; // Retain a stable pose; do not allocate/transform again every draw.
        }
    }
    if (!slot) { slot = oldest(); discard(slot); }
    // Several Link meshes bind the same palette and model matrix in one draw frame. Share that
    // immutable copy instead of charging the RAM budget for each material/mesh using it.
    Palette *palette = NULL;
    uint64_t hash = palette_hash(positions, pn, normals, nn);
    for (unsigned i = 0; i < CAPACITY; i++) {
        SstAnimationSample *s = &samples[i];
        Palette *p = s->palette;
        if (p && s->context==pose_context(local) && p->frame == last_frame && p->positions == positions && p->normals == normals &&
            s->position_size == pn && s->normal_size == nn &&
            p->source_hash == hash &&
            !memcmp(p->source_model, model, 48)) { palette = p; break; }
    }
    size_t need = sizeof(Palette) + (48 + pn + nn) / 2;
    if (!palette) {
        while (stats.bytes + need > SST_ANIMATION_BUDGET) {
            SstAnimationSample *old = oldest();
            if (!old) return false;
            discard(old);
        }
        palette = malloc(need);
        if (!palette) return false;
        *palette = (Palette){.frame = last_frame, .positions = positions, .normals = normals,
                             .source_hash = hash, .allocation = need};
        memcpy(palette->source_model, model, 48);
        memcpy(canonical, model, 48);
        if (pn) memcpy((uint8_t *)canonical + 48, positions, pn);
        if (nn) memcpy((uint8_t *)canonical + 48 + pn, normals, nn);
        inverse(mv, inv);
        sst_avatar_matrix(canonical, inv, false);
        transform((uint8_t *)canonical + 48, pn, inv, false);
        transform((uint8_t *)canonical + 48 + pn, nn, inv, true);
        for (size_t i = 0; i < (48 + pn + nn) / sizeof(float); i++)
            if (!pack_half(canonical[i], &palette->data[i])) { free(palette); return false; }
        stats.bytes += need;
    }
    palette->references++;
    *slot = (SstAnimationSample){.mesh = mesh, .clip = local->anim_id, .action = local->action, .context=pose_context(local), .bucket = bucket,
        .frame = local->anim_frame, .position_size = pn, .normal_size = nn,
        .used = ++serial, .cohort = cohort, .data = palette->data, .palette = palette};
    stats.samples++;
    stats.captures++;
    if(legacy_context(slot->context))validate_attachment_sample(slot);
    if (sword(local->anim_id)) stats.sword_captures++;
    return true;
}
static bool select_pose(uint16_t mesh, const SstPose *remote, size_t pn, size_t nn, SstAnimationPose *out, bool account) {
    if (!out) return false;
    *out = (SstAnimationPose){0};
    SstAnimationSample *before = NULL, *after = NULL;
    float before_delta = -INFINITY, after_delta = INFINITY;
    if (last_scene.stage[0] && mesh && valid_pose(remote) &&
        (!lengths[remote->anim_id] || remote->anim_frame <= lengths[remote->anim_id]))
        for (unsigned i = 0; i < CAPACITY; i++) {
            SstAnimationSample *s = &samples[i];
            if (!s->data || s->mesh != mesh || s->clip != remote->anim_id || s->action != remote->action ||
                s->context!=pose_context(remote) ||
                s->position_size != pn || s->normal_size != nn) continue;
            float d = s->frame - remote->anim_frame;
            if (locomotion(s->clip) && lengths[s->clip] > 0) d = remainderf(d, lengths[s->clip]);
            if (fabsf(d) > (finite_clip(s->clip) ? 2.0f : 4.5f)) continue;
            if (d <= 0 && d > before_delta) { before = s; before_delta = d; }
            if (d >= 0 && d < after_delta) { after = s; after_delta = d; }
        }
    if (!before) { before = after; before_delta = after_delta; }
    if (!after) { after = before; after_delta = before_delta; }
    if (!before) return false;
    if(account) {
        before->used = after->used = ++serial;
        stats.hits++;
        if (sword(remote->anim_id)) stats.sword_hits++;
    }
    *out = (SstAnimationPose){.before = before, .after = after};
    if (after_delta > before_delta) out->blend = -before_delta / (after_delta - before_delta);
    return true;
}
bool sst_animation_select(uint16_t mesh,const SstPose *remote,size_t pn,size_t nn,SstAnimationPose *out) {
    if(!out)return false;
    if(select_pose(mesh,remote,pn,nn,out,true)) {
        if(mesh<64)animation_parts[mesh].hits++;
        if(pn)stats.skinned_hits++;
        return true;
    }
    if(remote && !pn && !nn) {
        Attachment *a=attachment_for(mesh,remote->anim_id,pose_context(remote));
        if(a && attachment_ready(a) && select_pose(a->body,remote,a->positions,a->normals,out,true)) {
            out->attachment=true;out->joint=a->joint;stats.attachment_hits++;
            if(mesh<64)animation_parts[mesh].hits++;
            return true;
        }
        SstHandOffset child;
        bool active_body=false;
        bool have_child=valid_pose(remote) && sst_hand_lookup(mesh,remote,locomotion(remote->anim_id),lengths[remote->anim_id],&child);
        if(have_child && !replay_part_overflow)
            for(unsigned i=0;i<replay_part_count;i++)
                if(replay_parts[i].mesh==child.body && replay_parts[i].positions==child.positions &&
                   replay_parts[i].normals==child.normals)active_body=true;
        if(active_body &&
           select_pose(child.body,remote,child.positions,child.normals,out,true)) {
            out->attachment=true;out->child=true;out->joint=child.joint;
            memcpy(out->local,child.local,sizeof out->local);
            stats.attachment_hits++;sst_hand_note_placement();
            if(mesh<64)animation_parts[mesh].hits++;
            return true;
        }
    }
    stats.misses++;if(pn)stats.skinned_misses++;
    if(mesh<64)animation_parts[mesh].misses++;
    return false;
}
static SstAnimationSample *cohort_part(uint64_t cohort,uint16_t mesh,size_t pn,size_t nn) {
    if(!cohort)return NULL;
    for(unsigned i=0;i<CAPACITY;i++) {
        SstAnimationSample *s=&samples[i];
        if(s->data && s->cohort==cohort && s->mesh==mesh && s->clip==26 &&
           ((!s->context && !s->action) || (s->context==SST_POSE_CONTEXT_SMALL_CARRY && s->action==67)) &&
           s->position_size==pn && s->normal_size==nn)return s;
    }
    return NULL;
}
static bool cohort_complete(uint64_t cohort,const ReplayPart *parts,unsigned count,bool overflow) {
    bool skinned=false;
    if(!cohort || !count || overflow)return false;
    for(unsigned i=0;i<count;i++) {
        if(!cohort_part(cohort,parts[i].mesh,parts[i].positions,parts[i].normals))return false;
        if(parts[i].positions)skinned=true;
    }
    return skinned;
}
bool sst_animation_replay_prepare(const SstPose *remote,SstAnimationReplay *out) {
    if(!out)return false;
    *out=(SstAnimationReplay){0};
    bool exact=last_scene.stage[0] && replay_part_count && !replay_part_overflow && valid_pose(remote);
    bool neutral=last_scene.stage[0] && replay_part_count && !replay_part_overflow && remote && neutral_cohort;
    bool carry=last_scene.stage[0] && carry_cohort && !replay_part_overflow && valid_pose(remote) &&
        pose_context(remote)==SST_POSE_CONTEXT_SMALL_CARRY && !(remote->equip_flags&0xc0) &&
        same_parts(carry_parts,carry_part_count,replay_parts,replay_part_count) &&
        cohort_complete(carry_cohort,replay_parts,replay_part_count,replay_part_overflow);
    bool skinned=false;SstAnimationSample *idle=NULL,*carry_idle=NULL;
    for(unsigned i=0;i<replay_part_count;i++) {
        ReplayPart *p=&replay_parts[i];SstAnimationPose pose;
        if(!p->positions && !p->normals)continue; // missing rigid pieces are suppressed independently
        if(p->positions)skinned=true;
        if(carry)carry_idle=cohort_part(carry_cohort,p->mesh,p->positions,p->normals);
        if(exact && !select_pose(p->mesh,remote,p->positions,p->normals,&pose,false))exact=false;
        SstAnimationSample *s=cohort_part(neutral_cohort,p->mesh,p->positions,p->normals);
        if(!s)neutral=false;else idle=s;
    }
    if(skinned && exact) {
        *out=(SstAnimationReplay){.pose=*remote,.ready=true};replay_stats.exact_frames++;return true;
    }
    if(skinned && carry && carry_idle) {
        SstPose pose=*remote;pose.anim_id=carry_idle->clip;pose.action=carry_idle->action;pose.anim_frame=carry_idle->frame;
        if(valid_pose(&pose)) {
            *out=(SstAnimationReplay){.pose=pose,.cohort=carry_cohort,.ready=true};
            replay_stats.carry_frames++;return true;
        }
    }
    if(skinned && neutral && idle) {
        SstPose pose=*remote;pose.anim_id=idle->clip;pose.action=idle->action;pose.anim_frame=idle->frame;
        pose.equip_flags &= (uint8_t)~SST_POSE_IDENTITY_MASK;
        if(valid_pose(&pose)) {
            *out=(SstAnimationReplay){.pose=pose,.cohort=neutral_cohort,.ready=true};
            replay_stats.neutral_frames++;return true;
        }
    }
    replay_stats.unavailable_frames++;return false;
}
bool sst_animation_replay_select(uint16_t mesh,const SstAnimationReplay *replay,size_t pn,size_t nn,SstAnimationPose *out) {
    if(!out)return false;
    *out=(SstAnimationPose){0};
    if(replay && replay->ready && last_scene.stage[0]) {
        if(!replay->cohort) {
            if(sst_animation_select(mesh,&replay->pose,pn,nn,out))return true;
        } else {
            SstAnimationSample *s=cohort_part(replay->cohort,mesh,pn,nn);
            if(s && valid_pose(&replay->pose) && pose_context(&replay->pose)==s->context && replay->pose.anim_id==s->clip &&
               replay->pose.action==s->action && replay->pose.anim_frame==s->frame) {
                s->used=++serial;stats.hits++;if(pn)stats.skinned_hits++;
                if(mesh<64)animation_parts[mesh].hits++;
                *out=(SstAnimationPose){.before=s,.after=s};return true;
            }
            stats.misses++;if(pn)stats.skinned_misses++;
            if(mesh<64)animation_parts[mesh].misses++;
        }
    }
    replay_stats.suppressed_parts++;return false;
}
SstAnimationReplayStats sst_animation_replay_stats(void) {
    SstAnimationReplayStats out=replay_stats;out.neutral_parts=out.carry_parts=0;
    for(unsigned i=0;i<CAPACITY;i++)if(samples[i].data) {
        if(neutral_cohort && samples[i].cohort==neutral_cohort)out.neutral_parts++;
        if(carry_cohort && samples[i].cohort==carry_cohort)out.carry_parts++;
    }
    return out;
}
bool sst_animation_skip_rigid(const SstPose *local,const SstPose *remote) {
    if(!last_scene.stage[0] || !valid_pose(remote))return false;
    if(valid_pose(local) && local->anim_id==remote->anim_id && local->action==remote->action &&
       pose_context(local)==pose_context(remote) && isfinite(local->anim_frame)) {
        float delta=local->anim_frame-remote->anim_frame;
        if(locomotion(remote->anim_id) && lengths[remote->anim_id]>0)delta=remainderf(delta,lengths[remote->anim_id]);
        if(fabsf(delta)<=.5f)return false;
    }
    if(lengths[remote->anim_id] && remote->anim_frame>lengths[remote->anim_id])return false;
    for(unsigned i=0;i<CAPACITY;i++) {
        SstAnimationSample *s=&samples[i];
        if(!s->data || !s->position_size || s->clip!=remote->anim_id || s->action!=remote->action ||
           s->context!=pose_context(remote))continue;
        float delta=s->frame-remote->anim_frame;
        if(locomotion(s->clip) && lengths[s->clip]>0)delta=remainderf(delta,lengths[s->clip]);
        if(fabsf(delta)<=(finite_clip(s->clip)?2.0f:4.5f)) {stats.rigid_skips++;return true;}
    }
    return false;
}
static void blend_copy(void *dest, const uint16_t *a, const uint16_t *b, size_t size, float blend) {
    float *d = dest;
    for (size_t i = 0; i < size/sizeof(float); i++) {
        float left = unpack_half(a[i]);
        d[i] = !blend || a == b ? left : left + (unpack_half(b[i]) - left) * blend;
    }
}
bool sst_animation_uniform(const SstAnimationPose *pose, const SstPose *remote,
                           const float view[12], unsigned buffer, void *uniform, size_t size) {
    float mv[12];
    if (!pose || !pose->before || !pose->after || !isfinite(pose->blend) || pose->blend < 0 || pose->blend > 1)
        return false;
    const SstAnimationSample *s = pose->before, *other = pose->after;
    if (!s->data || !other->data || !uniform || !model_view(remote, view, mv) ||
        s->clip != remote->anim_id || s->action != remote->action || other->action != s->action ||
        s->context!=pose_context(remote) || other->context!=s->context ||
        other->clip != s->clip || other->mesh != s->mesh ||
        other->position_size != s->position_size || other->normal_size != s->normal_size) return false;
    if(pose->attachment) {
        if(((s->context&SST_POSE_CONTEXT_MASK) && !pose->child) || buffer!=0 || size<112 || (pose->joint+1u)*48>s->position_size)return false;
        float m[12];size_t offset=(48+pose->joint*48)/sizeof(float);
        blend_copy(m,s->data+offset,other->data+offset,48,pose->blend);
        if(pose->child && !sst_hand_compose(m,pose->local,m))return false;
        sst_avatar_matrix(m,mv,false);
        for(unsigned k=0;k<12;k++)if(!isfinite(m[k]))return false;
        memcpy((uint8_t*)uniform+64,m,sizeof m);return true;
    }
    if (buffer == 0) {
        if (size < 112) return false;
        float *m = (float *)((uint8_t *)uniform + 64);
        blend_copy(m, s->data, other->data, 48, pose->blend);
        sst_avatar_matrix(m, mv, false);
        return true;
    }
    size_t available = buffer == 3 ? s->position_size : buffer == 4 ? s->normal_size : 0;
    if (!size || size > available || size % sizeof(float)) return false;
    size_t offset = 48 + (buffer == 4 ? s->position_size : 0);
    blend_copy(uniform, s->data + offset / sizeof(float), other->data + offset / sizeof(float), size, pose->blend);
    transform(uniform, size, mv, buffer == 4);
    return true;
}
SstAnimationStats sst_animation_stats(void) { return stats; }
SstAnimationPartStats sst_animation_part_stats(uint16_t mesh) {
    return mesh<64?animation_parts[mesh]:(SstAnimationPartStats){0};
}
bool sst_animation_wrist_link(uint16_t mesh,uint16_t *body,uint16_t *joint,uint32_t *positions) {
    if(mesh>=ATTACH_MESHES)return false;
    const Attachment *match=NULL;unsigned clips=0;bool seen[ATTACH_CLIPS]={0};
    for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<ATTACH_CLIPS;i++) {
        const Attachment *a=&attachments[bank][mesh][i];
        if(!attachment_ready(a))continue;
        if(match && (a->body!=match->body || a->joint!=match->joint ||
            a->positions!=match->positions || a->normals!=match->normals))return false;
        match=a;if(!seen[i]){seen[i]=true;clips++;}
    }
    if(!match || clips<2)return false;
    if(body)*body=match->body;if(joint)*joint=match->joint;if(positions)*positions=match->positions;
    return true;
}

static uint64_t teaching_hash_bytes(uint64_t hash,const void *data,size_t size) {
    const unsigned char *bytes=data;
    for(size_t i=0;i<size;i++)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    return hash;
}
uint64_t sst_animation_teaching_hash(void) {
    uint64_t hash=UINT64_C(14695981039346656037);
    hash=teaching_hash_bytes(hash,&neutral_cohort,sizeof neutral_cohort);
    hash=teaching_hash_bytes(hash,&carry_cohort,sizeof carry_cohort);
    hash=teaching_hash_bytes(hash,&carry_part_count,sizeof carry_part_count);
    for(unsigned i=0;i<carry_part_count;i++) {
        hash=teaching_hash_bytes(hash,&carry_parts[i].mesh,sizeof carry_parts[i].mesh);
        hash=teaching_hash_bytes(hash,&carry_parts[i].positions,sizeof carry_parts[i].positions);
        hash=teaching_hash_bytes(hash,&carry_parts[i].normals,sizeof carry_parts[i].normals);
    }
    hash=teaching_hash_bytes(hash,lengths,sizeof lengths);
    // Exclude structure padding from the diagnostic oracle.
#define HASH_FIELD(object,field) hash=teaching_hash_bytes(hash,&(object)->field,sizeof (object)->field)
    for(unsigned bank=0;bank<2;bank++)for(unsigned mesh=0;mesh<ATTACH_MESHES;mesh++)
        for(unsigned clip=0;clip<ATTACH_CLIPS;clip++) {
            const Attachment *a=&attachments[bank][mesh][clip];
            HASH_FIELD(a,body);HASH_FIELD(a,joint);HASH_FIELD(a,positions);HASH_FIELD(a,normals);
            HASH_FIELD(a,tick);HASH_FIELD(a,confirmations);HASH_FIELD(a,cached);
            HASH_FIELD(a,blocked);HASH_FIELD(a,context);
        }
#undef HASH_FIELD
    for(unsigned i=0;i<CAPACITY;i++)if(samples[i].data) {
        const SstAnimationSample *s=&samples[i];
        uint16_t ids[4]={s->mesh,s->clip,s->action,s->context};
        hash=teaching_hash_bytes(hash,ids,sizeof ids);
        hash=teaching_hash_bytes(hash,&s->frame,sizeof s->frame);
        hash=teaching_hash_bytes(hash,&s->position_size,sizeof s->position_size);
        hash=teaching_hash_bytes(hash,&s->normal_size,sizeof s->normal_size);
        hash=teaching_hash_bytes(hash,s->data,(48+s->position_size+s->normal_size)/2);
    }
    return hash;
}
