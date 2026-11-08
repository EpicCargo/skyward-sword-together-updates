#include "sst_scene_history.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#define history_open _open
#define history_fdopen _fdopen
#define history_close _close
#define history_sync _commit
#define history_pid _getpid
#else
#define history_open open
#define history_fdopen fdopen
#define history_close close
#define history_sync fsync
#define history_pid getpid
#endif

#define HEADER 20u
#define FILE_BYTES (HEADER + SST_WORLD_BYTES)
#define GAME_TAG 0x53530100u // SSHD 1.0.0; wire protocol is deliberately independent.
static const uint8_t magic[8] = {'S','S','T','S','C','N','1',0};
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t checksum(const uint8_t *data) {
    uint32_t h=2166136261u;
    for (unsigned i=0;i<FILE_BYTES;i++) if (i<16 || i>=HEADER) h=(h^data[i])*16777619u;
    return h;
}
bool sst_scene_history_record(SstSceneHistory *h, const SstWorld *w, unsigned bit) {
    if (!sst_world_flag_allowed(SST_WORLD_SCENE,bit)) return false;
    unsigned byte=bit/8;uint8_t mask=1u<<(bit%8);
    if (!(w->known[SST_WORLD_SCENE][byte]&mask) || (w->pending[SST_WORLD_SCENE][byte]&mask) ||
        (w->scene_receipts[byte]&mask) || (h->bits[byte]&mask)) return false;
    h->bits[byte]|=mask;h->dirty=true;return true;
}
unsigned sst_scene_history_recover(const SstSceneHistory *h,const uint8_t owned[SST_WORLD_BYTES],SstWorld *w) {
    unsigned count=0;
    for (unsigned bit=0;bit<SST_WORLD_BITS;bit++) {
        unsigned byte=bit/8;uint8_t mask=1u<<(bit%8);
        if ((h->bits[byte]&owned[byte]&mask) && sst_world_learn(w,SST_WORLD_SCENE,bit)) count++;
    }
    return count;
}
unsigned sst_scene_history_prune(SstSceneHistory *h,const SstWorld *w) {
    unsigned count=0;
    for(unsigned bit=0;bit<SST_WORLD_BITS;bit++) {
        unsigned byte=bit/8;uint8_t mask=1u<<(bit%8);
        if((h->bits[byte]&mask) && (!sst_world_flag_allowed(SST_WORLD_SCENE,bit) || (w->scene_receipts[byte]&mask))) {
            h->bits[byte]&=(uint8_t)~mask;h->dirty=true;count++;
        }
    }
    return count;
}
SstHistoryResult sst_scene_history_load(SstSceneHistory *h,const char *path) {
    FILE *f=fopen(path,"rb");
    if (!f) return errno==ENOENT?SST_HISTORY_MISSING:SST_HISTORY_IO;
    uint8_t data[FILE_BYTES+1];size_t n=fread(data,1,sizeof data,f);
    bool error=ferror(f);if (fclose(f)) error=true;
    if (error) return SST_HISTORY_IO;
    if (n!=FILE_BYTES || memcmp(data,magic,sizeof magic) || get32(data+8)!=SST_WORLD_BITS ||
        get32(data+12)!=GAME_TAG || get32(data+16)!=checksum(data)) return SST_HISTORY_INVALID;
    memcpy(h->bits,data+HEADER,SST_WORLD_BYTES);h->dirty=false;return SST_HISTORY_OK;
}
bool sst_scene_history_save(SstSceneHistory *h,const char *path) {
    uint8_t data[FILE_BYTES]={0};memcpy(data,magic,sizeof magic);
    put32(data+8,SST_WORLD_BITS);put32(data+12,GAME_TAG);memcpy(data+HEADER,h->bits,SST_WORLD_BYTES);
    put32(data+16,checksum(data));
    char temporary[1200];int n=snprintf(temporary,sizeof temporary,"%s.tmp.%lu",path,(unsigned long)history_pid());
    if (n<0 || (size_t)n>=sizeof temporary) return false;
    int flags=O_WRONLY|O_CREAT|O_EXCL;
#ifdef _WIN32
    flags|=O_BINARY;
#endif
    int fd=history_open(temporary,flags,0600);if (fd<0) return false;
    FILE *f=history_fdopen(fd,"wb");
    if (!f) {history_close(fd);remove(temporary);return false;}
    bool ok=fwrite(data,1,sizeof data,f)==sizeof data && !fflush(f) && !history_sync(fileno(f));
    if (fclose(f)) ok=false;
    if (ok) {
#ifdef _WIN32
        ok=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        ok=rename(temporary,path)==0;
#endif
    }
    if (!ok) remove(temporary);
    else h->dirty=false;
    return ok;
}
