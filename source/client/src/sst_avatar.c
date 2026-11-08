// SPDX-License-Identifier: MIT
// Adapted from the user-provided SST handoff dated2026-10-30; original archive source retained.
#include "sst_avatar.h"
#include <math.h>
#include <string.h>
#include <stdatomic.h>

uint16_t sst_avatar_mesh_id(const void *vertices, size_t size) {
    // FNV-1a signatures of 256 bytes and allocation sizes for v1.0.0 Link geometry.
    // These identify geometry only; no game asset data is included in the mod.
    static const struct {
        uint64_t hash;
        size_t size;
    } meshes[] = {
        {0x671942a079703a3cull, 90112},  {0x0300976035458b62ull, 4096},  {0xa5775b9a3cca06fcull, 4096},
        {0x0d0eb79018421a7full, 4096},   {0x28fc5d0554502437ull, 4096},  {0x136a41efac300ebdull, 110592},
        {0x5dfa897b905fdc09ull, 4096},   {0xfdc0e32f73d7f6f9ull, 8192},  {0xfb6a2b322e5a02daull, 40960},
        {0xd62fc22f3d186e19ull, 126976}, {0xd58a26e14c6a9a9aull, 12288}, {0x1a567635a5313b3cull, 28672},
        {0xb085ccb4ac029e0dull, 28672},  {0xcffbdaef88cc03e2ull, 28672}, {0xe8ad1f21d5fba882ull, 61440},
        // Hero tunic and its attached Goddess Sword geometry; native Link-only bedroom capture.
        {0x0a9dd0db7eea3bc6ull, 53248},
        {0x3f3bb498bb2f036aull, 8192},
        {0x4d4ab90b641f5685ull, 77824},
        {0x52810dfbd1b75bceull, 94208},
        {0x540e58c73b91697cull, 45056},
        {0x54ae69a71d72ba1eull, 45056},
        {0x55eca924d68ef52eull, 40960},
        {0x61ca26efefbe9c4bull, 28672},
        {0x77a14156bfe3cefaull, 24576},
        {0x8817825e68b989bcull, 8192},
        {0x89bbcc6f94c04220ull, 8192},
        {0x9415cd2ef90fa952ull, 4096},
        {0x96d291f963aa1df1ull, 4096},
        {0xa05d931df8bc8ad0ull, 40960},
        {0xa0795d4f8201e5c2ull, 8192},
        {0xaf2bc3a9ba2789e0ull, 69632},
        {0xb1c709cadd7a92cfull, 4096},
        {0xb77126bb7adf9553ull, 16384},
        {0xe6c1b56c8d49d58aull, 28672},
        {0xeeceb46793db9b7aull, 32768},
        // Native bedroom captures with Goddess Longsword/White Sword and both Master Swords.
        {0x2430d2e4ba8efec5ull, 12288},
        {0x46a93694a151a239ull, 69632},
        {0x53516772c78bcb79ull, 53248},
        {0x6b6d54042a1e35beull, 49152},
        {0x6e2c2400d301f8e7ull, 36864},
        {0x989428bdd169450eull, 40960},
        {0x9ecf7cacea6319d8ull, 12288},
        {0xad977aeacd967d4full, 45056},
        {0xe868505ec7c6e506ull, 16384},
        {0xfde3c070b0902901ull, 16384},
        // Observed hand variants: full quantized position sets match named Alink
        // hand polygons sharing the same left/right DrawOpa wrist node.
        {0xba8f80a6c1af9b89ull, 28672}, // 46: HandLA
        {0x3f12c815a1145a4bull, 28672}, // 47: HandRA
        {0x931c5b90d4513a88ull, 28672}, // 48: HandLC
        {0xd46bdcb0ccfd1db2ull, 28672}, // 49: HandRC
        {0x282efda3bd6e77e4ull, 28672}, // 50: HandRB
        {0x414d8f71d0bcdba6ull, 28672}, // 51: HandRE
        // Idle hands of the starting outfit (SST_MESH_CENSUS, session 006): model-only, drawn every present
        // near Link, origins exactly on joints 3 / 5 of the main skeleton (k6/k9/k12) over 1000+ presents,
        // the same joints as ids 13 / 14 that replace them. Their own rotation differs, so they keep their own
        // pose identity (not folded) and are placed only from their own captured poses.
        {0xb598eb736c83e91aull, 28672}, // 52: idle hand on joint 3
        {0xf0bd2cccdaee937full, 28672}, // 53: idle hand on joint 5
        // Sword-grip hand of id 53 (SST_MESH_CENSUS, session 007, save with a sword): appears exactly when the
        // sword is drawn ("-k53 +u4" in one present), model-only, near Link for 1125 presents while drawn, origin
        // on joint 5 of k6/k9/k12 with translation deviation 0.00 over 120+ presents (rotation up to 0.87).
        // Own pose identity (not folded); placed from its own poses or a validated child offset only.
        {0xa3030cf882ecfdabull, 28672}, // 54: sword-grip hand on joint 5
        // Sword equipment of the 06:31 save (census, session 007-008): model-only, drawn every present near Link
        // while the player owns the sword (sheathed on the back or in the hand), on no fixed skeleton joint (the sheath
        // swings). Re-issued for a remote only when its pose reports a sword (SST_EQUIP_SWORD), from poses captured
        // in the same drawn/sheathed state (sst_avatar_equipment_pose).
        {0x84ef93f1069747e0ull, 32768}, // 55: sword equipment piece
        {0x66c1dc573072ef9bull, 4096},  // 56: sword equipment piece
        {0x36af2478f22a3576ull, 24576}, // 57: sword equipment piece
    };
    if (!vertices || size < 256)
        return false;
    bool candidate = false;
    for (size_t i = 0; i < sizeof meshes / sizeof *meshes; i++)
        if (meshes[i].size == size)
            candidate = true;
    if (!candidate)
        return false;
    uint64_t hash = sst_avatar_signature(vertices, size);
    for (size_t i = 0; i < sizeof meshes / sizeof *meshes; i++)
        if (meshes[i].size == size && meshes[i].hash == hash)
            return (uint16_t)(i + 1);
    // Experimental starting-outfit carry variants: full quantized position sets
    // match pl_hands polygon0/7, on the same DrawOpa L/R nodes as idle6/13.
    // Raw61/62 avoid virtual equipment identities58..60. Model-only folding and
    // the ordinary observed-pose/verified-child checks remain separate below.
    if (size == 28672 && hash == 0x37b1ebaa03430dc8ull) return 61;
    if (size == 28672 && hash == 0x68780427621a9627ull) return 62;
    return false;
}

