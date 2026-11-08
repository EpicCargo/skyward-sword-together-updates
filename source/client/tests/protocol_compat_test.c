// Session-008 QA: wire-protocol compatibility. Version gate, fixed POSE size, unknown message
// types and the bytes the coordinator will use for equipment state (equip_flags bits 0-4 and the
// POSE pad byte). Loopback only, raw peers from sst_test.h plus the real client library.
//
// CHECK_CUR marks an expectation that documents today's behaviour and may legitimately change
// with a protocol bump; plain CHECK marks an invariant.
#include "sst_test.h"
#include "../src/sst_host.h"
#include "../src/sst_world.h"

#define CHECK_CUR(cond) do { sst_test_checks++; if (!(cond)) { sst_test_failures++; \
    fprintf(stderr, "  FAIL[behaviour-may-change] %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NOTE(...) do { fprintf(stderr, "  note: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
enum { M_ERROR = 5, M_PING = 6, M_PONG = 7, M_SCENE = 0x10, M_POSE = 0x11, M_ENEMY = 0x30 };

static void p32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t g16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static void mkpose(uint8_t b[36], uint32_t tick, unsigned health, uint8_t equip, uint8_t pad) {
    memset(b, 0, 36); p32(b, tick);
    float v[3] = {1.0f, 2.0f, 3.0f}; memcpy(b + 4, v, 12);
    b[30] = health; b[31] = health >> 8; b[32] = 10; b[34] = equip; b[35] = pad;
}
static bool sync_peer(RawPeer *p) {
    uint8_t t[4] = {9, 8, 7, 6};
    return raw_send(p, M_PING, t, 4) && raw_expect(p, M_PONG, NULL, 0, NULL, 2000) == M_PONG;
}
static uint16_t join(RawPeer *p, uint16_t port, const char *name, uint8_t session) {
    uint8_t body[64]; size_t n = 0;
    if (!raw_connect(p, port) || !raw_hello(p, SST_PROTO, name, "room", "", session)) return 0;
    if (raw_expect(p, 0x02, body, sizeof body, &n, 2000) != 0x02 || n < 2) return 0;
    return g16(body);
}
static bool scene(RawPeer *p, const char *stage, int room, int layer) {
    uint8_t b[80]; size_t n = put_str(b, stage); b[n++] = (uint8_t)room; b[n++] = (uint8_t)layer; b[n++] = 0;
    return raw_send(p, M_SCENE, b, n);
}
static bool err_is(RawPeer *p, const char *text) {
    uint8_t b[160] = {0}; size_t n = 0;
    if (raw_expect(p, M_ERROR, b, sizeof b - 1, &n, 2000) != M_ERROR || n < 1) return false;
    bool ok = strlen(text) == b[0] && n >= 1u + b[0] && !memcmp(b + 1, text, b[0]);
    if (!ok) fprintf(stderr, "  error text: %.*s\n", (int)b[0], b + 1);
    return ok;
}
static bool online(void) { return sst_net_status(NULL, 0) == SST_ONLINE; }
static SstConfig cfg(uint16_t port) {
    SstConfig c = {.port = port, .auto_reconnect = false, .use_udp = false};
    snprintf(c.host, sizeof c.host, "127.0.0.1"); snprintf(c.name, sizeof c.name, "client");
    snprintf(c.room, sizeof c.room, "room");
    return c;
}
// Any frame at all within `ms`? (returns its type or -1)
static int any_frame(RawPeer *p, int ms) { return raw_read(p, NULL, 0, NULL, ms); }

// ------------------------------------------------------------------ version gate
static void hello_version_matrix(void) {
    char why[160]; CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    NOTE("SST_PROTO = %d", SST_PROTO);
    // A resident teammate: must see NO join traffic from rejected clients.
    static RawPeer resident; CHECK(join(&resident, port, "resident", 1) != 0);
    CHECK(sync_peer(&resident));
    const int versions[] = {SST_PROTO - 1, SST_PROTO + 1, 0, 1, 0xffff, SST_PROTO ^ 0x100};
    for (unsigned i = 0; i < sizeof versions / sizeof *versions; i++) {
        NOTE("HELLO protocol %d", versions[i]);
        static RawPeer p; CHECK(raw_connect(&p, port));
        CHECK(raw_hello(&p, (uint16_t)versions[i], "old", "room", "", (uint8_t)(20 + i)));
        CHECK(err_is(&p, "Update the mod: protocol mismatch"));
        CHECK(any_frame(&p, 800) == -1);          // nothing else (no WELCOME / JOIN / OFFER); socket closes
        raw_close(&p);
    }
    CHECK(any_frame(&resident, 300) == -1);        // resident saw no JOIN/LEAVE for the rejected hellos
    // A HELLO with fewer than 2 bytes cannot even carry a version: same message.
    static RawPeer p; CHECK(raw_connect(&p, port));
    uint8_t one = 45; CHECK(raw_send(&p, 0x01, &one, 1));
    CHECK(err_is(&p, "Update the mod: protocol mismatch"));
    raw_close(&p);
    // The current version is welcomed (with the gate still intact afterwards).
    static RawPeer ok; CHECK(join(&ok, port, "new", 30) != 0);
    SstPlayer list[4];
    (void)list;
    // Right version, malformed remainder: a different error.
    static RawPeer bad; CHECK(raw_connect(&bad, port));
    uint8_t h[2] = {SST_PROTO & 0xff, SST_PROTO >> 8}; CHECK(raw_send(&bad, 0x01, h, 2));
    CHECK(err_is(&bad, "Invalid join request"));
    raw_close(&bad); raw_close(&ok); raw_close(&resident);
    sst_host_stop();
}

static void client_stops_on_mismatch_error(void) {
    // Client side: SST_PROTO is what it advertises, and a mismatch ERROR ends the attempt.
    uint16_t port; SstSocket l = listen_loopback(&port);
    SstConfig c = cfg(port); c.auto_reconnect = true; sst_net_start(&c);
    SstSocket fd = accept_within(l, 3000); CHECK(fd != SST_INVALID_SOCKET);
    static RawPeer host; memset(&host, 0, sizeof host); host.fd = fd; uint8_t body[300]; size_t n = 0;
    CHECK(raw_read(&host, body, sizeof body, &n, 2000) == 0x01);
    CHECK(n >= 2 && g16(body) == SST_PROTO);
    uint8_t e[64]; size_t en = put_str(e, "Update the mod: protocol mismatch");
    CHECK(raw_send(&host, M_ERROR, e, en));
    char text[160] = "";
    CHECK(WAIT_UNTIL(sst_net_status(text, sizeof text) == SST_FAILED, 3000));
    CHECK(strstr(text, "Update the mod") != NULL);
    CHECK(accept_within(l, 2500) == SST_INVALID_SOCKET);
    sst_net_stop(); raw_close(&host); sst_socket_close(l);
}

// ------------------------------------------------------------------ POSE framing
static void pose_is_36_bytes(void) {
    char why[160]; CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    uint8_t pose[40]; mkpose(pose, 1, 3, 0x20, 0); pose[36] = pose[37] = pose[38] = 0;
    size_t sizes[] = {0, 1, 35, 37, 38, 40};
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        NOTE("POSE of %zu bytes", sizes[i]);
        static RawPeer a, b; CHECK(join(&a, port, "a", 1) != 0); CHECK(join(&b, port, "b", 2) != 0);
        scene(&a, "F001", 1, 0); scene(&b, "F001", 1, 0); CHECK(sync_peer(&a)); CHECK(sync_peer(&b));
        CHECK(raw_send(&a, M_POSE, pose, sizes[i]));
        CHECK(err_is(&a, "Invalid pose"));
        CHECK(raw_expect(&b, M_POSE, NULL, 0, NULL, 300) == -1);
        raw_close(&a); raw_close(&b);
        CHECK(WAIT_UNTIL(true, 1)); sleep_ms(150);
    }
    // 36 bytes: accepted, relayed as [id][36 bytes] (38).
    static RawPeer a, b; uint16_t ia = join(&a, port, "a", 1); CHECK(join(&b, port, "b", 2) != 0);
    scene(&a, "F001", 1, 0); scene(&b, "F001", 1, 0); CHECK(sync_peer(&a)); CHECK(sync_peer(&b));
    CHECK(raw_send(&a, M_POSE, pose, 36));
    uint8_t got[80]; size_t gn = 0;
    CHECK(raw_expect(&b, M_POSE, got, sizeof got, &gn, 2000) == M_POSE);
    CHECK(gn == 38 && g16(got) == ia && !memcmp(got + 2, pose, 36));
    // The host does not interpret float contents of a TCP pose (the UDP path does); a NaN pose is
    // relayed by the host and then discarded by the receiving client. Documented, not asserted.
    raw_close(&a); raw_close(&b);
    sst_host_stop();
}

