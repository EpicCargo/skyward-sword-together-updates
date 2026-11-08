#include "sst_dev_tools.h"
#include "sst_destinations.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static SstDevControls controls={.flight_speed=600};
static SstDevRequest request;
static SstDevStatus status;
static uint32_t serial;
SstDevControls sst_dev_controls(void) { pthread_mutex_lock(&mutex);SstDevControls out=controls;pthread_mutex_unlock(&mutex);return out; }
void sst_dev_set_controls(SstDevControls value) {
    if(!isfinite(value.flight_speed))value.flight_speed=600;
    value.flight_speed=fmaxf(50,fminf(3000,value.flight_speed));
    pthread_mutex_lock(&mutex);controls=value;pthread_mutex_unlock(&mutex);
}
static bool queue(SstDevRequest value) {
    pthread_mutex_lock(&mutex);
    bool accept=!status.busy && !request.kind;
    if(accept) { value.serial=++serial;if(!value.serial)value.serial=++serial;value.queued_ms=sst_now_ms();request=value;status.busy=true;snprintf(status.message,sizeof status.message,"Teleport queued; waiting for gameplay"); }
    pthread_mutex_unlock(&mutex);return accept;
}
bool sst_dev_queue_destination(size_t index) {
    if(!sst_destination_at(index))return false;
    return queue((SstDevRequest){.kind=SST_DEV_DESTINATION,.destination=index});
}
bool sst_dev_queue_peer(uint16_t peer) {
    if(!peer || peer==sst_net_my_id() || sst_net_status(NULL,0)!=SST_ONLINE)return false;
    uint32_t generation=sst_net_generation();SstPlayer players[SST_MAX_PLAYERS];
    int count=sst_net_players(players,SST_MAX_PLAYERS);
    if(generation!=sst_net_generation())return false;
    for(int i=0;i<count;i++)if(players[i].active && players[i].id==peer && players[i].scene.stage[0]) {
        SstDevRequest value={.kind=SST_DEV_PEER,.peer=peer,.connection_generation=generation,
            .presence_generation=players[i].presence_generation,.peer_scene_generation=players[i].udp_scene,
            .peer_scene=players[i].scene};
        return queue(value);
    }
    return false;
}
bool sst_dev_take_request(SstDevRequest *out) {
    if(!out)return false;
    pthread_mutex_lock(&mutex);bool have=request.kind!=SST_DEV_NONE;
    if(have){*out=request;request=(SstDevRequest){0};}
    pthread_mutex_unlock(&mutex);return have;
}
SstDevStatus sst_dev_status(void) {pthread_mutex_lock(&mutex);SstDevStatus out=status;pthread_mutex_unlock(&mutex);return out;}
void sst_dev_report(bool available,bool busy,const char *text) {
    pthread_mutex_lock(&mutex);status.available=available;status.busy=busy || request.kind!=SST_DEV_NONE;
    if(text)snprintf(status.message,sizeof status.message,"%s",text);
    pthread_mutex_unlock(&mutex);
}
void sst_dev_finish(bool success,const char *text) {
    pthread_mutex_lock(&mutex);status.busy=false;
    if(success)status.completed++;else status.rejected++;
    snprintf(status.message,sizeof status.message,"%s",text?text:success?"Teleport complete":"Teleport cancelled");
    pthread_mutex_unlock(&mutex);
}
void sst_dev_reset(void) {
    pthread_mutex_lock(&mutex);controls=(SstDevControls){.flight_speed=600};request=(SstDevRequest){0};status=(SstDevStatus){0};serial=0;pthread_mutex_unlock(&mutex);
}
bool sst_dev_flight_delta(const float view[12],int32_t x,int32_t y,bool up,bool down,bool boost,float speed,double seconds,float out[3]) {
    if(!view || !out || !isfinite(speed) || speed<50 || speed>3000 || !isfinite(seconds) || seconds<0 || seconds>.1)return false;
    for(unsigned i=0;i<12;i++)if(!isfinite(view[i]))return false;
    float right[2]={view[0],view[2]},forward[2]={-view[8],-view[10]};
    float rn=hypotf(right[0],right[1]),fn=hypotf(forward[0],forward[1]);
    if(rn<.001f && fn<.001f)return false;
    if(rn<.001f){right[0]=-forward[1]/fn;right[1]=forward[0]/fn;rn=1;}
    if(fn<.001f){forward[0]=right[1]/rn;forward[1]=-right[0]/rn;fn=1;}
    float lx=fmaxf(-1,fminf(1,x/32767.f)),ly=fmaxf(-1,fminf(1,y/32767.f));
    if(hypotf(lx,ly)<.18f)lx=ly=0;
    float direction[3]={lx*right[0]/rn+ly*forward[0]/fn,(float)up-(float)down,lx*right[1]/rn+ly*forward[1]/fn};
    float length=sqrtf(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
    if(length>1)for(unsigned i=0;i<3;i++)direction[i]/=length;
    float result[3];for(unsigned i=0;i<3;i++)result[i]=direction[i]*speed*(boost?3:1)*(float)seconds;
    memcpy(out,result,sizeof result);return true;
}
