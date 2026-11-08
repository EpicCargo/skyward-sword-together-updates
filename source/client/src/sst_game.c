// See sst_game.h.
#include "sst_game.h"
#include "sst_host.h"
#include "sst_world.h"
#include "sst_enemy_motion.h"
#include "sst_animation.h"
#include "sst_avatar.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <time.h>

// dPlayer = vtable(8) + dBase members(0xC0) + dAcOBase members, whose dAcBase part has rot at +0x74
// and pos at +0x7C (sshd-rando actor.rs).
const SstGameLayout SST_LAYOUT_SSHD_100 = {
    // Rando's main starts at 0x7100004000; SkyRecomp's starts at 0x7000000000.
    .player_ptr = 0x7002bf9940ull,
    .stage_name = 0x7002bf98d8ull,
    .room = 0x7002bf98faull,
    .layer = 0x7002bf98fbull,
    .entrance = 0x7002bf98fcull,
    .file_mgr_ptr = 0x7002bf9cc0ull,
    .camera_ptr = 0x7002bf9958ull,
    .guest_lo = 0x7000000000ull,
    .guest_hi = 0x7320000000ull,
    .off_pos = 0xC8 + 0x7C,
    .off_rot = 0xC8 + 0x74,
    .off_action = 0x468,
    .off_health = 0x5316,
    .off_max_health = 0x5312,
    .off_pouch = 0x7d0, .off_item_flags = 0x9f4, .off_story_flags = 0x8f4,
    .minigame_ptr = 0x700192fb58ull, .minigame_mode = 0x700166cc10ull,
    .off_view = 0xf0,
    .off_fovy = 0xd8,
    .off_anim_index = 0x1a2c, .off_anim_ids = 0x1a3a,
    .off_anim_tracks = 0xb08, .anim_track_stride = 0x58, .off_anim_frame = 0x50, .off_anim_length = 0x48,
    .off_player_instance = 8,
    .off_carried_actor = 0x1b90,
    .off_item_being_used = 0x6404,
    .expected_player_vtable = UINT64_C(0x7001637d28),
};

SstSettings g_sst = {.server = "127.0.0.1:7623", .auto_reconnect = true, .use_udp = true, .show_names = true, .show_avatars = true,
    .sync_progress = true, .sync_enemies = true, .sync_enemy_motion = true, .host_port = 7623, .sync_props = true, .sync_pickups = true};

#define RD(T, a) (*(const T *)(uintptr_t)(a))

static bool pointer_fits(const SstGameLayout *l, uint64_t p, uint32_t offset, size_t bytes) {
    if (!p || p > UINT64_MAX - offset || p + offset > UINT64_MAX - bytes) return false;
    return !l->guest_hi || (p >= l->guest_lo && p + offset + bytes <= l->guest_hi);
}

