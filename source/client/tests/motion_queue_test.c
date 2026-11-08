// Deterministic receive-queue regression; no sockets or game thread.
// Include net.c and link the ordinary client LIB excluding src/sst_net.c.
#include "../src/sst_net.c"
#include <assert.h>

static const SstScene area = {.stage = "D100", .room = 2, .layer = 0};
static uint64_t key;
static unsigned delivered;

static void setup(void) {
    memset(players, 0, sizeof players);
    motion_n = 0; presence_serial = 7; status = SST_ONLINE;
    players[0] = (SstPlayer){.active = true, .id = 2, .presence_generation = 7, .scene = area};
    players[1] = (SstPlayer){.active = true, .id = 3, .presence_generation = 6, .scene = area};
    key = sst_world_enemy_key(0x16d, 15, 123);
    delivered = 0;
}

static void incoming(uint16_t from, uint32_t sequence, float x) {
    Buf b = {0};
    put16(&b, from); putstr(&b, area.stage); put8(&b, area.room); put8(&b, area.layer);
    put32(&b, (uint32_t)key); put32(&b, (uint32_t)(key >> 32));
    put32(&b, sequence); put16(&b, 0x16d);
    putf(&b, x); putf(&b, 10); putf(&b, -5);
    for (unsigned i = 0; i < 3; i++) put16(&b, 0);
    for (unsigned i = 0; i < 3; i++) putf(&b, 0);
    on_frame(T_MOTION, b.b, b.n);
}

static void consume(const SstEnemyMotion *m, void *user) {
    (void)user;
    delivered++;
    assert(m->key == key && sst_world_same_area(&m->scene, &area));
    if (m->from == 2) {
        assert(m->presence_generation == players[0].presence_generation);
        assert(m->sequence == 1 && m->pos[0] == 20);
    } else {
        assert(m->from == 3 && m->presence_generation == 6);
        assert(m->sequence == 9 && m->pos[0] == 30);
    }
}

int main(void) {
    setup();
    incoming(2, 50, 10);
    incoming(3, 9, 30);
    assert(motion_n == 2 && in_motion[0].presence_generation == 7);
    // JOIN keeps the ID but starts a new authenticated presence before polling.
    // Existing unrelated prop and committed-pickup retention must stay intact.
    prop_n = 1; in_props[0] = (SstProp){.from = 3, .sequence = 17};
    pickup_n = 1; in_pickups[0] = (SstPickupCommit){.from = 2, .epoch = 23};
    Buf join = {0}; put16(&join, 2); putstr(&join, "Returned"); putstr(&join, "team");
    on_frame(T_PLAYER_JOIN, join.b, join.n);
    assert(players[0].presence_generation == 8);
    incoming(2, 1, 20);
    assert(motion_n == 2 && in_motion[0].presence_generation == 8);
    assert(in_motion[0].sequence == 1 && in_motion[0].pos[0] == 20);
    assert(prop_n == 1 && in_props[0].from == 3 && in_props[0].sequence == 17);
    assert(pickup_n == 1 && in_pickups[0].from == 2 && in_pickups[0].epoch == 23);
    sst_net_poll_motion(consume, NULL);
    assert(delivered == 2 && motion_n == 0); // no old-generation packet returned

    setup();
    incoming(2, 50, 10);
    incoming(2, 49, 11); incoming(2, 50, 12);
    assert(motion_n == 1 && in_motion[0].sequence == 50 && in_motion[0].pos[0] == 10);
    incoming(2, 51, 13);
    assert(motion_n == 1 && in_motion[0].sequence == 51 && in_motion[0].pos[0] == 13);
    // Even a larger sequence cannot replace a newer pending incarnation.
    players[0].presence_generation = 6;
    incoming(2, 999, 14);
    assert(in_motion[0].presence_generation == 7 && in_motion[0].sequence == 51);
    players[0].presence_generation = 8;
    incoming(2, 1, 20);
    assert(motion_n == 1 && in_motion[0].presence_generation == 8 && in_motion[0].sequence == 1);
    sst_net_poll_motion(consume, NULL);
    assert(delivered == 1 && motion_n == 0);

    setup();
    incoming(2, UINT32_MAX, 10); incoming(2, 0, 11);
    assert(motion_n == 1 && in_motion[0].sequence == 0 && in_motion[0].pos[0] == 11);
    puts("motion queue PASS: reconnect generation replaces pending old stream, same-generation ordering and unrelated queues preserved");
    return 0;
}