uint64_t sst_avatar_signature(const void *vertices, size_t size) {
    if (!vertices || size < 256) return 0;
    const uint8_t *v = vertices;
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 256; i++)
        hash = (hash ^ v[i]) * 1099511628211ull;
    return hash;
}

bool sst_avatar_mesh(const void *vertices, size_t size) {
    return sst_avatar_mesh_id(vertices, size) != 0;
}

uint16_t sst_avatar_pose_mesh(uint16_t mesh, size_t positions, size_t normals) {
    // Only model-only hands share a wrist pose identity. Geometry authorization,
    // local shape/outfit selection, palette layout, clip/action and phase stay separate.
    if (mesh == 61 || mesh == 62) {
        // These new descriptors were observed only as rigid hand geometry.
        if (positions || normals) return 0;
        return mesh == 61 ? 52 : 53;
    }
    if (positions || normals) return mesh;
    switch (mesh) {
    case 46: case 48: return 34; // al_handsL, also used by the observed HandLG geometry
    case 47: case 49: case 50: case 51: return 23; // al_handsR / HandRG
    // Starting-outfit hands (census, sessions 006-008): every variant of a side sits at exactly (26.50, 0, 0) in its
    // parent joint's frame (joint 3: 52 idle, 13 ZL; joint 5: 53 idle, 14 ZL, 54 sword grip) in every sighting, so
    // they are drawn from one hand node per side and share its pose: a remote keeps that hand placed whichever
    // variant the observer happens to draw (its finger shape stays the observer's).
    case 13: return 52;
    case 14: case 54: return 53;
    default: return mesh;
    }
}

bool sst_avatar_hand_parent(uint16_t pose_mesh, uint16_t *joint, uint32_t *positions) {
    // SST_MESH_CENSUS parent search, session 006 (client hands4-9, 1060+ presents for 52/53, 120 for 13/14):
    // translation deviation 0.00 against joints 3 / 5 of the 368-joint palettes (k6, k12; k9 shares the
    // prefix); rotation deviation up to 0.05 (52/53, idle and walk) and about 1.0 (13/14, clip 168).
    // Session 007 (explore/sword1): 54 on joint 5, translation 0.00, rotation up to 0.87 (sword drawn).
    static const struct { uint16_t mesh, joint; } parents[] = {{52, 3}, {13, 3}, {53, 5}, {14, 5}, {54, 5}};
    for (size_t i = 0; i < sizeof parents / sizeof *parents; i++)
        if (parents[i].mesh == pose_mesh) {
            if (joint) *joint = parents[i].joint;
            if (positions) *positions = 368 * 48;
            return true;
        }
    return false;
}

bool sst_avatar_local_draw(const SstPose *local, const float view[12], const float model[12]) {
    if (!local || !view || !model) return false;
    float distance=0;
    for (unsigned r=0;r<3;r++) {
        float origin=view[r*4+3];
        for (unsigned c=0;c<3;c++) {
            if (!isfinite(local->pos[c]) || !isfinite(view[r*4+c])) return false;
            origin+=view[r*4+c]*local->pos[c];
        }
        if (!isfinite(origin) || !isfinite(model[r*4+3])) return false;
        float delta=model[r*4+3]-origin;distance+=delta*delta;
    }
    return isfinite(distance) && distance<=256.0f*256.0f;
}