static uint16_t read16(uint64_t a) {
    uint16_t v; memcpy(&v, (const void *)(uintptr_t)a, sizeof v); return v;
}
static unsigned health_capacity(const SstGameLayout *l, uint64_t fm, unsigned base) {
    if (!l->off_pouch || !l->off_item_flags || !l->off_story_flags ||
        !pointer_fits(l, fm, l->off_pouch, 32) ||
        !pointer_fits(l, fm, l->off_item_flags, 128) ||
        !pointer_fits(l, fm, l->off_story_flags, 256)) return base;
    // Verified 1.0.0 native healing rules: story 166 and minigame mode 9 suppress medals.
    if (read16(fm + l->off_story_flags + 10) & (1u << 5)) return base;
    if (l->minigame_ptr && l->minigame_mode) {
        uint64_t mg; uint32_t mode;
        memcpy(&mg, (const void *)(uintptr_t)l->minigame_ptr, sizeof mg);
        memcpy(&mode, (const void *)(uintptr_t)l->minigame_mode, sizeof mode);
        if (mg && mode == 9) return base;
    }
    uint64_t items = fm + l->off_item_flags;
    unsigned slots = (read16(items + 108) >> 10) & 7; // logical counter 490
    if (read16(items + 10) & (1u << 6)) slots += 4; // Adventure Pouch ownership 112
    if (slots > 8) slots = 8;
    unsigned capacity = base;
    for (unsigned i = 0; i < slots; i++)
        if (read16(fm + l->off_pouch + i * 4) == 114) capacity += 4; // Life Medal
    return capacity < 80 ? capacity : 80;
}
// Native observed steady small-vase posture: slot5 LiftS_UtoD431 at4.5/9.
// The primary gait keeps playing, so that gait alone is not a full pose identity.
static uint8_t native_pose_context(const SstGameLayout *l,uint64_t player) {
    if(!l->off_carried_actor)return 0;
    if(!pointer_fits(l,player,l->off_carried_actor,8))return SST_POSE_CONTEXT_UNSUPPORTED;
    uint64_t carried;memcpy(&carried,(void *)(uintptr_t)(player+l->off_carried_actor),8);
    if(!carried)return 0;
    if(!l->off_anim_ids || !l->off_anim_tracks || !l->anim_track_stride || !l->off_anim_frame || !l->off_anim_length)
        return SST_POSE_CONTEXT_UNSUPPORTED;
    uint64_t id=(uint64_t)l->off_anim_ids+5*2;
    uint64_t phase=(uint64_t)l->off_anim_tracks+5*(uint64_t)l->anim_track_stride+l->off_anim_frame;
    uint64_t length=(uint64_t)l->off_anim_tracks+5*(uint64_t)l->anim_track_stride+l->off_anim_length;
    if(id>UINT32_MAX || phase>UINT32_MAX || length>UINT32_MAX ||
       !pointer_fits(l,player,(uint32_t)id,2) || !pointer_fits(l,player,(uint32_t)phase,4) ||
       !pointer_fits(l,player,(uint32_t)length,4))return SST_POSE_CONTEXT_UNSUPPORTED;
    float f,n;memcpy(&f,(void *)(uintptr_t)(player+phase),4);memcpy(&n,(void *)(uintptr_t)(player+length),4);
    if(read16(player+id)==431 && isfinite(f) && isfinite(n) && fabsf(f-4.5f)<.001f && fabsf(n-9.f)<.001f)
        return SST_POSE_CONTEXT_SMALL_CARRY;
    return SST_POSE_CONTEXT_UNSUPPORTED;
}
// Pinned1.0.0 is_using_sword getter: u16(player+0x6404)==ITEM_BEING_USED::SWORD.
// Validate the vtable before touching the optional field on this native object.
static SstSwordUse native_sword_use(const SstGameLayout *l,uint64_t player) {
    if(!l->off_item_being_used)return SST_SWORD_USE_UNKNOWN;
    if(!l->expected_player_vtable || !pointer_fits(l,player,0,8))return SST_SWORD_USE_UNAVAILABLE;
    uint64_t vtable;memcpy(&vtable,(const void *)(uintptr_t)player,8);
    if(vtable!=l->expected_player_vtable || !pointer_fits(l,player,l->off_item_being_used,2))
        return SST_SWORD_USE_UNAVAILABLE;
    return read16(player+l->off_item_being_used)==0x11?SST_SWORD_USE_IN_USE:SST_SWORD_USE_NOT_IN_USE;
}

uint8_t sst_game_equipment_flags(const SstLocal *local,uint8_t observed) {
    if(local) {
        if(local->sword_use==SST_SWORD_USE_IN_USE)return SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN;
        if(local->sword_use==SST_SWORD_USE_UNKNOWN)return observed&(SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN);
    }
    return observed&SST_EQUIP_SWORD;
}

