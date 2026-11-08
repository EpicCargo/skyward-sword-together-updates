#include "../src/sst_scene_history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <process.h>
#define history_test_mkdir(p) _mkdir(p)
#define history_test_pid _getpid
#else
#define history_test_mkdir(p) mkdir(p,0700)
#define history_test_pid getpid
#endif
static void owned_bit(uint8_t *bits,unsigned bit) { bits[bit/8]|=1u<<(bit%8); }
int main(void) {
    SstSceneHistory h={0},loaded={0};SstWorld w={0};uint8_t owned[SST_WORLD_BYTES]={0};
    assert(!sst_scene_history_record(&h,&w,127)); // no proof from arbitrary save data
    assert(sst_world_receive(&w,SST_WORLD_SCENE,127,1));
    assert(!sst_scene_history_record(&h,&w,127)); // not yet applied natively
    sst_world_applied(&w,SST_WORLD_SCENE,127);
    assert(sst_scene_history_record(&h,&w,127) && h.dirty);
    assert(!sst_scene_history_record(&h,&w,127));
    assert(sst_world_learn(&w,SST_WORLD_SCENE,126));
    assert(sst_scene_history_record(&h,&w,126));
    owned_bit(owned,127);owned_bit(owned,125); //127 retained,126 rolled back,125 unreviewed
    SstWorld replay={0};assert(sst_scene_history_recover(&h,owned,&replay)==1);
    assert(replay.known[SST_WORLD_SCENE][15]==128 && !replay.pending[SST_WORLD_SCENE][15]);
    sst_world_protect_scene(&replay,127);assert(!sst_scene_history_recover(&h,owned,&replay));
    assert(!sst_scene_history_record(&h,&w,SST_WORLD_BITS));
    char dir[1000];
#ifdef _WIN32
    char base[900];assert(GetTempPathA(sizeof base,base));
    snprintf(dir,sizeof dir,"%ssst-history-%lu",base,(unsigned long)history_test_pid());
    assert(!history_test_mkdir(dir));
#else
    snprintf(dir,sizeof dir,"/tmp/sst-history-XXXXXX");assert(mkdtemp(dir));
#endif
    char path[1100],bad[1100],temporary[1200],block[1100];
    snprintf(path,sizeof path,"%s/scenes.bin",dir);snprintf(bad,sizeof bad,"%s/bad.bin",dir);
    assert(sst_scene_history_load(&loaded,path)==SST_HISTORY_MISSING);
    assert(sst_scene_history_save(&h,path) && !h.dirty);
    assert(sst_scene_history_load(&loaded,path)==SST_HISTORY_OK && !memcmp(h.bits,loaded.bits,sizeof h.bits));
    FILE *f=fopen(path,"rb");uint8_t bytes[500];assert(f);size_t n=fread(bytes,1,sizeof bytes,f);fclose(f);assert(n==436);
    SstSceneHistory before=loaded;
    for(unsigned kind=0;kind<5;kind++) {
        uint8_t altered[500];memcpy(altered,bytes,n);size_t size=n;
        if(kind==0)altered[0]^=1;
        if(kind==1)altered[12]^=1;
        if(kind==2)altered[30]^=1;
        if(kind==3)size--;
        if(kind==4)altered[size++]=0;
        f=fopen(bad,"wb");assert(f && fwrite(altered,1,size,f)==size);fclose(f);
        assert(sst_scene_history_load(&loaded,bad)==SST_HISTORY_INVALID && !memcmp(&loaded,&before,sizeof loaded));
    }
    assert(sst_world_learn(&w,SST_WORLD_SCENE,124));assert(sst_scene_history_record(&h,&w,124));
    snprintf(temporary,sizeof temporary,"%s.tmp.%lu",path,(unsigned long)history_test_pid());
    f=fopen(temporary,"wb");assert(f);fputs("existing file must survive",f);fclose(f);
    assert(!sst_scene_history_save(&h,path) && h.dirty); // exclusive temp prevents clobber
    assert(sst_scene_history_load(&loaded,path)==SST_HISTORY_OK && !memcmp(loaded.bits,before.bits,sizeof loaded.bits));
    assert(!remove(temporary));assert(sst_scene_history_save(&h,path));
    assert(sst_scene_history_load(&loaded,path)==SST_HISTORY_OK && !memcmp(loaded.bits,h.bits,sizeof h.bits));
    snprintf(block,sizeof block,"%s/directory.bin",dir);assert(!history_test_mkdir(block));
    h.dirty=true;assert(!sst_scene_history_save(&h,block) && h.dirty);
    struct stat st;assert(!stat(block,&st) && S_ISDIR(st.st_mode));
    snprintf(temporary,sizeof temporary,"%s.tmp.%lu",block,(unsigned long)history_test_pid());assert(access(temporary,F_OK));
    SstWorld protected={0};sst_world_protect_scene(&protected,127);
    assert(sst_scene_history_prune(&h,&protected)==1 && h.dirty);
    assert(!(h.bits[15]&128) && (h.bits[15]&64));
    assert(!sst_scene_history_prune(&h,&protected));
    remove(path);remove(bad);rmdir(block);rmdir(dir);
    puts("scene history: admitted/applied proof, ownership intersection, protected receipts, atomic replacement, corruption/version/truncation, and failed-write preservation PASS");
}
