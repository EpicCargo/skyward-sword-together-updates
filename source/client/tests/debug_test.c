// Developer diagnostics: bounded log, detail gating, enemy table and connection-phase
// classification for each failure class. Loopback only.
#include "sst_test.h"
#include "../src/sst_debug.h"
#include "../src/sst_host.h"
#include "../src/sst_game.h"

#ifdef _WIN32
#define SET_ENV(assignment) _putenv(assignment)
#else
#define SET_ENV(assignment) putenv(assignment)
#endif

static SstConfig config(uint16_t port, bool reconnect, bool udp) {
    SstConfig c = {.port = port, .auto_reconnect = reconnect, .use_udp = udp};
    snprintf(c.host, sizeof c.host, "127.0.0.1");
    snprintf(c.name, sizeof c.name, "diag");
    snprintf(c.room, sizeof c.room, "room");
    return c;
}
static bool log_contains(const char *needle) {
    static SstDebugLine lines[SST_DEBUG_LOG_LINES];
    int n = sst_debug_lines(lines, SST_DEBUG_LOG_LINES, NULL);
    for (int i = 0; i < n; i++) if (strstr(lines[i].text, needle)) return true;
    return false;
}

// SST_HOST_BIND keeps local test hosts off the network (tools/run_tests.py sets it). The all-interfaces default
// is not exercised here: binding it would raise the Windows Firewall prompt.
static void host_bind_override_is_loopback(void) {
    char why[200];
    SET_ENV("SST_HOST_BIND=127.0.0.1");
    sst_debug_clear();
    CHECK(sst_host_start(0, "room", why, sizeof why));
    char want[64];
    snprintf(want, sizeof want, "hosting on TCP port %u;", sst_host_port());
    CHECK(log_contains(want));
    CHECK(!log_contains("all IPv4 interfaces"));
    CHECK(!strcmp(sst_host_address(), "127.0.0.1"));
    sst_host_stop();
    CHECK(sst_host_address()[0] == 0);
}

static void log_is_bounded_and_ordered(void) {
    sst_debug_clear();
    SstDebugLine lines[SST_DEBUG_LOG_LINES + 8];
    uint32_t dropped = 99;
    CHECK(sst_debug_lines(lines, SST_DEBUG_LOG_LINES, &dropped) == 0 && dropped == 0);
    for (int i = 0; i < SST_DEBUG_LOG_LINES + 50; i++) sst_debug_log(SST_DBG_NET, "line %d", i);
    int n = sst_debug_lines(lines, SST_DEBUG_LOG_LINES + 8, &dropped);
    CHECK(n == SST_DEBUG_LOG_LINES);
    CHECK(dropped == 50);
    CHECK(!strcmp(lines[0].text, "line 50"));
    CHECK(!strcmp(lines[n - 1].text, "line 177"));
    for (int i = 1; i < n; i++) CHECK(lines[i].serial == lines[i - 1].serial + 1);
    // A smaller request returns the most recent lines, still oldest first.
    CHECK(sst_debug_lines(lines, 3, NULL) == 3 && !strcmp(lines[2].text, "line 177") && !strcmp(lines[0].text, "line 175"));
    // Long text is truncated, never overflowed.
    char big[1000]; memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
    sst_debug_log(SST_DBG_HOST, "%s", big);
    n = sst_debug_lines(lines, 1, NULL);
    CHECK(n == 1 && strlen(lines[0].text) == SST_DEBUG_LINE - 1 && lines[0].category == SST_DBG_HOST);
    sst_debug_clear();
    CHECK(sst_debug_lines(lines, 8, &dropped) == 0 && dropped == 0);
    sst_debug_log((SstDebugCategory)-1,"invalid");sst_debug_set_detail((SstDebugCategory)-1,true);
    CHECK(!sst_debug_detail_enabled((SstDebugCategory)-1));
    CHECK(!strcmp(sst_debug_category_name((SstDebugCategory)-1),"?"));
    CHECK(sst_debug_lines(NULL,128,NULL)==0 && sst_debug_lines(lines,-1,NULL)==0);
    CHECK(sst_debug_enemies(NULL,128)==0 && sst_debug_enemies(NULL,-1)==0);
    sst_debug_enemy_event(NULL);sst_debug_enemy_motion(NULL);
}

