#include "../src/sst_dev_tools.h"
#include "../src/sst_destinations.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
uint16_t sst_net_my_id(void){return 7;}
uint64_t sst_now_ms(void){return 1000;}
uint32_t sst_net_generation(void){return 23;}
SstStatus sst_net_status(char *why,int capacity){(void)why;(void)capacity;return SST_ONLINE;}
int sst_net_players(SstPlayer *out,int capacity){
 if(!out||capacity<1)return 0;
 *out=(SstPlayer){.id=2,.active=true,.presence_generation=12,.udp_scene=9};
 strcpy(out->scene.stage,"F001r");out->scene.room=1;out->scene.layer=1;return 1;
}
static void near(float a,float b){assert(fabsf(a-b)<.001f);}
int main(void){
 sst_dev_reset();SstDevControls c=sst_dev_controls();assert(!c.moon_jump&&!c.noclip);near(c.flight_speed,600);
 c.moon_jump=c.noclip=true;c.flight_speed=NAN;sst_dev_set_controls(c);near(sst_dev_controls().flight_speed,600);
 c.flight_speed=-1;sst_dev_set_controls(c);near(sst_dev_controls().flight_speed,50);
 c.flight_speed=1e8;sst_dev_set_controls(c);near(sst_dev_controls().flight_speed,3000);
 assert(!sst_dev_queue_destination(sst_destination_count()));assert(sst_dev_queue_destination(0));assert(!sst_dev_queue_peer(2));
 SstDevRequest q={0};assert(!sst_dev_take_request(NULL));assert(sst_dev_take_request(&q));assert(q.kind==SST_DEV_DESTINATION&&q.serial);
 assert(!sst_dev_take_request(&q));assert(!sst_dev_queue_peer(2));sst_dev_finish(true,"done");assert(sst_dev_status().completed==1);
 assert(!sst_dev_queue_peer(0)&&!sst_dev_queue_peer(7));assert(sst_dev_queue_peer(2));assert(sst_dev_take_request(&q));assert(q.peer==2&&q.serial==2&&q.connection_generation==23&&q.presence_generation==12&&q.peer_scene_generation==9&&q.queued_ms==1000&&!strcmp(q.peer_scene.stage,"F001r"));
 sst_dev_finish(false,"left");assert(sst_dev_status().rejected==1&&!sst_dev_status().busy);
 float view[12]={1,0,0,0,0,1,0,0,0,0,1,0},out[3];
 assert(sst_dev_flight_delta(view,0,32767,false,false,false,600,.016,out));near(out[0],0);near(out[1],0);near(out[2],-9.6f);
 assert(sst_dev_flight_delta(view,32767,0,true,false,true,600,.016,out));near(hypotf(out[0],out[1]),28.8f);
 assert(sst_dev_flight_delta(view,0,0,true,true,false,600,.016,out));near(out[1],0);
 assert(sst_dev_flight_delta(view,1000,1000,false,false,false,600,.016,out));near(out[0],0);near(out[2],0);
 float vertical[12]={1,0,0,0,0,0,-1,0,0,1,0,0};
 assert(sst_dev_flight_delta(vertical,0,32767,true,false,false,600,.016,out));assert(out[1]>0&&out[2]<0);
 out[0]=7;out[1]=8;out[2]=9;assert(!sst_dev_flight_delta(view,0,0,false,false,false,NAN,.01,out));near(out[0],7);
 assert(!sst_dev_flight_delta(view,0,0,false,false,false,600,-1,out));assert(!sst_dev_flight_delta(view,0,0,false,false,false,600,1,out));
 view[0]=INFINITY;assert(!sst_dev_flight_delta(view,0,0,false,false,false,600,.01,out));near(out[1],8);
 sst_dev_reset();assert(!sst_dev_controls().noclip&&!sst_dev_controls().moon_jump&&!sst_dev_status().busy&&!sst_dev_status().completed);
 puts("developer controls: ephemeral defaults, bounded queue, reset and camera-relative motion PASS");return 0;
}
