#include "../src/sst_avatar.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void point(const float m[12], const float p[3], float out[3]) {
    for (int i = 0; i < 3; i++)
        out[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
}
int main(void) {
    // A rotated, translated camera, two players with different yaw, and a bone at local feet.
    const float view[12] = {0, 0, 1, 100, 0, 1, 0, -80, -1, 0, 0, -300};
    SstPose local = {.pos = {250, 10, -400}, .rot = {0, 0, 0}},
            remote = {.pos = {500, 20, -250}, .rot = {0, 16384, 0}};
    float t[12], bone[12], expected[3], a[3], b[3];
    assert(sst_avatar_transform(&local, &remote, view, t));
    memcpy(bone, view, sizeof bone);
    point(view, local.pos, a);
    bone[3] = a[0];
    bone[7] = a[1];
    bone[11] = a[2];
    sst_avatar_matrix(bone, t, false);
    point(view, remote.pos, expected);
    for (int i = 0; i < 3; i++)
        assert(fabsf(bone[i * 4 + 3] - expected[i]) < .001f);
    // An offset of +X at the local feet becomes -Z for a quarter-turn about Y.
    float offset[3] = {251, 10, -400};
    point(view, offset, a);
    point(t, a, b);
    float rotated[3] = {500, 20, -251};
    point(view, rotated, expected);
    for (int i = 0; i < 3; i++)
        assert(fabsf(b[i] - expected[i]) < .001f);
    float normal[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    sst_avatar_matrix(normal, t, true);
    assert(normal[3] == 0 && normal[7] == 0 && normal[11] == 0);
    remote.pos[0] = NAN;
    assert(!sst_avatar_transform(&local, &remote, view, t));
    unsigned char junk[256] = {0};
    assert(!sst_avatar_mesh(junk, sizeof junk));
    assert(!sst_avatar_signature(NULL, 256) && !sst_avatar_signature(junk, 255));
    uint64_t signature=sst_avatar_signature(junk,sizeof junk);junk[255]=1;
    assert(signature && sst_avatar_signature(junk,sizeof junk)!=signature);
    assert(sst_avatar_pose_mesh(13,0,0)==52 && sst_avatar_pose_mesh(14,0,0)==53);
    assert(sst_avatar_pose_mesh(54,0,0)==53 && sst_avatar_pose_mesh(13,48,0)==13);
    assert(sst_avatar_pose_mesh(54,0,48)==54);
    sst_avatar_equipment_reset();assert(!sst_avatar_local_equipment());
    sst_avatar_note_local(55,100);sst_avatar_note_local(6,101);
    assert(sst_avatar_local_equipment()==SST_EQUIP_SWORD);
    sst_avatar_note_local(54,101);sst_avatar_note_local(56,101);sst_avatar_note_local(6,102);
    assert(sst_avatar_local_equipment()==(SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN));
    sst_avatar_note_local(6,103);assert(!sst_avatar_local_equipment());
    sst_avatar_note_local(55,103);sst_avatar_note_local(6,110);assert(!sst_avatar_local_equipment());
    sst_avatar_note_local(54,110);sst_avatar_note_local(6,1);assert(!sst_avatar_local_equipment());
    for(uint16_t mesh=55;mesh<=57;mesh++) {
        assert(sst_avatar_equipment(mesh));
        assert(!sst_avatar_remote_pose_mesh(mesh,mesh,0));
        assert(!sst_avatar_remote_pose_mesh(mesh,mesh,SST_EQUIP_SWORD_DRAWN));
        assert(sst_avatar_remote_pose_mesh(mesh,mesh+3,SST_EQUIP_SWORD)==mesh);
        assert(sst_avatar_remote_pose_mesh(mesh,mesh,SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN)==mesh+3);
    }
    assert(!sst_avatar_equipment(54) && !sst_avatar_equipment(58));
    assert(sst_avatar_remote_pose_mesh(53,53,0)==53);
    local=(SstPose){.pos={250,10,-400}};
    float current[12];memcpy(current,view,sizeof current);point(view,local.pos,a);
    current[3]=a[0];current[7]=a[1]+150;current[11]=a[2];
    assert(sst_avatar_local_draw(&local,view,current));
    current[3]+=1000;assert(!sst_avatar_local_draw(&local,view,current));
    current[3]=NAN;assert(!sst_avatar_local_draw(&local,view,current));
    // Skin matrices remain in view space while a separate model origin can leave the body bounds.
    current[3]=a[0]+1000;float skin[36];
    for(unsigned i=0;i<3;i++) {memcpy(skin+i*12,view,48);skin[i*12+3]=a[0];skin[i*12+7]=a[1]+30*i;skin[i*12+11]=a[2];}
    assert(sst_avatar_local_skin(&local,view,current,skin,sizeof skin));
    assert(!sst_avatar_local_skin(&local,view,current,skin,48));
    for(unsigned i=0;i<3;i++)skin[i*12+3]+=1000;
    assert(!sst_avatar_local_skin(&local,view,current,skin,sizeof skin));
    memset(skin,0,sizeof skin);assert(!sst_avatar_local_skin(&local,view,current,skin,sizeof skin));
    skin[0]=NAN;assert(!sst_avatar_local_skin(&local,view,current,skin,sizeof skin));
    assert(!sst_avatar_local_skin(&local,view,current,skin,47));
    puts("avatar transforms: translated/rotated camera, remote yaw, bone position, normals and invalid data "
         "PASS");
}