SstLocal sst_game_read_local(const SstGameLayout *l, uint32_t tick) {
    SstLocal out = {0};
    uint64_t pl;
    memcpy(&pl, (const void *)(uintptr_t)l->player_ptr, 8);
    if (!pointer_fits(l, pl, l->off_pos, 12) || !pointer_fits(l, pl, l->off_rot, 6)) return out;
    memcpy(out.pose.pos, (const void *)(uintptr_t)(pl + l->off_pos), 12);
    memcpy(out.pose.rot, (const void *)(uintptr_t)(pl + l->off_rot), 6);
    for (int i = 0; i < 3; i++)
        if (!isfinite(out.pose.pos[i]) || fabsf(out.pose.pos[i]) > 1e6f) return out;  // stale or wrong pointer
    const char *stage = (const char *)(uintptr_t)l->stage_name;
    int n = 0;
    for (; n < 8 && stage[n]; n++)
        if (stage[n] < 0x20 || stage[n] > 0x7e) return out;
    if (n == 0) return out;
    memcpy(out.scene.stage, stage, n);
    out.scene.stage[n] = 0;
    out.scene.room = (uint8_t)RD(int8_t, l->room);
    if (l->layer) out.scene.layer = RD(uint8_t, l->layer);
    if (l->entrance) out.scene.entrance = RD(uint8_t, l->entrance);
    // These verified title backgrounds also create a dPlayer. FileMgr may still contain a
    // previous save's health when returning to the title.
    if (!strcmp(out.scene.stage, "F000") && out.scene.room == 0 &&
        ((out.scene.layer == 28 && out.scene.entrance == 48) ||
         (out.scene.layer == 26 && out.scene.entrance == 55))) return (SstLocal){0};
    if (l->off_action && pointer_fits(l, pl, l->off_action, 4)) {
        uint32_t action;
        memcpy(&action, (const void *)(uintptr_t)(pl + l->off_action), 4);
        if (action <= UINT16_MAX) out.pose.action = (uint16_t)action;
    }
    if (l->off_anim_index) {
        out.pose.anim_id = UINT16_MAX; // No trustworthy controller during a model change/load.
        if (l->off_anim_ids && l->off_anim_tracks && l->anim_track_stride &&
            pointer_fits(l, pl, l->off_anim_index, 1)) {
            unsigned index = RD(uint8_t, pl + l->off_anim_index);
            uint64_t id_offset = (uint64_t)l->off_anim_ids + index * 2;
            uint64_t frame_offset = (uint64_t)l->off_anim_tracks +
                                    (uint64_t)index * l->anim_track_stride + l->off_anim_frame;
            if (index < 6 && id_offset <= UINT32_MAX && frame_offset <= UINT32_MAX &&
                pointer_fits(l, pl, (uint32_t)id_offset, 2) &&
                pointer_fits(l, pl, (uint32_t)frame_offset, 4)) {
                uint16_t id = read16(pl + id_offset);
                float frame; memcpy(&frame, (const void *)(uintptr_t)(pl + frame_offset), sizeof frame);
                if (id < 443 && isfinite(frame) && frame >= 0 && frame <= 65536) {
                    out.pose.anim_id = id;
                    out.pose.anim_frame = frame;
                    uint64_t length_offset = (uint64_t)l->off_anim_tracks +
                                            (uint64_t)index * l->anim_track_stride + l->off_anim_length;
                    if (l->off_anim_length && length_offset <= UINT32_MAX &&
                        pointer_fits(l, pl, (uint32_t)length_offset, 4)) {
                        float length;
                        memcpy(&length, (const void *)(uintptr_t)(pl + length_offset), sizeof length);
                        if (isfinite(length) && length > 0 && length <= 65536 && frame <= length)
                            out.animation_length = length;
                    }
                }
            }
        }
    }
    if (l->file_mgr_ptr) {
        uint64_t fm;
        memcpy(&fm, (const void *)(uintptr_t)l->file_mgr_ptr, 8);
        if (pointer_fits(l, fm, l->off_health, 2) && pointer_fits(l, fm, l->off_max_health, 2)) {
            memcpy(&out.pose.health, (const void *)(uintptr_t)(fm + l->off_health), 2);
            memcpy(&out.pose.max_health, (const void *)(uintptr_t)(fm + l->off_max_health), 2);
            if (out.pose.max_health && out.pose.max_health <= 80)
                out.pose.max_health = health_capacity(l, fm, out.pose.max_health);
            if (out.pose.max_health > 80 || out.pose.health > out.pose.max_health)
                out.pose.health = out.pose.max_health = 0;
        }
    }
    out.pose.equip_flags = native_pose_context(l,pl);
    out.sword_use = native_sword_use(l,pl);
    if(out.sword_use==SST_SWORD_USE_IN_USE)out.pose.equip_flags |= SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN;
    out.pose.tick = tick;
    out.player_address = pl;
    if (l->off_player_instance && pointer_fits(l, pl, l->off_player_instance, 4))
        memcpy(&out.player_instance, (const void *)(uintptr_t)(pl + l->off_player_instance), 4);
    out.valid = true;
    return out;
}

SstLocal sst_game_read_render_local(const SstGameLayout *layout,const SstLocal *published,uint32_t tick) {
    if(!layout || !published || !published->valid || getenv("SST_NO_PLAYER_READ"))return (SstLocal){0};
    SstLocal fresh=sst_game_read_local(layout,tick);
    if(!fresh.valid || fresh.player_address!=published->player_address ||
       fresh.player_instance!=published->player_instance ||
       strncmp(fresh.scene.stage,published->scene.stage,sizeof fresh.scene.stage) ||
       fresh.scene.room!=published->scene.room || fresh.scene.layer!=published->scene.layer ||
       fresh.scene.entrance!=published->scene.entrance)return (SstLocal){0};
    return fresh;
}

