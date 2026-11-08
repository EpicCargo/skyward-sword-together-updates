// Public state behavior only. No actor addresses, sockets, game, or graphics.
#include "sst_props.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SstProp vase(unsigned id) {
    return (SstProp){.scene={.stage="F000",.room=0,.layer=1},
        .key={.placed_id=(uint16_t)id,.profile=SST_PROP_PROFILE,.params=0xfffffff0,.room=0,.layer=1},
        .sequence=1,.pose_tick=100,.phase=SST_PROP_PLACE,
        .matrix={1,0,0,0,0,1,0,0,0,0,1,0}};
}

static unsigned used(const SstPropState *state) {
    unsigned result=0;
    for(unsigned i=0;i<SST_PROPS;i++) result+=state->props[i].used;
    return result;
}

static void departed_object_capacity(void) {
    SstProp seed=vase(1),out;SstPropState state={0};
    SstPropPresence live[2]={{1,1},{2,1}};
    sst_props_begin(&state,&seed.scene,1);assert(sst_props_roster(&state,live,2));
    for(unsigned i=0;i<SST_PROPS;i++) {
        SstProp p=vase(10+i);assert(sst_props_update(&state,2,1,&p,1000));
    }
    assert(used(&state)==SST_PROPS);
    live[1]=(SstPropPresence){3,2};assert(sst_props_roster(&state,live,2));
    for(unsigned i=0;i<SST_PROPS;i++) {
        SstProp old=vase(10+i);assert(!sst_props_get(&state,&old.key,1100,&out));
        old.sequence=999;assert(!sst_props_update(&state,2,1,&old,1100));
        SstProp next=vase(100+i);assert(sst_props_update(&state,3,2,&next,1100));
    }
    assert(used(&state)==SST_PROPS);
    puts("PASS departed-object capacity:32 new objects after retiring32 old objects; retired packets rejected");
}

static void stable_placement_and_churn(void) {
    SstProp stable=vase(1),out;SstPropState state={0};
    SstPropPresence live[2]={{1,1},{2,1}};
    sst_props_begin(&state,&stable.scene,1);assert(sst_props_roster(&state,live,2));
    assert(sst_props_update(&state,1,1,&stable,1000));
    for(unsigned wave=0;wave<100;wave++) {
        for(unsigned i=0;i<SST_PROPS-1;i++) {
            SstProp p=vase(10+wave*(SST_PROPS-1)+i);
            assert(sst_props_update(&state,live[1].id,live[1].generation,&p,1000+wave));
        }
        SstPropPresence old=live[1];live[1]=(SstPropPresence){(uint16_t)(3+wave),2+wave};
        assert(sst_props_roster(&state,live,2));
        assert(sst_props_get(&state,&stable.key,1200+wave,&out) && out.from==1 && out.sequence==1);
        assert(!sst_props_update(&state,1,1,&stable,1200+wave));
        SstProp late=vase(10+wave*(SST_PROPS-1));late.sequence=999;
        assert(!sst_props_update(&state,old.id,old.generation,&late,1200+wave));
    }
    assert(used(&state)==1);
    puts("PASS stable placement:3100 distinct retired objects reclaimed; stable target and duplicate rejection retained");
}

static void active_contender_watermark(void) {
    SstProp p=vase(1),out;SstPropState state={0};
    SstPropPresence live[3]={{1,1},{2,1},{3,1}};
    sst_props_begin(&state,&p.scene,1);assert(sst_props_roster(&state,live,3));
    p.phase=SST_PROP_CARRY;assert(sst_props_update(&state,2,1,&p,1000));
    p.sequence=77;assert(!sst_props_update(&state,3,1,&p,1000));
    live[1]=(SstPropPresence){4,2};assert(sst_props_roster(&state,live,3));
    assert(!sst_props_get(&state,&p.key,1100,&out));
    assert(!sst_props_update(&state,3,1,&p,1100));
    for(unsigned i=0;i<SST_PROPS-1;i++) {
        SstProp fill=vase(100+i);assert(sst_props_update(&state,4,2,&fill,1100));
    }
    SstProp extra=vase(500);assert(!sst_props_update(&state,4,2,&extra,1100));
    // A targetless key still carries a living contender's replay watermark.
    assert(sst_props_roster(&state,live,3));assert(!sst_props_update(&state,3,1,&p,1100));
    p.sequence=78;assert(sst_props_update(&state,3,1,&p,1100));
    assert(sst_props_get(&state,&p.key,1100,&out) && out.from==3 && out.sequence==78);
    // It becomes reclaimable only after that last evidence-owning stream leaves.
    assert(sst_props_roster(&state,live,2));
    assert(sst_props_update(&state,4,2,&extra,1200));
    puts("PASS live replay evidence:targetless contender watermark kept until that stream leaves; no live placement eviction");
}

int main(int argc,char **argv) {
    assert(argc==2);int mode=atoi(argv[1]);
    if(mode==1)departed_object_capacity();
    else if(mode==2)stable_placement_and_churn();
    else if(mode==3)active_contender_watermark();
    else return 2;
    return 0;
}