static void client_pose_is_36_bytes(void) {
    char why[160]; CHECK(sst_host_start(0, "room", why, sizeof why));
    SstConfig c = cfg(sst_host_port()); sst_net_start(&c);
    CHECK(WAIT_UNTIL(online(), 3000));
    SstScene sc = {.stage = "F001", .room = 1}; sst_net_send_scene(&sc);
    static RawPeer b; CHECK(join(&b, sst_host_port(), "b", 2) != 0);
    CHECK(scene(&b, "F001", 1, 0) && sync_peer(&b));
    SstPose p = {.tick = 5, .pos = {1, 2, 3}, .rot = {1, 2, 3}, .anim_id = 77, .anim_frame = 1.5f, .action = 3,
                 .health = 5, .max_health = 12, .equip_flags = 0x3f};
    bool got = false; uint8_t body[80]; size_t n = 0;
    for (int i = 0; i < 30 && !got; i++) { p.tick = 5 + i; sst_net_send_pose(&p); got = raw_expect(&b, M_POSE, body, sizeof body, &n, 100) == M_POSE; }
    CHECK(got && n == 38);
    if (got && n == 38) {
        CHECK(g16(body) == sst_net_my_id());
        CHECK(body[2 + 34] == 0x3f);        // equip_flags travels verbatim
        CHECK_CUR(body[2 + 35] == 0);       // the client always writes pad = 0 today (no field in SstPose)
        CHECK(g16(body + 2 + 22) == 77 && g16(body + 2 + 30) == 5 && g16(body + 2 + 32) == 12);
    }
    sst_net_stop(); raw_close(&b); sst_host_stop();
}