bool sst_game_read_camera(const SstGameLayout *l,float w,float h,SstCamera *out) {
    if(!l->camera_ptr || !isfinite(w) || !isfinite(h) || w<=0 || h<=0)return false;
    uint64_t camera;memcpy(&camera,(void *)(uintptr_t)l->camera_ptr,8);
    if(!pointer_fits(l,camera,l->off_view,48) || !pointer_fits(l,camera,l->off_fovy,4))return false;
    float view[12],fov;
    memcpy(view,(void *)(uintptr_t)(camera+l->off_view),sizeof view);
    memcpy(&fov,(void *)(uintptr_t)(camera+l->off_fovy),4);
    if(!isfinite(fov) || fov<5 || fov>150)return false;
    // dCamera's view is a row-major affine world-to-view matrix. Reject uninitialized bases.
    for(int r=0;r<3;r++) {
        float norm=0;
        for(int c=0;c<4;c++)if(!isfinite(view[r*4+c]))return false;
        for(int c=0;c<3;c++)norm+=view[r*4+c]*view[r*4+c];
        if(norm<.95f || norm>1.05f)return false;
    }
    float fy=1.0f/tanf(fov*(3.14159265358979323846f/360.0f));
    memset(out,0,sizeof *out);out->screen_w=w;out->screen_h=h;
    for(int c=0;c<4;c++) {
        out->view_proj[c]=view[c]*fy/(w/h);
        out->view_proj[4+c]=view[4+c]*fy;
        out->view_proj[8+c]=-view[8+c];
        out->view_proj[12+c]=-view[8+c];
    }
    return true;
}

bool sst_parse_server(const char *in, char *host, int host_size, uint16_t *port) {
    char s[256];
    while (*in == ' ') in++;
    if (!strncmp(in, "tcp://", 6)) in += 6;
    snprintf(s, sizeof s, "%s", in);
    for (int n = (int)strlen(s); n > 0 && s[n - 1] == ' '; n--) s[n - 1] = 0;
    const char *h = s, *p = NULL;
    if (s[0] == '[') {  // [ipv6]:port
        char *end = strchr(s, ']');
        if (!end) return false;
        *end = 0;
        h = s + 1;
        if (end[1] == ':') p = end + 2;
        else if (end[1]) return false;
    } else {
        char *colon = strchr(s, ':');
        if (colon && colon == strrchr(s, ':')) { *colon = 0; p = colon + 1; }  // one colon: host:port; more: bare ipv6
    }
    *port = 7623;
    if (p) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || *end || v < 1 || v > 65535) return false;
        *port = (uint16_t)v;
    }
    int hl = (int)strlen(h);
    if (hl == 0 || hl >= host_size) return false;
    memcpy(host, h, hl + 1);
    return true;
}

static bool g_wanted;  // the player pressed Connect
static SstLocal g_local;
static bool g_initialized;
static uint64_t g_last_ms, g_last_trace;
static _Atomic uint32_t g_tick;
static SstScene g_last_scene;
static uint64_t g_avatar_player;
static uint32_t g_avatar_instance;
static char g_session_code[7];
static bool g_session_creator;
static atomic_bool g_session_active;
static uint64_t g_session_lease_check;
const char *sst_game_team(void) { return atomic_load(&g_session_active) ? "" : g_sst.team; }
bool sst_game_session(char *code, size_t capacity, bool *creator) {
    if (code && capacity) snprintf(code, capacity, "%s", g_session_code);
    if (creator) *creator = g_session_creator;
    return g_session_code[0] != 0;
}
SstLocal sst_game_local(void) { return g_local; }
uint32_t sst_game_pose_tick(void) { return atomic_load(&g_tick); }

static SstConfig net_config(void) {
    SstConfig c = {.auto_reconnect = g_sst.auto_reconnect, .use_udp = g_sst.use_udp};
    snprintf(c.name, sizeof c.name, "%s", g_sst.name[0] ? g_sst.name : "Player");
    snprintf(c.room, sizeof c.room, "%s", g_sst.room);
    snprintf(c.team, sizeof c.team, "%s", g_sst.team);
    return c;
}

void sst_connect(void) {
    SstConfig c = net_config();
    if (!sst_parse_server(g_sst.server, c.host, sizeof c.host, &c.port)) {
        sst_net_set_offline_error(SST_FAIL_ADDRESS, "Invalid host address. Enter a host name or IP with an optional :port.");
        return;
    }
    atomic_store(&g_session_active, false);
    g_session_code[0] = 0; g_session_creator = false;
    sst_net_stop();
    sst_host_stop();
    g_wanted = true;
    sst_net_start(&c);
    sst_net_send_scene(&g_local.scene);
}