bool sst_avatar_local_skin(const SstPose *local,const float view[12],const float model[12],
                           const void *positions,size_t size) {
    if(sst_avatar_local_draw(local,view,model))return true;
    if(!local || !view || !positions || size>0x10000 || size%48)return false;
    unsigned nearby=0,count=(unsigned)(size/48);if(count>8)count=8;
    const float *bones=positions;
    for(unsigned i=0;i<count;i++) {
        const float *m=bones+i*12;bool valid=true;
        for(unsigned k=0;k<12;k++)if(!isfinite(m[k]))valid=false;
        for(unsigned row=0;row<3;row++) {
            float norm=0;for(unsigned col=0;col<3;col++)norm+=m[row*4+col]*m[row*4+col];
            if(!isfinite(norm) || norm<.0001f || norm>100)valid=false;
        }
        if(valid && sst_avatar_local_draw(local,view,m) && ++nearby>=2)return true;
    }
    return false;
}
bool sst_avatar_transform(const SstPose *local, const SstPose *remote, const float v[12], float out[12]) {
    for (int i = 0; i < 12; i++)
        if (!isfinite(v[i]))
            return false;
    for (int i = 0; i < 3; i++)
        if (!isfinite(local->pos[i]) || !isfinite(remote->pos[i]))
            return false;
    float angle = (int16_t)(remote->rot[1] - local->rot[1]) * (6.2831853071795864769f / 65536.0f);
    float c = cosf(angle), s = sinf(angle);
    float r[9] = {c, 0, s, 0, 1, 0, -s, 0, c}, vr[9] = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                vr[i * 3 + j] += v[i * 4 + k] * r[k * 3 + j];
    // V R V^-1. The view basis is orthonormal; its inverse is its transpose.
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float n = 0;
            for (int k = 0; k < 3; k++)
                n += vr[i * 3 + k] * v[j * 4 + k];
            out[i * 4 + j] = n;
        }
    for (int i = 0; i < 3; i++) {
        float t = v[i * 4 + 3];
        for (int k = 0; k < 3; k++) {
            t += v[i * 4 + k] * remote->pos[k] - vr[i * 3 + k] * local->pos[k];
            t -= out[i * 4 + k] * v[k * 4 + 3];
        }
        out[i * 4 + 3] = t;
    }
    return true;
}

void sst_avatar_matrix(float m[12], const float t[12], bool normal) {
    float result[12] = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 3; k++)
                result[i * 4 + j] += t[i * 4 + k] * m[k * 4 + j];
            if (j == 3 && !normal)
                result[i * 4 + j] += t[i * 4 + 3];
        }
    memcpy(m, result, sizeof result);
}

// ---- Visible equipment (sword): local state from this player's own draws, sent in the pose, used per remote.
static uint64_t equipment_present = UINT64_MAX;
static uint8_t equipment_now;
static atomic_uchar equipment_last;
bool sst_avatar_equipment(uint16_t mesh) { return mesh >= 55 && mesh <= 57; }
uint16_t sst_avatar_equipment_pose(uint16_t mesh, bool drawn) {
    return sst_avatar_equipment(mesh) && drawn ? (uint16_t)(mesh + 3) : mesh; // 58-60: captured with the sword drawn
}
void sst_avatar_note_local(uint16_t mesh, uint64_t present) {
    if (equipment_present != UINT64_MAX && present < equipment_present) sst_avatar_equipment_reset();
    if (present != equipment_present) {
        // A finished present decides: equipment drawn in it (a one-present lag is invisible in the replay)
        if (equipment_present != UINT64_MAX)
            atomic_store(&equipment_last, present == equipment_present + 1 ? equipment_now : 0);
        equipment_present = present;
        equipment_now = 0;
    }
    if (sst_avatar_equipment(mesh)) equipment_now |= SST_EQUIP_SWORD;
    if (mesh == 54) equipment_now |= SST_EQUIP_SWORD | SST_EQUIP_SWORD_DRAWN; // the sword-grip hand
}
uint8_t sst_avatar_local_equipment(void) { return atomic_load(&equipment_last); }
void sst_avatar_equipment_reset(void) {
    equipment_present = UINT64_MAX; equipment_now = 0; atomic_store(&equipment_last, 0);
}
uint16_t sst_avatar_remote_pose_mesh(uint16_t mesh, uint16_t pose_mesh, uint8_t remote_flags) {
    if (!sst_avatar_equipment(mesh)) return pose_mesh;
    if (!(remote_flags & SST_EQUIP_SWORD)) return 0; // that player has no sword: never show the observer's
    return sst_avatar_equipment_pose(mesh, remote_flags & SST_EQUIP_SWORD_DRAWN);
}
