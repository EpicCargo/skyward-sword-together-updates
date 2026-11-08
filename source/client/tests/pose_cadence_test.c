// Mocked-time regression for source cadence versus packet cadence. No game/network required.
#include "../src/sst_pose_buffer.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(condition) do { checks++; if (!(condition)) { failures++; \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); } } while (0)

#define MAX_PACKETS 4096
#define SPEED .02                 // world units per millisecond
#define PHASE_SPEED .03           // native animation frames per millisecond
#define LOOP_LENGTH 45.f
#define DURATION_MS 30000.
#define WARMUP_MS 7000.

typedef struct { double arrival; unsigned ordinal; SstPose pose; } Packet;
typedef struct {
    const char *name;
    double source_fps, observer_fps;
    unsigned delivery_stride, batch;
    bool jitter, transitions, latest_only;
    uint32_t first_tick;
} Scenario;
typedef struct {
    unsigned frames, bad, future, clamped, held, backwards, wrong_tick;
    unsigned transient_frames, transient_clamped, transient_phase_held;
    double min_lag, max_lag, min_latency, max_latency;
    double max_step_ratio, sum_squared_step_error, max_phase_error, period;
    double recovered_ms[2];
} Metrics;

static SstPose moving_pose(uint32_t tick,double source_ms) {
    SstPose p={.tick=tick,.pos={(float)(SPEED*source_ms),10,-5},
        .anim_id=26,.anim_frame=(float)fmod(source_ms*PHASE_SPEED,LOOP_LENGTH),
        .action=0,.health=24,.max_health=24};
    return p;
}
static double source_fps(const Scenario *s,double ms) {
    return s->transitions ? (ms<10000 || ms>=20000 ? 60 : 40) : s->source_fps;
}
static int by_arrival(const void *left,const void *right) {
    const Packet *a=left,*b=right;
    if(a->arrival!=b->arrival)return (a->arrival>b->arrival)-(a->arrival<b->arrival);
    return (a->ordinal>b->ordinal)-(a->ordinal<b->ordinal);
}
static unsigned packets_for(const Scenario *s,Packet *packets) {
    unsigned count=0,ordinal=0;
    double last_accepted=-1000;
    // This is the existing sender's reset-to-now 33 ms gate, driven by genuine
    // source frames. 40 FPS therefore creates 20 observations/s; no replacement
    // timestamp scheme or idealized 30 Hz publisher is smuggled into the fixture.
    for(double frame_ms=0;frame_ms<=DURATION_MS+1e-6;
        frame_ms+=1000./source_fps(s,frame_ms)) {
        if(frame_ms-last_accepted<33.-1e-6)continue;
        last_accepted=frame_ms;
        ordinal++;
        if(s->delivery_stride && (ordinal-1)%s->delivery_stride)continue;
        if(count>=MAX_PACKETS) { CHECK(count<MAX_PACKETS);break; }
        Packet *p=&packets[count++];
        *p=(Packet){.arrival=frame_ms+60,.ordinal=ordinal,
            .pose=moving_pose(s->first_tick+(uint32_t)(ordinal-1),frame_ms)};
        if(s->jitter) {
            p->arrival+=(double)((int)((ordinal*37u)%25u)-12);
            if(ordinal%29u==0)p->arrival+=120; // real reorder, not just spacing noise
        }
    }
    if(s->batch>1)for(unsigned first=0;first<count;first+=s->batch) {
        unsigned last=first+s->batch-1;
        if(last>=count)last=count-1;
        double arrival=packets[last].arrival;
        for(unsigned i=first;i<=last;i++)packets[i].arrival=arrival;
    }
    qsort(packets,count,sizeof *packets,by_arrival);
    return count;
}
static bool measured_window(const Scenario *s,double now) {
    if(now<WARMUP_MS)return false;
    // Each segment gets a separate seven-second settling allowance. Ordinary
    // steady tests still measure 23 seconds, so a long hold cannot pass unnoticed.
    if(s->transitions && ((now>=10000 && now<17000) ||
                          (now>=20000 && now<27000)))return false;
    return true;
}
static void recovery_sample(double elapsed,double lag,double period,double expected,
                            double *good_since,double *recovered) {
    // Recovery needs both a credible clock slope and real interpolation history
    // for half a second. A momentary crossing of either threshold cannot pass.
    if(lag>.25 && lag<9 && fabs(period-expected)<=expected*.1) {
        if(*good_since<0)*good_since=elapsed;
        if(*recovered<0 && elapsed-*good_since>=500.-1e-6)*recovered=elapsed;
    } else *good_since=-1;
}
static Metrics run_scenario(const Scenario *s) {
    Packet *packets=calloc(MAX_PACKETS,sizeof *packets);
    CHECK(packets!=NULL);
    Metrics m={.min_lag=DBL_MAX,.max_lag=-DBL_MAX,
               .min_latency=DBL_MAX,.max_latency=-DBL_MAX,.recovered_ms={-1,-1}};
    if(!packets)return m;
    unsigned count=packets_for(s,packets),next=0,visible_ordinal=0;
    SstPoseBuffer b;sst_pose_buffer_reset(&b);
    sst_pose_buffer_animation(&b,26,LOOP_LENGTH,true);
    double dt=1000./s->observer_fps,last_x=0,last_phase=0,good_since[2]={-1,-1};
    bool previous=false,previous_output=false;
    for(unsigned render=0;;render++) {
        double now=render*dt;
        if(now>DURATION_MS)break;
        // latest_only models the network snapshot seen by the actual renderer.
        // The direct path also exercises generic late insertion and equal-time
        // batches, instead of relying exclusively on ingress discarding them.
        unsigned freshest=MAX_PACKETS;
        while(next<count && packets[next].arrival<=now+1e-6) {
            if(s->latest_only) {
                if(packets[next].ordinal>visible_ordinal &&
                   (freshest==MAX_PACKETS || packets[next].ordinal>packets[freshest].ordinal))
                    freshest=next;
            } else {
                sst_pose_buffer_push(&b,&packets[next].pose,packets[next].arrival);
                sst_pose_buffer_push(&b,&packets[next].pose,packets[next].arrival+1);
            }
            next++;
        }
        if(freshest!=MAX_PACKETS) {
            sst_pose_buffer_push(&b,&packets[freshest].pose,packets[freshest].arrival);
            visible_ordinal=packets[freshest].ordinal;
        }
        SstPose out;
        if(!sst_pose_buffer_sample(&b,now,&out)) {
            previous=false;previous_output=false;continue;
        }
        double lag=(double)b.timeline[b.head]-b.clock_tick;
        if(s->transitions && now>=10000) {
            unsigned transition=now<20000?0:1;
            double elapsed=now-(transition?20000:10000);
            double expected=transition?1000./30:50;
            recovery_sample(elapsed,lag,b.source_period_ms,expected,
                            &good_since[transition],&m.recovered_ms[transition]);
            if(elapsed<=5000) {
                m.transient_frames++;
                if(lag<=-2.+1e-5)m.transient_clamped++;
                if(previous_output && fabs(remainder(out.anim_frame-last_phase,LOOP_LENGTH))<1e-6)
                    m.transient_phase_held++;
            }
        }
        bool measure=measured_window(s,now);
        if(measure) {
            m.frames++;
            if(!isfinite(out.pos[0]) || !isfinite(out.anim_frame) ||
               !isfinite(b.clock_tick) || !isfinite(b.source_period_ms))m.bad++;
            double latency=now-(double)out.pos[0]/SPEED;
            if(lag<m.min_lag)m.min_lag=lag;
            if(lag>m.max_lag)m.max_lag=lag;
            if(latency<m.min_latency)m.min_latency=latency;
            if(latency>m.max_latency)m.max_latency=latency;
            if(lag<=0)m.future++;
            if(lag<=-2.+1e-5)m.clamped++;
            // Position is an independent wall-time oracle for animation phase.
            // This detects a buffer that smooths x but repeatedly holds animation.
            double phase=fmod((double)out.pos[0]/SPEED*PHASE_SPEED,LOOP_LENGTH);
            double phase_error=fabs(remainder((double)out.anim_frame-phase,LOOP_LENGTH));
            // Beyond newest, animation intentionally holds while only position
            // extrapolates. Demand phase/position agreement inside real history.
            if(lag>=0 && phase_error>m.max_phase_error)m.max_phase_error=phase_error;
            if(!s->transitions) {
                double source_period=1000./s->source_fps*ceil(33./(1000./s->source_fps)-1e-9);
                uint32_t expected=s->first_tick+(uint32_t)floor((double)out.pos[0]/SPEED/source_period);
                int32_t tick_error=(int32_t)(out.tick-expected);
                if(tick_error < -1 || tick_error > 1)m.wrong_tick++;
            }
            if(previous) {
                double step=(double)out.pos[0]-last_x,ratio=step/(SPEED*dt);
                if(step < -1e-4)m.backwards++;
                if(fabs(step)<1e-6)m.held++;
                if(fabs(ratio)>m.max_step_ratio)m.max_step_ratio=fabs(ratio);
                m.sum_squared_step_error+=(ratio-1)*(ratio-1);
            }
        }
        previous=measure;previous_output=true;last_x=out.pos[0];last_phase=out.anim_frame;
    }
    m.period=b.source_period_ms;
    free(packets);
    return m;
}
static void report(const Scenario *s,const Metrics *m) {
    fprintf(stderr,"%s src %.0f FPS observer %.0f: period %.3f ms, lag %.2f..%.2f, "
        "latency %.1f..%.1f ms, future %.3f hold %.3f, step RMS %.3f max %.3f, phase %.4f\n",
        s->name,s->source_fps,s->observer_fps,m->period,m->min_lag,m->max_lag,
        m->min_latency,m->max_latency,m->frames?(double)m->future/m->frames:1,
        m->frames?(double)m->held/m->frames:1,
        m->frames?sqrt(m->sum_squared_step_error/m->frames):INFINITY,
        m->max_step_ratio,m->max_phase_error);
}
static void check_steady(Scenario s,double expected_period,bool noisy) {
    Metrics m=run_scenario(&s);report(&s,&m);
    CHECK(m.frames>100);
    CHECK(m.bad==0);
    CHECK(fabs(m.period-expected_period)<expected_period*(noisy?.08:.01));
    // Bursts/loss can consume interpolation history temporarily. They must not
    // recreate steady-source walkoff or become perpetual extrapolation/holding.
    CHECK(m.future < (noisy?m.frames/2:m.frames/100)+1);
    CHECK(m.clamped < (noisy?m.frames/3:0)+1);
    CHECK(m.backwards==0);
    CHECK(m.wrong_tick==0);
    CHECK(m.held < (noisy?m.frames/2:m.frames/100)+1);
    CHECK(m.min_lag>(noisy?-2.001:.25));
    CHECK(m.max_lag<9);
    CHECK(m.min_latency>0);
    CHECK(m.max_latency<900);
    CHECK(m.max_phase_error<.03);
    CHECK(m.max_step_ratio<(noisy?3.25:1.25));
    CHECK(sqrt(m.sum_squared_step_error/m.frames)<(noisy?.6:.12));
}
static void steady_and_transport_cases(void) {
    const double observers[]={40,60,120};
    for(unsigned i=0;i<sizeof observers/sizeof *observers;i++) {
        check_steady((Scenario){.name="40 FPS sender",.source_fps=40,
            .observer_fps=observers[i],.first_tick=100},50,false);
        check_steady((Scenario){.name="60 FPS control",.source_fps=60,
            .observer_fps=observers[i],.first_tick=100},1000./30,false);
    }
    // Ten received packets/s must retain the actual 30 observation-tick/s clock.
    check_steady((Scenario){.name="30 Hz source, 10 Hz delivery",.source_fps=60,
        .observer_fps=60,.delivery_stride=3,.first_tick=100},1000./30,false);
    check_steady((Scenario){.name="20 Hz source, 10 Hz delivery",.source_fps=40,
        .observer_fps=60,.delivery_stride=2,.first_tick=100},50,false);
    check_steady((Scenario){.name="20 Hz jitter/drop/reorder",.source_fps=40,
        .observer_fps=60,.delivery_stride=2,.jitter=true,.first_tick=100},50,true);
    check_steady((Scenario){.name="30 Hz jitter/reorder",.source_fps=60,
        .observer_fps=120,.jitter=true,.first_tick=100},1000./30,true);
    check_steady((Scenario){.name="six-pose equal-time TCP batches",.source_fps=60,
        .observer_fps=60,.batch=6,.first_tick=100},1000./30,true);
    check_steady((Scenario){.name="TCP batches, latest network snapshot",.source_fps=60,
        .observer_fps=40,.batch=6,.latest_only=true,.first_tick=100},1000./30,true);
    check_steady((Scenario){.name="20 Hz across uint32 wrap",.source_fps=40,
        .observer_fps=120,.first_tick=UINT32_MAX-200u},50,false);
    check_steady((Scenario){.name="30 Hz wrap with loss/reorder",.source_fps=60,
        .observer_fps=60,.delivery_stride=3,.jitter=true,
        .first_tick=UINT32_MAX-300u},1000./30,true);
}
static void cadence_transitions(void) {
    Scenario s={.name="60 -> 40 -> 60 FPS sender",.source_fps=60,
        .observer_fps=60,.transitions=true,.first_tick=100};
    Metrics m=run_scenario(&s);report(&s,&m);
    CHECK(m.frames>100);
    CHECK(m.bad==0);
    CHECK(fabs(m.period-1000./30)<1);
    CHECK(m.future < m.frames/100+1);
    CHECK(m.clamped==0);
    CHECK(m.backwards==0);
    CHECK(m.held < m.frames/100+1);
    CHECK(m.min_lag>.25 && m.max_lag<9);
    CHECK(m.max_phase_error<.03);
    CHECK(m.max_latency<900);
    CHECK(m.max_step_ratio<1.5);
    fprintf(stderr,"transition recovery: 30 -> 20 Hz %.0f ms, 20 -> 30 Hz %.0f ms; "
        "first 5 s clamp %.3f, animation hold %.3f\n",m.recovered_ms[0],m.recovered_ms[1],
        m.transient_frames?(double)m.transient_clamped/m.transient_frames:1,
        m.transient_frames?(double)m.transient_phase_held/m.transient_frames:1);
    CHECK(m.transient_frames>100);
    CHECK(m.recovered_ms[0]>=0 && m.recovered_ms[0]<=5000.+1e-6);
    CHECK(m.recovered_ms[1]>=0 && m.recovered_ms[1]<=5000.+1e-6);
}
static void late_ring_and_restart(void) {
    SstPoseBuffer b;sst_pose_buffer_reset(&b);
    SstPose a=moving_pose(UINT32_MAX-1u,0),c=moving_pose(1,150),late=moving_pose(0,100);
    sst_pose_buffer_push(&b,&a,0);
    sst_pose_buffer_push(&b,&c,150);
    sst_pose_buffer_push(&b,&late,180);
    CHECK(b.count==3);
    CHECK(b.ring[b.head].tick==1);
    for(int age=1;age<b.count;age++) {
        int newer=(b.head-age+1+SST_POSE_RING)%SST_POSE_RING;
        int older=(b.head-age+SST_POSE_RING)%SST_POSE_RING;
        CHECK(b.timeline[newer]>b.timeline[older]);
        CHECK(b.timeline[newer]-b.timeline[older]==
              (int32_t)(b.ring[newer].tick-b.ring[older].tick));
    }
    sst_pose_buffer_push(&b,&late,181);CHECK(b.count==3);
    // A presence reset is explicit: even a restarted counter within the old
    // +/-1000 reorder range must begin a new buffer rather than inherit its fit.
    sst_pose_buffer_reset(&b);
    SstPose out;
    for(unsigned k=0;k<80;k++) {
        SstPose p=moving_pose(5000+k,k*50.);
        sst_pose_buffer_push(&b,&p,k*50.);
        CHECK(sst_pose_buffer_sample(&b,k*50.,&out));
    }
    CHECK(fabs(b.source_period_ms-50)<1);
    SstPose restarted=moving_pose(1,8000);
    sst_pose_buffer_push(&b,&restarted,8000); // large ordinal reset auto-clears
    CHECK(b.count==1);
    CHECK(fabs(b.source_period_ms-1000./30)<.001);
    CHECK(sst_pose_buffer_sample(&b,8000,&out));
    CHECK(fabsf(out.pos[0]-restarted.pos[0])<.001f);
    sst_pose_buffer_reset(&b);
    restarted.tick=400;sst_pose_buffer_push(&b,&restarted,9000);
    sst_pose_buffer_reset(&b); // network presence-generation boundary
    restarted.tick=1;sst_pose_buffer_push(&b,&restarted,9001);
    CHECK(b.count==1 && b.ring[b.head].tick==1);
}
static void extrapolation_and_discrete_animation(void) {
    SstPoseBuffer b;sst_pose_buffer_reset(&b);
    sst_pose_buffer_animation(&b,26,LOOP_LENGTH,true);
    SstPose out,last={0};
    // Every third source observation is delivered. Extrapolation still covers
    // two SOURCE ticks, rather than two large received-packet intervals.
    for(unsigned k=0;k<100;k++) {
        SstPose p=moving_pose(100+k*3,k*100.);
        sst_pose_buffer_push(&b,&p,k*100.);
        CHECK(sst_pose_buffer_sample(&b,k*100.,&out));last=p;
    }
    float cap=last.pos[0]+(float)(SPEED*(1000./30)*2);
    float max_x=last.pos[0];
    for(unsigned i=1;i<=90;i++) {
        CHECK(sst_pose_buffer_sample(&b,9900+i*(1000./60),&out));
        CHECK(out.pos[0]<=cap+.01f);
        if(out.pos[0]>max_x)max_x=out.pos[0];
        if(out.pos[0]>last.pos[0]+.01f)CHECK(out.anim_frame==last.anim_frame);
    }
    CHECK(fabsf(max_x-cap)<.02f); // proves useful bounded motion, not freeze-only
    float held_x=out.pos[0];
    CHECK(sst_pose_buffer_sample(&b,11500,&out));
    CHECK(fabsf(out.pos[0]-held_x)<.001f);
    // Finite action/phase cannot advance beyond a received attack sample.
    sst_pose_buffer_reset(&b);sst_pose_buffer_animation(&b,197,12,false);
    SstPose a=moving_pose(100,0),c=moving_pose(101,50);
    a.anim_id=c.anim_id=197;a.action=c.action=109;a.anim_frame=4;c.anim_frame=5;
    sst_pose_buffer_push(&b,&a,0);sst_pose_buffer_push(&b,&c,50);
    CHECK(sst_pose_buffer_sample(&b,50,&out));
    CHECK(sst_pose_buffer_sample(&b,250,&out));
    CHECK(out.anim_id==197 && out.action==109 && out.anim_frame<=5);
}
static void invalid_times_and_clock_reversal(void) {
    SstPoseBuffer b;sst_pose_buffer_reset(&b);
    SstPose p=moving_pose(0,0),out;
    const double invalid[]={NAN,INFINITY,-INFINITY,-1};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++) {
        sst_pose_buffer_push(&b,&p,invalid[i]);CHECK(b.count==0);
        CHECK(!sst_pose_buffer_sample(&b,invalid[i],&out));
    }
    sst_pose_buffer_push(&b,&p,0);CHECK(b.count==1);
    CHECK(sst_pose_buffer_sample(&b,0,&out)); // timestamp zero is a valid epoch
    CHECK(out.tick==0 && out.pos[0]==0);
    for(unsigned k=1;k<=90;k++) {
        p=moving_pose(k,k*(1000./30));
        sst_pose_buffer_push(&b,&p,k*(1000./30));
        CHECK(sst_pose_buffer_sample(&b,k*(1000./30),&out));
    }
    SstPoseBuffer before=b;
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++) {
        p.tick++;sst_pose_buffer_push(&b,&p,invalid[i]);
        CHECK(memcmp(&b,&before,sizeof b)==0);
        CHECK(!sst_pose_buffer_sample(&b,invalid[i],&out));
        CHECK(memcmp(&b,&before,sizeof b)==0);
    }
    SstPose reference=out;
    CHECK(sst_pose_buffer_sample(&b,3000,&reference));
    (void)sst_pose_buffer_sample(&b,2990,&out); // may reject or harmlessly hold
    CHECK(sst_pose_buffer_sample(&b,3000,&out));
    CHECK(fabsf(out.pos[0]-reference.pos[0])<.0001f);
    CHECK(fabs(b.clock_tick-before.clock_tick)<.0001);
}
static void teleport_is_not_extrapolated_velocity(void) {
    SstPoseBuffer b;sst_pose_buffer_reset(&b);
    SstPose old=moving_pose(100,0),teleport=moving_pose(101,50),out;
    old.pos[0]=0;teleport.pos[0]=5000;
    sst_pose_buffer_push(&b,&old,0);sst_pose_buffer_push(&b,&teleport,50);
    // Deliberately sample just ahead of the newest observation: the bounded
    // extrapolation branch must honor the same teleport rule as interpolation.
    b.started=true;b.clock_tick=102;b.last_update_ms=50;
    CHECK(sst_pose_buffer_sample(&b,50,&out));
    CHECK(out.pos[0]==5000 && out.tick==101);
    CHECK(sst_pose_buffer_sample(&b,100,&out));
    CHECK(out.pos[0]==5000);
}
static void long_pause_does_not_blend_old_history(void) {
    SstPoseBuffer b;sst_pose_buffer_reset(&b);SstPose out;
    for(unsigned k=0;k<100;k++) {
        SstPose p=moving_pose(k+1,k*50.);
        sst_pose_buffer_push(&b,&p,k*50.);
        CHECK(sst_pose_buffer_sample(&b,k*50.,&out));
    }
    // A stopped game's ordinal counter advances by one, not by elapsed seconds.
    // The old poses are all beyond the remote wrapper's one-second freshness
    // horizon when this new genuine observation arrives.
    SstPose resumed=moving_pose(101,10000);
    sst_pose_buffer_push(&b,&resumed,10000);
    CHECK(sst_pose_buffer_sample(&b,10000,&out));
    CHECK(fabsf(out.pos[0]-resumed.pos[0])<.001f);
    CHECK(fabs(b.source_period_ms-1000./30)<1);
}
static void short_stall_recovers_its_original_cadence(void) {
    const double periods[]={1000./30,50},stalls[]={200,500,800};
    for(unsigned rate=0;rate<sizeof periods/sizeof *periods;rate++)
        for(unsigned pause=0;pause<sizeof stalls/sizeof *stalls;pause++) {
            SstPoseBuffer b;sst_pose_buffer_reset(&b);
            sst_pose_buffer_animation(&b,26,LOOP_LENGTH,true);
            SstPose out;double period=periods[rate],last=159*period;
            for(unsigned k=0;k<160;k++) {
                SstPose p=moving_pose(k,k*period);
                sst_pose_buffer_push(&b,&p,k*period);
                CHECK(sst_pose_buffer_sample(&b,k*period,&out));
            }
            double resumed=last+period+stalls[pause],next=resumed;
            double good_since=-1,recovered=-1,last_phase=out.anim_frame;
            unsigned ordinal=160,frames=0,clamped=0,phase_held=0;
            // Render through the gap as well as after it. The next genuine source
            // observation is only ordinal+1: this is a single source-frame stall,
            // not packet loss with a large source-tick delta or a >1s reset.
            for(unsigned frame=1;;frame++) {
                double now=last+frame*(1000./60);
                if(now>resumed+6000)break;
                while(next<=now+1e-6) {
                    SstPose p=moving_pose(ordinal++,next);
                    sst_pose_buffer_push(&b,&p,next);next+=period;
                }
                CHECK(sst_pose_buffer_sample(&b,now,&out));
                if(now>=resumed) {
                    double elapsed=now-resumed,lag=(double)b.timeline[b.head]-b.clock_tick;
                    recovery_sample(elapsed,lag,b.source_period_ms,period,&good_since,&recovered);
                    if(elapsed<=5000) {
                        frames++;
                        if(lag<=-2.+1e-5)clamped++;
                        if(fabs(remainder(out.anim_frame-last_phase,LOOP_LENGTH))<1e-6)phase_held++;
                    }
                }
                last_phase=out.anim_frame;
            }
            fprintf(stderr,"single %.0f ms source stall at %.1f Hz: recovery %.0f ms, "
                "final period %.3f ms, first 5 s clamp %.3f animation hold %.3f\n",
                stalls[pause],1000./period,recovered,b.source_period_ms,
                frames?(double)clamped/frames:1,frames?(double)phase_held/frames:1);
            CHECK(recovered>=0 && recovered<=5000.+1e-6);
            CHECK(fabs(b.source_period_ms-period)<period*.1);
            CHECK((double)b.timeline[b.head]-b.clock_tick>.25);
        }
}
int main(void) {
    steady_and_transport_cases();
    cadence_transitions();
    late_ring_and_restart();
    extrapolation_and_discrete_animation();
    invalid_times_and_clock_reversal();
    teleport_is_not_extrapolated_velocity();
    long_pause_does_not_blend_old_history();
    short_stall_recovers_its_original_cadence();
    fprintf(stderr,"pose cadence: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
