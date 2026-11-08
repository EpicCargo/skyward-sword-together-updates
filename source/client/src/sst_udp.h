// Optional, replaceable movement channel. TCP owns identity, scenes and durable events.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

enum { SST_UDP_OFFER = 8, SST_UDP_READY = 9, SST_UDP_SCENE = 10 };
#define SST_UDP_HEADER 29
#define SST_UDP_MAX 192
#define SST_UDP_TIMEOUT 1500
#define SST_UDP_PROBE_MS 500

static inline bool sst_udp_pose_valid(const uint8_t *p, size_t n) {
    if (n!=36) return false;
    const unsigned offsets[]={4,8,12,24};
    for (unsigned i=0;i<4;i++) {
        float value; memcpy(&value,p+offsets[i],4);
        if (!isfinite(value) || fabsf(value)>1000000) return false;
    }
    return true;
}

static inline uint32_t sst_udp_u32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static inline void sst_udp_put32(uint8_t *p, uint32_t v) {
    for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
// CRC catches accidental corruption (also injected by the chaos tool). Not encryption/MAC.
static inline uint32_t sst_udp_crc(const uint8_t *p, size_t n) {
    uint32_t crc=~0u;
    for (size_t i=0;i<n;i++) {
        if (i>=25 && i<29) continue;
        crc^=p[i];
        for (int b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}
static inline size_t sst_udp_pack(uint8_t out[SST_UDP_MAX], const uint8_t token[16],
                                 uint32_t seq, uint8_t type, const uint8_t *body, size_t n) {
    if (n>SST_UDP_MAX-SST_UDP_HEADER) return 0;
    memcpy(out,"SSU1",4); memcpy(out+4,token,16); sst_udp_put32(out+20,seq); out[24]=type;
    memcpy(out+29,body,n); sst_udp_put32(out+25,sst_udp_crc(out,n+29)); return n+29;
}
static inline bool sst_udp_valid(const uint8_t *p, size_t n, const uint8_t token[16], uint32_t last) {
    return n>=SST_UDP_HEADER && n<=SST_UDP_MAX && !memcmp(p,"SSU1",4) &&
        !memcmp(p+4,token,16) && (int32_t)(sst_udp_u32(p+20)-last)>0 &&
        sst_udp_u32(p+25)==sst_udp_crc(p,n);
}
// Small token bucket. Probe loss halves the budget; successful probes grow it slowly.
typedef struct { uint64_t at; unsigned credit, rate; } SstUdpBudget;
static inline void sst_udp_feedback(SstUdpBudget *b, bool good) {
    if (!b->rate) b->rate=6000;
    b->rate=good ? (b->rate<23500 ? b->rate+500 : 24000) : (b->rate>3000 ? b->rate/2 : 1500);
    if (b->credit>b->rate/4) b->credit=b->rate/4;
}
static inline bool sst_udp_spend(SstUdpBudget *b, uint64_t now, size_t bytes) {
    if (!b->rate) { b->rate=6000; b->credit=1500; b->at=now; }
    uint64_t add=(now-b->at)*b->rate/1000;
    if (add) { b->credit=(unsigned)(add+b->credit>b->rate/4 ? b->rate/4 : add+b->credit); b->at=now; }
    if (bytes>b->credit) return false;
    b->credit-=(unsigned)bytes; return true;
}