bool sst_host_game(char *why, size_t cap) {
    sst_disconnect();
    if (!sst_host_start(g_sst.host_port, g_sst.room, why, cap)) {
        sst_net_set_offline_error(SST_FAIL_SOCKET, "Could not start the local host. Check the hosting port and bind address.");
        return false;
    }
    SstConfig c = net_config();
    const char *bound = sst_host_address();
    snprintf(c.host, sizeof c.host, "%s", bound && bound[0] && strcmp(bound, "0.0.0.0") ? bound : "127.0.0.1");
    c.port = sst_host_port();
    g_wanted = true;
    sst_net_start(&c);
    sst_net_send_scene(&g_local.scene);
    return true;
}

void sst_disconnect(void) {
    atomic_store(&g_session_active, false);
    g_session_lease_check = 0;
    g_session_code[0] = 0; g_session_creator = false;
    g_wanted = false;
    sst_net_stop();
    sst_host_stop();
}

static bool connect_launcher_session(void) {
    const char *server = getenv("SST_SESSION_SERVER"), *room = getenv("SST_SESSION_ROOM");
    if (!server && !room) return false;
    SstConfig c = net_config();
    bool valid = server && room && strlen(room) == 6 &&
        sst_parse_server(server, c.host, sizeof c.host, &c.port) && !strcmp(c.host, "127.0.0.1");
    if (valid) for (unsigned i = 0; i < 6; i++)
        if (!strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", room[i])) valid = false;
    if (!valid) {
        sst_net_set_offline_error(SST_FAIL_ADDRESS, "The launcher's online connection is invalid. Reopen the launcher and join the room again.");
        return true; // A malformed online session must not connect to a saved unrelated server.
    }
    sst_disconnect();
    snprintf(c.room, sizeof c.room, "%s", room); c.team[0] = 0; c.use_udp = false;
    snprintf(g_session_code, sizeof g_session_code, "%s", room);
    const char *role = getenv("SST_SESSION_ROLE"); g_session_creator = role && !strcmp(role, "host");
    atomic_store(&g_session_active, true);
    g_wanted = true; sst_net_start(&c); sst_net_send_scene(&g_local.scene);
    return true;
}

void sst_game_reset(void) {
    sst_disconnect();
    sst_world_reset();
    sst_animation_reset();
    sst_avatar_equipment_reset();
    g_initialized = false;
    g_last_ms = g_last_trace = g_tick = 0;
    memset(&g_local, 0, sizeof g_local);
    memset(&g_last_scene, 0, sizeof g_last_scene);
    g_avatar_player = 0;
    g_avatar_instance = 0;
}

void sst_frame(uint64_t frame) {
    sst_world_configure(g_sst.sync_progress, g_sst.sync_enemies);
    sst_motion_configure(g_sst.sync_enemy_motion && g_sst.sync_enemies);
    if (!g_initialized) {
        g_initialized = true;
        const char *connect = getenv("SST_AUTOCONNECT");
        const char *hosting = getenv("SST_AUTOHOST");
        if (hosting && !strcmp(hosting, "1")) {
            char why[160];
            if (!sst_host_game(why, sizeof why)) fprintf(stderr, "[sst] %s\n", why);
        } else if (connect && !strcmp(connect, "1") && !connect_launcher_session()) sst_connect();
    }
    uint64_t now = sst_now_ms();
    if (atomic_load(&g_session_active) && now - g_session_lease_check >= 250) {
        g_session_lease_check = now;
        const char *lease = getenv("SST_SESSION_LEASE_FILE");
        if (lease && lease[0]) {
            struct stat info;
            time_t wall = time(NULL);
            if (stat(lease, &info) || !S_ISREG(info.st_mode) ||
                (wall >= info.st_mtime && wall - info.st_mtime > 10)) {
                sst_disconnect();
                sst_net_set_offline_error(SST_FAIL_CLOSED, "The online room ended. Rejoin from the SST launcher.");
            }
        }
    }
    if (now - g_last_ms < 33) return;
    g_last_ms = now;
    SstLocal l = {0};
    if (!getenv("SST_NO_PLAYER_READ")) l = sst_game_read_local(&SST_LAYOUT_SSHD_100, ++g_tick);
    if (!g_sst.show_avatars) sst_animation_reset();
    else if (!l.valid || l.player_address != g_avatar_player ||
        l.player_instance != g_avatar_instance) sst_animation_frame(NULL, NULL, frame);
    g_avatar_player = l.player_address;
    g_avatar_instance = l.player_instance;
    l.pose.equip_flags = (uint8_t)((l.pose.equip_flags & ~(SST_EQUIP_SWORD | SST_EQUIP_SWORD_DRAWN)) |
                                 sst_game_equipment_flags(&l,sst_avatar_local_equipment()));
    g_local = l;
    l.pose.equip_flags |= sst_world_pose_flags() | (sst_motion_enabled() ? SST_MOTION_CAPABLE : 0);
    if (getenv("SST_TRACE") && now - g_last_trace >= 1000) {
        g_last_trace = now;
        fprintf(stderr, "[sst] frame=%llu local valid=%d stage=%s room=%u layer=%u entrance=%u pos=%.2f,%.2f,%.2f health=%u/%u action=%u\n",
                (unsigned long long)frame, l.valid, l.scene.stage, l.scene.room, l.scene.layer, l.scene.entrance,
                l.pose.pos[0], l.pose.pos[1], l.pose.pos[2], l.pose.health, l.pose.max_health, l.pose.action);
    }
    if (!g_wanted || sst_net_status(NULL, 0) != SST_ONLINE) return;
    if (strcmp(l.scene.stage, g_last_scene.stage) || l.scene.room != g_last_scene.room ||
        l.scene.layer != g_last_scene.layer || l.scene.entrance != g_last_scene.entrance) {
        g_last_scene = l.scene;
        sst_net_send_scene(&l.scene);
    }
    if (l.valid) sst_net_send_pose(&l.pose);
}

bool sst_settings_load(const char *key, const char *val) {
    if (strncmp(key, "sst_", 4)) return false;
    key += 4;
    if (!strcmp(key, "name")) snprintf(g_sst.name, sizeof g_sst.name, "%s", val);
    else if (!strcmp(key, "room")) snprintf(g_sst.room, sizeof g_sst.room, "%s", val);
    else if (!strcmp(key, "team")) snprintf(g_sst.team, sizeof g_sst.team, "%s", val);
    else if (!strcmp(key, "server")) snprintf(g_sst.server, sizeof g_sst.server, "%s", val);
    else if (!strcmp(key, "auto_reconnect")) g_sst.auto_reconnect = atoi(val) != 0;
    else if (!strcmp(key, "use_udp")) g_sst.use_udp = atoi(val) != 0;
    else if (!strcmp(key, "show_names")) g_sst.show_names = atoi(val) != 0;
    else if (!strcmp(key, "show_avatars")) g_sst.show_avatars = atoi(val) != 0;
    else if (!strcmp(key, "sync_progress")) g_sst.sync_progress = atoi(val) != 0;
    else if (!strcmp(key, "sync_enemies")) g_sst.sync_enemies = atoi(val) != 0;
    else if (!strcmp(key, "sync_enemy_motion")) g_sst.sync_enemy_motion = atoi(val) != 0;
    else if (!strcmp(key, "sync_props")) g_sst.sync_props = atoi(val) != 0;
    else if (!strcmp(key, "sync_pickups")) g_sst.sync_pickups = atoi(val) != 0;
    else if (!strcmp(key, "host_port")) {
        long port = strtol(val, NULL, 10);
        if (port > 0 && port <= 65535) g_sst.host_port = (uint16_t)port;
    }
    return true;
}

void sst_settings_save(FILE *f) {
    fprintf(f, "sst_name = %s\nsst_room = %s\nsst_team = %s\nsst_server = %s\n", g_sst.name, g_sst.room, g_sst.team,
            g_sst.server);
    fprintf(f, "sst_auto_reconnect = %d\nsst_show_names = %d\n", g_sst.auto_reconnect, g_sst.show_names);
    fprintf(f, "sst_host_port = %u\n", g_sst.host_port);
    fprintf(f, "sst_use_udp = %d\n", g_sst.use_udp);
    fprintf(f, "sst_show_avatars = %d\n", g_sst.show_avatars);
    fprintf(f, "sst_sync_progress = %d\nsst_sync_enemies = %d\n", g_sst.sync_progress, g_sst.sync_enemies);
    fprintf(f, "sst_sync_enemy_motion = %d\n", g_sst.sync_enemy_motion);
    fprintf(f, "sst_sync_props = %d\n", g_sst.sync_props);
    fprintf(f, "sst_sync_pickups = %d\n", g_sst.sync_pickups);
}
