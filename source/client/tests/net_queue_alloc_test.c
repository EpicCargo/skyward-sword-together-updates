// Deterministic allocation-failure regression; no sockets or game thread.
// Include net.c and link the ordinary client LIB excluding src/sst_net.c.
#include <stdlib.h>
#include <stdbool.h>
static bool fail_allocation;
static void *test_realloc(void *pointer,size_t size);
#define realloc test_realloc
#include "../src/sst_net.c"
#undef realloc
#include <assert.h>
static void *test_realloc(void *pointer,size_t size) {
    return fail_allocation?NULL:realloc(pointer,size);
}
int main(void) {
    send_cap=8;send_len=8;sendbuf=malloc(send_cap);assert(sendbuf);
    memset(sendbuf,0x5a,send_len);uint8_t *before=sendbuf;
    Buf body={.n=4,.b={1,2,3,4}};
    fail_allocation=true;
    pthread_mutex_lock(&mu);
    assert(!queue_frame(T_PICKUP,&body));
    pthread_mutex_unlock(&mu);
    assert(sendbuf==before && send_cap==8 && send_len==8);
    for(unsigned i=0;i<8;i++)assert(sendbuf[i]==0x5a);
    assert(failure==SST_FAIL_SOCKET && strstr(last_error,"allocation failed"));
    fail_allocation=false;assert(queue_frame(T_PICKUP,&body));assert(send_len==17);
    assert(sendbuf[12]==T_PICKUP && !memcmp(sendbuf+13,body.b,4));
    for(unsigned i=0;i<8;i++)assert(sendbuf[i]==0x5a);
    assert(connect_failure(ECONNREFUSED)==SST_FAIL_CONNECT_REFUSED);
    assert(connect_failure(ENETUNREACH)==SST_FAIL_CONNECT_UNREACHABLE);
    assert(connect_failure(ETIMEDOUT)==SST_FAIL_CONNECT_TIMEOUT);
    assert(connect_failure(EIO)==SST_FAIL_CONNECT_OTHER);
#ifdef _WIN32
    _putenv("SST_TEST_TIMEOUT=800junk");
#else
    setenv("SST_TEST_TIMEOUT","800junk",1);
#endif
    assert(env_ms("SST_TEST_TIMEOUT",10000)==10000);
#ifdef _WIN32
    _putenv("SST_TEST_TIMEOUT=800");
#else
    setenv("SST_TEST_TIMEOUT","800",1);
#endif
    assert(env_ms("SST_TEST_TIMEOUT",10000)==800);
    // Malformed WELCOME cannot clear queued committed pickups or establish Online.
    pickup_n=1;status=SST_CONNECTING;uint8_t malformed[]={1,0,1};
    on_frame(T_WELCOME,malformed,sizeof malformed);
    assert(status==SST_FAILED && failure==SST_FAIL_REJECTED && pickup_n==1);
    // The live optional extensions remain known and are not reclassified as unknown.
    assert(T_PICKUP==0x33 && T_PROPS_ALL==0x34 && T_PROP==0x32 && SST_PROTO==45);
    free(sendbuf);sendbuf=NULL;send_cap=send_len=0;
    puts("network allocation PASS: queued bytes survive OOM, failure classification and extension/welcome guards preserved");
    return 0;
}