static void detail_is_opt_in(void) {
    sst_debug_clear();
    CHECK(!sst_debug_detail_enabled(SST_DBG_ENEMY));
    sst_debug_detail(SST_DBG_ENEMY, "hidden");
    SstEnemyMotion m = {.scene = {.stage = "D100", .room = 1}, .key = 77, .sequence = 5, .profile = 9, .from = 2,
                        .pos = {1, 2, 3}};
    sst_debug_enemy_motion(&m);
    SstDebugEnemy e[4];
    CHECK(sst_debug_enemies(e, 4) == 0);
    CHECK(!log_contains("hidden"));
    sst_debug_set_detail(SST_DBG_ENEMY, true);
    sst_debug_detail(SST_DBG_ENEMY, "shown");
    CHECK(log_contains("shown"));
    sst_debug_enemy_motion(&m);
    SstEnemyEvent ev = {.scene = m.scene, .key = 77, .sequence = 3, .profile = 9, .health = 4, .damage = 2, .from = 3};
    sst_debug_enemy_event(&ev);
    CHECK(sst_debug_enemies(e, 4) == 1);
    CHECK(e[0].key == 77 && e[0].has_motion && e[0].has_health && e[0].motion_sequence == 5 &&
          e[0].enemy_sequence == 3 && e[0].health == 4 && e[0].from == 2 && e[0].pos[2] == 3);
    m.scene.room=2;sst_debug_enemy_motion(&m);CHECK(sst_debug_enemies(e,4)==2);
    m.profile=10;sst_debug_enemy_motion(&m);CHECK(sst_debug_enemies(e,4)==3);
    // Bounded: more keys than slots evict the least recently updated.
    for (uint64_t k = 1000; k < 1000 + SST_DEBUG_ENEMIES + 10; k++) { m.key = k; sst_debug_enemy_motion(&m); }
    static SstDebugEnemy all[SST_DEBUG_ENEMIES + 4];
    CHECK(sst_debug_enemies(all, SST_DEBUG_ENEMIES + 4) == SST_DEBUG_ENEMIES);
    sst_debug_set_detail(SST_DBG_ENEMY, false);
    CHECK(sst_debug_enemies(all, 4) == 0);
}

static void timeout_is_classified_before_handshake(void) {
#ifdef _WIN32
    sst_debug_clear();
    sst_socket_init();
    SstSocket s = socket(AF_INET, SOCK_STREAM, 0);
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_CONDITIONAL_ACCEPT, (const char *)&on, sizeof on);
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (void *)&a, sizeof a); listen(s, 1);
    SstSocklen len = sizeof a; getsockname(s, (void *)&a, &len);
    SET_ENV("SST_CONNECT_TIMEOUT_MS=1000");
    SstConfig c = config(ntohs(a.sin_port), true, false);
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(sst_net_diag().phase == SST_PHASE_TCP_CONNECT, 1000));
    CHECK(WAIT_UNTIL(sst_net_diag().phase == SST_PHASE_BACKOFF, 3000));
    SstNetDiag d = sst_net_diag();
    CHECK(d.failure == SST_FAIL_CONNECT_TIMEOUT);
    CHECK(d.connect_timeout_ms == 1000);
    CHECK(d.attempts == 1 && d.consecutive_failures == 1 && d.handshakes == 0 && !d.has_handshake);
    CHECK(d.next_retry_in_ms > 0 && d.next_retry_in_ms <= 1000);
    CHECK(strstr(d.last_error, "timed out") != NULL);
    CHECK(strstr(sst_net_failure_hint(d.failure), "save files cannot cause this") != NULL);
    // Automatic retry continues with backoff and counts attempts.
    CHECK(WAIT_UNTIL(sst_net_diag().attempts >= 2, 4000));
    CHECK(log_contains("TCP connect attempt") && log_contains("retrying in 1 s"));
    sst_net_stop();
    CHECK(sst_net_diag().phase == SST_PHASE_IDLE);
    SET_ENV("SST_CONNECT_TIMEOUT_MS=");
    sst_socket_close(s);
#endif
}

static void refused_is_classified(void) {
    uint16_t port; SstSocket l = listen_loopback(&port); sst_socket_close(l);
    SstConfig c = config(port, false, false);
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(sst_net_diag().phase == SST_PHASE_IDLE, 6000));
    SstNetDiag d = sst_net_diag();
    CHECK(d.failure == SST_FAIL_CONNECT_REFUSED);
    CHECK(d.status == SST_OFFLINE);
    sst_net_stop();
}

