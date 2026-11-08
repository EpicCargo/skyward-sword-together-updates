// Game glue: reads the local player from game memory, feeds the network client, and keeps the
// multiplayer settings. Addresses live in an SstGameLayout so tests can run on mock memory.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "sst_net.h"
#include "sst_overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t player_ptr;     // address of the global holding the dPlayer pointer
    uint64_t stage_name;     // current stage name (char[8])
    uint64_t room;           // current room (s8)
    uint64_t layer, entrance;
    uint64_t file_mgr_ptr;   // optional FileMgr pointer, for current/max health
    uint64_t camera_ptr;
    uint64_t guest_lo, guest_hi; // optional bounds for guest pointers (exclusive upper bound)
    uint32_t off_pos;        // dPlayer offsets
    uint32_t off_rot;
    uint32_t off_action;
    uint32_t off_health, off_max_health; // FileMgr offsets
    uint32_t off_pouch, off_item_flags, off_story_flags; // optional SSHD health-bonus rules
    uint64_t minigame_ptr, minigame_mode;
    uint32_t off_view, off_fovy;
    // Optional HD player-model controller layout (six native animation tracks).
    uint32_t off_anim_index, off_anim_ids, off_anim_tracks, anim_track_stride, off_anim_frame, off_anim_length;
    uint32_t off_player_instance;
    uint32_t off_carried_actor; // optional held-object getter field; never dereferenced
    uint32_t off_item_being_used; // optional verified native u16 active-item field
    uint64_t expected_player_vtable; // required exact identity before active-item reads
} SstGameLayout;

// SSHD 1.0.0 in SkyRecomp. Source: sshd-rando symbols.yaml / actor.rs, rebased from 0x71xxxxxxxx to
// SkyRecomp's 0x70xxxxxxxx. Matches the v1.0.0 main build ID; see docs/STATUS.md for runtime verification.
extern const SstGameLayout SST_LAYOUT_SSHD_100;

typedef enum {
    SST_SWORD_USE_UNKNOWN = 0, // optional field disabled: preserve legacy observed bits
    SST_SWORD_USE_NOT_IN_USE,
    SST_SWORD_USE_IN_USE,
    SST_SWORD_USE_UNAVAILABLE // configured but identity/range unavailable: clear drawn
} SstSwordUse;

typedef struct {
    bool valid;
    SstScene scene;
    SstPose pose;
    float animation_length; // local controller metadata; not serialized
    uint64_t player_address;
    uint32_t player_instance;
    SstSwordUse sword_use; // host-only metadata, never serialized
} SstLocal;

// Reads the local player through `layout`. valid=false when there is no player or the data looks wrong.
SstLocal sst_game_read_local(const SstGameLayout *layout, uint32_t tick);
// Fresh renderer metadata for the currently published actor/scene only. Never
// falls back to the older published pose. Missing layout/published data, invalid
// published data or disabled reads return zero before touching native memory.
// Fresh actor/scene changes return zero and defer rendering
// until the publisher observes the transition and resets animation readiness.
// Retains read_local's optional-field semantics (including untrusted animation).
// Does not publish or advance the pose clock. Native sword-use metadata is read
// under the same guards; observed geometry ownership is merged separately.
SstLocal sst_game_read_render_local(const SstGameLayout *layout, const SstLocal *published, uint32_t tick);
// Returns ONLY sword equipment bits0..1. Native IN_USE yields SWORD|DRAWN;
// NOT_IN_USE/UNAVAILABLE retain only observed ownership. UNKNOWN retains legacy
// observed bits. NULL/invalid state clears DRAWN. This does not infer ownership
// from a native false value, publish data, or generate any animation pose.
uint8_t sst_game_equipment_flags(const SstLocal *local, uint8_t observed);
// Most recent sample, including while offline. Called on the engine's render thread.
SstLocal sst_game_local(void);
uint32_t sst_game_pose_tick(void); // atomic render clock for same-frame guest-thread observations
bool sst_game_read_camera(const SstGameLayout *layout, float width, float height, SstCamera *out);

typedef struct {
    char name[SST_STR], room[SST_STR], team[SST_STR];
    char server[160];        // host:port
    bool auto_reconnect;
    bool use_udp;
    bool show_names;
    bool show_avatars;
    bool sync_progress, sync_enemies;
    bool sync_enemy_motion;
    uint16_t host_port;
    bool sync_props;
    bool sync_pickups;
} SstSettings;

extern SstSettings g_sst;

// Parses "host[:port]" (default port 7623). false: empty or bad port.
bool sst_parse_server(const char *s, char *host, int host_size, uint16_t *port);

void sst_connect(void);
bool sst_host_game(char *why, size_t why_size);
void sst_disconnect(void);
// Ephemeral launcher-created online session; never saved over direct connection settings.
bool sst_game_session(char *code, size_t capacity, bool *creator);
const char *sst_game_team(void); // effective connection team; saved direct-play preference is unchanged
void sst_game_reset(void); // engine reboot, after the old guest threads have stopped
// Once per presented frame, from the engine.
void sst_frame(uint64_t frame);

// settings.c integration: keys start with "sst_".
bool sst_settings_load(const char *key, const char *val);
void sst_settings_save(FILE *f);

#ifdef __cplusplus
}
#endif