// ------------------------------------------------------------------ unknown message types
static void unknown_types_host(void) {
    char why[160]; CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    static RawPeer a, b; CHECK(join(&a, port, "a", 1) != 0); CHECK(join(&b, port, "b", 2) != 0);
    scene(&a, "F001", 1, 0); scene(&b, "F001", 1, 0); CHECK(sync_peer(&a)); CHECK(sync_peer(&b));
    // Logged-in peer: unknown / server-only types are ignored (no error, no relay, link stays up).
    const uint8_t types[] = {0x00, 0x18, 0x12, 0x25, 0x35, 0x40, 0x7e, 0xff,
                             0x02, 0x03, 0x04, 0x05, 0x07, 0x24}; // 0x02..0x07 / 0x24 are host-to-client only
    for (unsigned i = 0; i < sizeof types; i++) {
        uint8_t body[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        CHECK(raw_send(&a, types[i], body, i % 3 ? 12 : 0));
    }
    CHECK(sync_peer(&a));                                   // still connected, no HOST_ERROR
    CHECK(raw_expect(&a, M_ERROR, NULL, 0, NULL, 100) == -1);
    int t = any_frame(&b, 300);
    CHECK(t == -1);                                         // nothing relayed to the teammate
    if (t != -1) NOTE("teammate saw unexpected type 0x%02x", t);
    // Not yet logged in: any non-HELLO frame is refused.
    static RawPeer p; CHECK(raw_connect(&p, port));
    uint8_t x = 0; CHECK(raw_send(&p, 0x7e, &x, 1));
    CHECK(err_is(&p, "Expected join request"));
    raw_close(&p); raw_close(&a); raw_close(&b);
    sst_host_stop();
}

static void unknown_types_client(void) {
    uint16_t port; SstSocket l = listen_loopback(&port);
    SstConfig c = cfg(port); sst_net_start(&c);
    SstSocket fd = accept_within(l, 3000); CHECK(fd != SST_INVALID_SOCKET);
    static RawPeer h; memset(&h, 0, sizeof h); h.fd = fd; uint8_t body[300]; size_t n = 0;
    CHECK(raw_read(&h, body, sizeof body, &n, 2000) == 0x01);
    uint8_t w[3] = {9, 0, 0}; CHECK(raw_send(&h, 0x02, w, 3));
    CHECK(WAIT_UNTIL(online(), 3000));
    const uint8_t types[] = {0x00, 0x09 + 0x10, 0x12, 0x25, 0x35, 0x7e, 0xff};
    for (unsigned i = 0; i < sizeof types; i++) CHECK(raw_send(&h, types[i], body, i));
    // A valid join afterwards is still processed: the stream stayed in sync.
    uint8_t j[16]; size_t jn = 0; j[jn++] = 4; j[jn++] = 0; jn += put_str(j + jn, "late"); jn += put_str(j + jn, "");
    CHECK(raw_send(&h, 0x03, j, jn));
    SstPlayer list[4];
    CHECK(WAIT_UNTIL(sst_net_players(list, 4) == 1, 2000));
    CHECK(online());
    sst_net_stop(); raw_close(&h); sst_socket_close(l);
}

// ------------------------------------------------------------------ equipment state bytes
// equip_flags bits 0-4 and the pad byte are opaque to the host: they must come out exactly as
// they went in (host -> client), also for a player who joins after the pose was sent.
static void equip_flags_and_pad_round_trip(void) {
    char why[160]; CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    SstConfig c = cfg(port); sst_net_start(&c);
    CHECK(WAIT_UNTIL(online(), 3000));
    SstScene sc = {.stage = "F001", .room = 1}; sst_net_send_scene(&sc);
    static RawPeer a, b; uint16_t ia = join(&a, port, "a", 1); CHECK(ia != 0); CHECK(join(&b, port, "b", 2) != 0);
    scene(&a, "F001", 1, 0); scene(&b, "F001", 1, 0); CHECK(sync_peer(&a)); CHECK(sync_peer(&b));
    CHECK(WAIT_UNTIL(({ SstPlayer l[4]; sst_net_players(l, 4) == 2; }), 2000));
    const uint8_t pads[] = {0x00, 0x5a, 0xff, 0x01};
    unsigned bad_raw = 0, bad_client = 0, tested = 0; uint32_t tick = 100;
    for (unsigned flags = 0; flags < 32; flags++) {
        for (unsigned hi = 0; hi < 8; hi += 7) {               // high bits clear / all set (0xe0)
            uint8_t eq = (uint8_t)(flags | hi << 5), pad = pads[(flags + hi) % 4], pose[36];
            mkpose(pose, ++tick, 3, eq, pad);
            CHECK(raw_send(&a, M_POSE, pose, 36));
            uint8_t got[80]; size_t gn = 0;
            bool ok = raw_expect(&b, M_POSE, got, sizeof got, &gn, 2000) == M_POSE;
            if (!ok || gn != 38 || g16(got) != ia || memcmp(got + 2, pose, 36)) bad_raw++;
            // The real client keeps equip_flags (all 8 bits); the pad byte is not part of SstPose.
            bool seen = WAIT_UNTIL(({ SstPlayer l[4]; int n = sst_net_players(l, 4); bool m = false;
                for (int i = 0; i < n; i++) if (l[i].id == ia && l[i].has_pose && l[i].pose.tick == tick) m = l[i].pose.equip_flags == eq; m; }), 1000);
            if (!seen) bad_client++;
            tested++;
        }
    }
    NOTE("%u patterns: host relay mismatches %u, client equip_flags mismatches %u", tested, bad_raw, bad_client);
    CHECK(bad_raw == 0);
    CHECK(bad_client == 0);
    // Late joiner: the host replays the last pose within 1 s of receipt, byte for byte (incl. pad).
    uint8_t last[36]; mkpose(last, ++tick, 4, 0x1f | 0x20, 0xa5);
    CHECK(raw_send(&a, M_POSE, last, 36)); CHECK(sync_peer(&a));
    static RawPeer late; CHECK(join(&late, port, "late", 3) != 0);
    uint8_t got[80]; size_t gn = 0; bool replay = false;
    for (int i = 0; i < 6 && !replay; i++) {
        if (raw_expect(&late, M_POSE, got, sizeof got, &gn, 1500) != M_POSE) break;
        replay = gn == 38 && g16(got) == ia && !memcmp(got + 2, last, 36);
    }
    CHECK(replay);
    // Scene change clears the host's pose; nothing stale is replayed for a peer in a new scene.
    raw_close(&late); raw_close(&a); raw_close(&b);
    sst_net_stop(); sst_host_stop();
}

int main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);
    if (!getenv("SST_HOST_BIND"))
#ifdef _WIN32
        _putenv("SST_HOST_BIND=127.0.0.1");
#else
        setenv("SST_HOST_BIND", "127.0.0.1", 1);
#endif
    RUN(hello_version_matrix);
    RUN(client_stops_on_mismatch_error);
    RUN(pose_is_36_bytes);
    RUN(client_pose_is_36_bytes);
    RUN(unknown_types_host);
    RUN(unknown_types_client);
    RUN(equip_flags_and_pad_round_trip);
    TEST_EXIT();
}