static void handshake_timeout_is_classified(void) {
    // Something accepts TCP on the port but never speaks the protocol.
    SET_ENV("SST_HANDSHAKE_TIMEOUT_MS=800");
    uint16_t port; SstSocket l = listen_loopback(&port);
    SstConfig c = config(port, false, false);
    sst_net_start(&c);
    SstSocket fd = accept_within(l, 2000);
    CHECK(fd != SST_INVALID_SOCKET);
    CHECK(WAIT_UNTIL(sst_net_diag().phase == SST_PHASE_HANDSHAKE, 500));
    RawPeer keepalive={.fd=fd};uint8_t pong[4]={0};
    for(unsigned i=0;i<3;i++){CHECK(raw_send(&keepalive,0x07,pong,sizeof pong));sleep_ms(100);}
    CHECK(WAIT_UNTIL(sst_net_diag().failure == SST_FAIL_HANDSHAKE_TIMEOUT, 3000));
    char why[160]; sst_net_status(why, sizeof why);
    CHECK(strstr(why, "accepted TCP but sent no co-op welcome") != NULL);
    sst_net_stop();
    SET_ENV("SST_HANDSHAKE_TIMEOUT_MS=");
    sst_socket_close(fd); sst_socket_close(l);
}

static void rejection_and_success_are_reported(void) {
    sst_debug_clear();
    char why[160];
    CHECK(sst_host_start(0, "room", why, sizeof why));
    SstConfig c = config(sst_host_port(), false, true);
    snprintf(c.room, sizeof c.room, "other");
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(sst_net_status(NULL, 0) == SST_FAILED, 3000));
    CHECK(sst_net_diag().failure == SST_FAIL_REJECTED);
    CHECK(log_contains("host refused/closed"));
    CHECK(log_contains("host rejected join"));
    sst_net_stop();

    snprintf(c.room, sizeof c.room, "room");
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(sst_net_status(NULL, 0) == SST_ONLINE, 3000));
    SstNetDiag d = sst_net_diag();
    CHECK(d.phase == SST_PHASE_ONLINE && d.failure == SST_FAIL_NONE && d.handshakes == 1);
    CHECK(d.my_id != 0 && d.protocol == SST_PROTO && d.has_handshake && d.last_error[0] == 0);
    CHECK(log_contains("handshake complete") && log_contains("welcomed player"));
    CHECK(!log_contains(c.host) && !log_contains(c.name) && !log_contains(c.room));
    // UDP movement probes are answered on loopback: loss is measurable and near zero.
    CHECK(WAIT_UNTIL(sst_net_diag().udp_probes_acked >= 2, 3000));
    d = sst_net_diag();
    CHECK(d.udp_probes_sent >= d.udp_probes_acked && d.udp_probes_sent - d.udp_probes_acked <= 1);
    sst_net_stop();
    sst_host_stop();
    CHECK(log_contains("hosting stopped"));
}

static void invalid_address_is_reported(void) {
    sst_net_set_offline_error(SST_FAIL_ADDRESS, "Invalid host address: private-address-sentinel");
    char why[160]; SstStatus st = sst_net_status(why, sizeof why);
    CHECK(st == SST_OFFLINE);
    CHECK(strstr(why, "Invalid host address") != NULL);
    CHECK(sst_net_diag().failure == SST_FAIL_ADDRESS);
    CHECK(!log_contains("private-address-sentinel"));
}

static void invalid_explicit_bind_never_opens_all_interfaces(void) {
    char why[160];SET_ENV("SST_HOST_BIND=not-an-ip");
    CHECK(!sst_host_start(0,"private-room-sentinel",why,sizeof why));
    CHECK(!sst_host_port() && !sst_host_address()[0] && strstr(why,"Invalid SST_HOST_BIND"));
    CHECK(!log_contains("private-room-sentinel"));SET_ENV("SST_HOST_BIND=127.0.0.1");
}

int main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);
    SET_ENV("SST_HOST_BIND=127.0.0.1");
    RUN(log_is_bounded_and_ordered);
    RUN(detail_is_opt_in);
    RUN(timeout_is_classified_before_handshake);
    RUN(refused_is_classified);
    RUN(handshake_timeout_is_classified);
    RUN(rejection_and_success_are_reported);
    RUN(invalid_address_is_reported);
    RUN(host_bind_override_is_loopback);
    RUN(invalid_explicit_bind_never_opens_all_interfaces);
    TEST_EXIT();
}
