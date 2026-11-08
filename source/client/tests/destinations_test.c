#include "../src/sst_destinations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void native_anchors(void) {
    const char *stage[]={"D100","D101","D200","D201","D300","D301","D003_7","B400","S000"};
    const unsigned room[]={0,0,1,0,0,0,0,0,0};
    for(unsigned i=0;i<sizeof stage/sizeof *stage;i++)
        assert(sst_destination_valid(stage[i],room[i],i==8?2:0,0));
    const SstDestination *academy=sst_destination_find("F001r",1,1,5);
    assert(academy && !strcmp(academy->zone,"Knight Academy"));
    // A logical LYSE-selected layer can share base resources. Rejecting it
    // because there is no separate l1 archive would reject a known live area.
    const SstDestination *sparring=sst_destination_find("F009r",0,1,0);
    assert(sparring && (sparring->flags&SST_DEST_SHARED_RESOURCES));
    const SstDestination *title=sst_destination_find("F000",0,26,55);
    assert(title && (title->flags&SST_DEST_EVENT_OR_TITLE));
    title=sst_destination_find("F000",0,28,48);
    assert(title && (title->flags&SST_DEST_EVENT_OR_TITLE));
    assert(sst_destination_find("Demo",0,0,0)->flags&SST_DEST_EVENT_OR_TITLE);
    // Metadata existence deliberately differs from permission/safe arrival.
    assert(sst_destination_valid("Demo",0,0,0));
    assert(!sst_destination_valid("F001r",2,0,0)); // no fabricated entrance0
    for(unsigned room=2;room<=4;room++) {
        const SstDestination *fallback=sst_destination_find("F001r",room,0,255);
        assert(fallback && (fallback->flags&SST_DEST_FALLBACK_FIRST));
        assert((fallback->flags&(SST_DEST_UNTESTED|SST_DEST_STORY_DEPENDENT))==
               (SST_DEST_UNTESTED|SST_DEST_STORY_DEPENDENT));
        assert(strstr(fallback->label,"first-record fallback"));
        assert(!sst_destination_find("F001r",room,0,65535)); // native u16 ID is not an argument
        assert(!sst_destination_find("F001r",room,255,255)); // no invented logical layer
    }
}
static void tuple_and_bounds(void) {
    assert(sst_destination_catalog_valid());
    assert(sst_destination_count()==6608 && sst_destination_count()<=SST_DESTINATION_LIMIT);
    assert(sst_destination_room_zone_count()==247);
    assert(!sst_destination_at(sst_destination_count()) && !sst_destination_at(SIZE_MAX));
    assert(!sst_destination_room_zone_at(sst_destination_room_zone_count()));
    assert(!sst_destination_room_zone_at(SIZE_MAX));
    assert(!sst_destination_find(NULL,0,0,0) && !sst_destination_find("",0,0,0));
    assert(!sst_destination_find("../../x",0,0,0) && !sst_destination_find("F00000000",0,0,0));
    assert(!sst_destination_find("F001r",256,0,0) && !sst_destination_find("F001r",1,256,0));
    assert(!sst_destination_find("F001r",1,1,256) && !sst_destination_find("ZZZ",0,0,0));
    size_t stages=0,rooms=0,disabled=0,fallback_rooms=0;const char *previous="";
    for(size_t i=0;i<sst_destination_count();i++) {
        const SstDestination *d=sst_destination_at(i);
        assert(d && d->region && *d->region && d->zone && *d->zone && d->label && *d->label);
        assert((d->flags&SST_DEST_VERIFIED) && (d->flags&SST_DEST_UNTESTED));
        assert(sst_destination_find(d->stage,d->room,d->layer,d->entrance)==d);
        if(d->flags&SST_DEST_FALLBACK_FIRST)assert(d->entrance==255 && (d->flags&SST_DEST_UNTESTED));
        if(strcmp(previous,d->stage)){stages++;previous=d->stage;}
    }
    assert(stages==102); // all native prepared-stage identifiers, not LA warp data
    for(size_t i=0;i<sst_destination_room_zone_count();i++) {
        const SstDestinationZone *z=sst_destination_room_zone_at(i);rooms++;
        assert(z->first<=sst_destination_count() && z->count<=sst_destination_count()-z->first);
        if(!z->count){assert(z->flags&SST_DEST_NO_SPAWN);disabled++;}
        else assert(!(z->flags&SST_DEST_NO_SPAWN));
        if(z->flags&SST_DEST_FALLBACK_FIRST)fallback_rooms++;
        for(size_t j=0;j<z->count;j++) {
            const SstDestination *d=sst_destination_at(z->first+j);
            assert(d && !strcmp(d->stage,z->stage) && d->room==z->room);
        }
    }
    assert(rooms==247 && disabled==0 && fallback_rooms==15);
}
static void search_and_filters(void) {
    size_t count=sst_destination_count();
    assert(sst_destination_search(NULL,0,0,NULL,0)==count);
    assert(sst_destination_search("  \t\n",0,0,NULL,0)==count);
    assert(!sst_destination_search("",0,0,NULL,1));
    char excessive[SST_DESTINATION_QUERY_MAX+2];memset(excessive,'x',sizeof excessive);excessive[sizeof excessive-1]=0;
    assert(!sst_destination_search(excessive,0,0,NULL,0));
    struct { size_t indices[2]; uint64_t canary; } out={{SIZE_MAX,SIZE_MAX},UINT64_C(0x1248abcdefff)};
    size_t total=sst_destination_search("fArOn SkYvIeW",0,0,out.indices,2);
    assert(total>2 && out.canary==UINT64_C(0x1248abcdefff));
    for(unsigned i=0;i<2;i++) {
        const SstDestination *d=sst_destination_at(out.indices[i]);
        assert(d && !strcmp(d->region,"Faron") && strstr(d->zone,"Skyview"));
    }
    assert(sst_destination_search("Skyloft Knight Academy",0,0,NULL,0)>0);
    assert(sst_destination_search("F009r l1",SST_DEST_SHARED_RESOURCES,0,NULL,0)>0);
    assert(sst_destination_search("",SST_DEST_BOSS_ARENA,0,NULL,0)>0);
    assert(sst_destination_search("",SST_DEST_EVENT_OR_TITLE,0,NULL,0)>0);
    assert(sst_destination_search("",0,SST_DEST_EVENT_OR_TITLE,NULL,0)<count);
    assert(!sst_destination_search("",SST_DEST_BOSS_ARENA,SST_DEST_BOSS_ARENA,NULL,0));
    assert(!sst_destination_search("",SST_DEST_NO_SPAWN,0,NULL,0));
    assert(sst_destination_search("first-record fallback",SST_DEST_FALLBACK_FIRST,0,NULL,0)==56);
    assert(sst_destination_search("F001r room2",SST_DEST_FALLBACK_FIRST,0,NULL,0)>0);
    assert(!sst_destination_search("no such native place",0,0,NULL,0));
    assert(!sst_destination_search("F000",0,SST_DEST_VERIFIED,NULL,0));
}
int main(void) {
    native_anchors();tuple_and_bounds();search_and_filters();
    printf("destinations metadata: %zu native tuples, %zu room-zones; no native arrival/execution claim\n",
           sst_destination_count(),sst_destination_room_zone_count());
    return 0;
}
