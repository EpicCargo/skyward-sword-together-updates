// Read-only typed census of one owned SSHD1.0.0 child. No process/PID API,
// guest writes, model calls, saved pointers or production bridge/cache dependency.
#ifndef SST_OWNED_PROP_CENSUS_H
#define SST_OWNED_PROP_CENSUS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

enum { OWNED_CENSUS_NODES=2048, OWNED_CENSUS_PROPS=128 };
typedef bool (*OwnedCensusRead)(void *, uint64_t, void *, size_t);
typedef struct {
    uint64_t actor;
    uint32_t instance, params, state;
    uint16_t placed_id, profile;
    uint8_t room;
    bool held_by_native;
    float pos[3];
} OwnedPropRecord;
typedef struct {
    uint32_t nodes, count, player_instance;
    OwnedPropRecord items[OWNED_CENSUS_PROPS];
} OwnedPropSnapshot;
typedef struct {
    OwnedCensusRead read;
    void *context;
    uint64_t low, high;
} OwnedCensusReader;

static bool owned_census_read(const OwnedCensusReader *r, uint64_t at, void *out, size_t bytes) {
    return r && r->read && at>=r->low && at<r->high && bytes<=r->high-at &&
        r->read(r->context,at,out,bytes);
}
static uint16_t owned_u16(const unsigned char *p) {return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t owned_u32(const unsigned char *p) {return (uint32_t)owned_u16(p)|(uint32_t)owned_u16(p+2)<<16;}
static uint64_t owned_u64(const unsigned char *p) {return (uint64_t)owned_u32(p)|(uint64_t)owned_u32(p+4)<<32;}
static bool owned_read_u64(const OwnedCensusReader *r,uint64_t at,uint64_t *out) {
    unsigned char bytes[8];if(!owned_census_read(r,at,bytes,sizeof bytes))return false;
    *out=owned_u64(bytes);return true;
}
static bool owned_prop_profile(uint16_t profile,uint64_t vtable,uint32_t params) {
    if(profile==496)return vtable==UINT64_C(0x700162f008);
    if(profile==497)return vtable==UINT64_C(0x700162f3b0);
    return profile==521 && vtable==UINT64_C(0x70015f6290) &&
        ((params&15u)==0 || (params&15u)==15) && ((params>>6)&3u)>=2;
}
static bool owned_prop_census(const OwnedCensusReader *r,uint64_t player,OwnedPropSnapshot *out) {
    if(!out)return false;
    unsigned char player_before[24],player_after[24];uint64_t held=0;
    if(!owned_census_read(r,player,player_before,sizeof player_before) ||
        owned_u16(player_before+16)!=200 || player_before[18]!=1 || player_before[19] || player_before[20] ||
        !owned_u32(player_before+8) || !owned_read_u64(r,player+0x1b90,&held))return false;
    uint64_t root=player+0x18;
    for(unsigned depth=0;;depth++) {
        uint64_t parent;
        if(depth==64 || !owned_read_u64(r,root,&parent))return false;
        if(!parent)break;
        root=parent;
    }
    // This independent implementation uses a bounded visited list, not the
    // production bridge's hash walker or its binding cache.
    uint64_t pending[OWNED_CENSUS_NODES],visited[OWNED_CENSUS_NODES];unsigned left=1;
    pending[0]=root;OwnedPropSnapshot candidate={.player_instance=owned_u32(player_before+8)};
    while(left) {
        uint64_t node=pending[--left];unsigned char links[40],head[24];
        if(node<r->low+0x18 || candidate.nodes==OWNED_CENSUS_NODES)return false;
        for(unsigned i=0;i<candidate.nodes;i++)if(visited[i]==node)return false;
        if(!owned_census_read(r,node,links,sizeof links))return false;
        uint64_t actor=node-0x18;
        if(owned_u64(links+32)!=actor || !owned_census_read(r,actor,head,sizeof head))return false;
        visited[candidate.nodes++]=node;
        uint16_t profile=owned_u16(head+16);uint32_t params=owned_u32(head+12);
        if(head[18]==1 && !head[19] && !head[20] && owned_prop_profile(profile,owned_u64(head),params)) {
            unsigned char identity[2],state[4],again[24];uint8_t room,type=0;
            if(!owned_census_read(r,actor+0x136,identity,2) ||
                !owned_census_read(r,actor+0x17c,&room,1) ||
                !owned_census_read(r,actor+0x358,state,4))return false;
            if(profile==521 && !owned_census_read(r,actor+0x1367,&type,1))return false;
            if(owned_u16(identity)!=0xffff && !type) {
                if(candidate.count==OWNED_CENSUS_PROPS || !owned_u32(head+8))return false;
                OwnedPropRecord *item=&candidate.items[candidate.count++];
                *item=(OwnedPropRecord){.actor=actor,.instance=owned_u32(head+8),.params=params,
                    .state=owned_u32(state),.placed_id=owned_u16(identity),.profile=profile,.room=room,
                    .held_by_native=held==actor};
                if(!owned_census_read(r,actor+0x144,item->pos,sizeof item->pos))return false;
                for(unsigned i=0;i<3;i++)if(!isfinite(item->pos[i]) || fabsf(item->pos[i])>1000000)return false;
                if(!owned_census_read(r,actor,again,sizeof again) || memcmp(head,again,sizeof head))return false;
                unsigned char check_identity[2];uint8_t check_room;
                if(!owned_census_read(r,actor+0x136,check_identity,2) || memcmp(identity,check_identity,2) ||
                    !owned_census_read(r,actor+0x17c,&check_room,1) || room!=check_room)return false;
            }
        }
        uint64_t child=owned_u64(links+8),next=owned_u64(links+24);
        if(child) {if(left==OWNED_CENSUS_NODES)return false;pending[left++]=child;}
        if(next) {if(left==OWNED_CENSUS_NODES)return false;pending[left++]=next;}
    }
    uint64_t held_after;
    if(!owned_census_read(r,player,player_after,sizeof player_after) ||
        memcmp(player_before,player_after,sizeof player_before) ||
        !owned_read_u64(r,player+0x1b90,&held_after) || held_after!=held)return false;
    *out=candidate;return true;
}
#endif
