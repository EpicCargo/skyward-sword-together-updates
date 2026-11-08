// Candidate-only scalar state fixture. No sockets, native actors, game or GPU.
#include "../src/sst_props.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SstProp vase(unsigned id) {
    return (SstProp){.scene={.stage="F000",.room=0,.layer=1,.entrance=4},
        .key={.placed_id=(uint16_t)id,.profile=SST_PROP_PROFILE,.params=0xfffffff0,.room=0,.layer=1},
        .sequence=1,.pose_tick=100,.phase=SST_PROP_PLACE,
        .matrix={1,0,0,0,0,1,0,0,0,0,1,0}};
}

static void roster_churn(void) {
    SstProp p=vase(20), out;SstPropState state={0};
    SstPropPresence live[4]={{1,1},{2,7},{3,8},{4,9}};
    sst_props_begin(&state,&p.scene,1);
    assert(sst_props_roster(&state,live,4));
    for(unsigned i=0;i<4;i++) {
        p=vase(20+i);
        assert(sst_props_update(&state,live[i].id,live[i].generation,&p,1000));
    }
    live[1]=(SstPropPresence){5,10};
    assert(sst_props_roster(&state,live,4));
    p=vase(21);assert(!sst_props_get(&state,&p.key,1100,&out));
    p.phase=SST_PROP_CARRY;
    assert(sst_props_update(&state,5,10,&p,1100));
    assert(!sst_props_update(&state,2,7,&p,1100));
    assert(!sst_props_update(&state,2,99,&p,1100));
    assert(!sst_props_update(&state,5,9,&p,1100));
    for(unsigned i=0;i<1000;i++) {
        SstPropPresence old=live[1];
        live[1]=(SstPropPresence){(uint16_t)(6+i),11+i};
        // Roster order does not assign or reset the stable peers' stream slots.
        SstPropPresence reordered[4]={live[3],live[1],live[0],live[2]};
        assert(sst_props_roster(&state,reordered,4));
        assert(sst_props_update(&state,live[1].id,live[1].generation,&p,1200+i));
        SstProp late=p;late.sequence=999;
        assert(!sst_props_update(&state,old.id,old.generation,&late,1200+i));
        assert(sst_props_get(&state,&p.key,1200+i,&out) && out.from==live[1].id);
        SstProp stable=vase(22);
        assert(sst_props_get(&state,&stable.key,1200+i,&out) && out.from==3 && out.sequence==1);
        assert(!sst_props_update(&state,3,8,&stable,1200+i));
    }
    // Local network generation and remote presence generations are distinct
    // counters. A low local generation must remain usable after remote churn.
    p=vase(20);p.sequence=2;
    assert(sst_props_update(&state,1,1,&p,3000));
    puts("roster churn: 1000 replacements, delayed retired streams rejected, stable targets/watermarks and local generation retained");
}

static void roster_input_and_reentry(void) {
    SstProp p=vase(7),out;SstPropState state={0};
    SstPropPresence live[2]={{1,1},{2,7}};
    sst_props_begin(&state,&p.scene,1);assert(sst_props_roster(&state,live,2));
    assert(sst_props_update(&state,2,7,&p,1000));
    SstPropState unchanged=state;
    SstPropPresence duplicate[2]={{1,1},{1,2}},zero={0,1};
    assert(!sst_props_roster(&state,duplicate,2));
    assert(!memcmp(&state,&unchanged,sizeof state));
    assert(!sst_props_roster(&state,&zero,1));
    assert(!sst_props_roster(&state,live,SST_MAX_PLAYERS+1));
    assert(!sst_props_roster(&state,NULL,1));
    assert(!memcmp(&state,&unchanged,sizeof state));
    // A player can leave this area without changing their socket incarnation.
    assert(sst_props_roster(&state,live,1));
    p.sequence=2;
    assert(!sst_props_update(&state,2,7,&p,1100));
    assert(!sst_props_get(&state,&p.key,1100,&out));
    // Only the caller's explicit authenticated roster can authorize the return.
    assert(sst_props_roster(&state,live,2));
    assert(sst_props_update(&state,2,7,&p,1200));
    sst_props_forget(&state,2,7);p.sequence=3;
    assert(!sst_props_update(&state,2,7,&p,1201));
    assert(sst_props_roster(&state,NULL,0));
    assert(!sst_props_update(&state,1,1,&p,1202));
    assert(!sst_props_get(&state,&p.key,1202,&out));
    puts("roster authority: invalid snapshots leave state unchanged; area reentry requires explicit authorization; forgotten/unknown streams stay rejected");
}

static void roster_generation_wrap(void) {
    SstProp p=vase(7),out;SstPropState state={0};
    SstPropPresence live={65535,UINT32_MAX};
    sst_props_begin(&state,&p.scene,1);assert(sst_props_roster(&state,&live,1));
    p.sequence=UINT32_MAX;assert(sst_props_update(&state,65535,UINT32_MAX,&p,1000));
    live.generation=0;
    assert(sst_props_roster(&state,&live,1));
    assert(!sst_props_get(&state,&p.key,1001,&out));
    p.sequence=0;assert(sst_props_update(&state,65535,0,&p,1001));
    p.sequence=1;assert(!sst_props_update(&state,65535,UINT32_MAX,&p,1002));
    assert(sst_props_get(&state,&p.key,1002,&out) && out.presence_generation==0);
    puts("roster wrap: explicit current incarnation handles uint32 wrap and rejects the retired incarnation");
}

int main(void) {
    roster_churn();roster_input_and_reentry();roster_generation_wrap();
    puts("candidate roster CPU checks PASS; synthetic scalar state only");
    return 0;
}
