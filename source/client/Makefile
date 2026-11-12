CC ?= clang
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11 -D_GNU_SOURCE
B := build
LIB := src/sst_origin.c src/sst_net.c src/sst_host.c src/sst_pose_buffer.c src/sst_remote.c src/sst_avatar.c src/sst_hand.c src/sst_animation.c src/sst_flags.c src/sst_backup.c src/sst_world.c src/sst_enemy_motion.c src/sst_chest.c src/sst_game.c src/sst_scene_history.c src/sst_props.c src/sst_pickup.c src/sst_census.c src/sst_debug.c src/sst_dev_tools.c src/sst_destinations.c src/sst_teleport.c
HDR := $(wildcard src/*.h) $(wildcard src/*.inc)
LDLIBS ?= -lpthread -lm

$(B)/net_e2e: tests/net_e2e.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -o $@ tests/net_e2e.c $(LIB) $(LDLIBS)

$(B)/two_player_sim: tests/two_player_sim.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -o $@ tests/two_player_sim.c $(LIB) $(LDLIBS)

$(B)/overlay_test: $(B)/net_queue_alloc_test $(B)/enemy_motion_merge_test $(B)/protocol_compat_test $(B)/net_regress_test $(B)/debug_test tests/overlay_test.c src/sst_overlay.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/overlay_test.c $(LIB) $(LDLIBS)

$(B)/unit: tests/unit.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/unit.c $(LIB) $(LDLIBS)

$(B)/host_fixture: tests/host_fixture.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/host_fixture.c $(LIB) $(LDLIBS)

$(B)/origin_test: tests/origin_test.c src/sst_origin.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/origin_test.c src/sst_origin.c -lpthread

$(B)/geometry_only_test: tests/geometry_only_test.c src/sst_animation.c src/sst_avatar.c src/sst_hand.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/geometry_only_test.c src/sst_animation.c src/sst_avatar.c src/sst_hand.c -lm

$(B)/phase_admission_test: tests/phase_admission_test.c src/sst_animation.c src/sst_avatar.c src/sst_hand.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/phase_admission_test.c src/sst_animation.c src/sst_avatar.c src/sst_hand.c -lm

$(B)/remote_frame_test: tests/remote_frame_test.c src/sst_remote.c src/sst_pose_buffer.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/remote_frame_test.c src/sst_remote.c src/sst_pose_buffer.c -lpthread -lm

$(B)/remote_test: tests/remote_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/remote_test.c $(LIB) $(LDLIBS)

$(B)/four_player: tests/four_player.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/four_player.c $(LIB) $(LDLIBS)

$(B)/avatar_test: tests/avatar_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/avatar_test.c $(LIB) $(LDLIBS)

$(B)/animation_test: tests/animation_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/animation_test.c $(LIB) $(LDLIBS)

$(B)/carry_reader_test: tests/carry_reader_test.c tests/render_local_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/carry_reader_test.c $(LIB) $(LDLIBS)

$(B)/carry_context_test: tests/carry_context_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/carry_context_test.c $(LIB) $(LDLIBS)

$(B)/carry_cohort_test: tests/carry_cohort_test.c src/sst_animation.c src/sst_avatar.c src/sst_hand.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/carry_cohort_test.c src/sst_animation.c src/sst_avatar.c src/sst_hand.c -lm

$(B)/carry_transport_test: tests/carry_transport_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/carry_transport_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/carry_playout_test: tests/carry_playout_test.c src/sst_pose_buffer.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/carry_playout_test.c src/sst_pose_buffer.c -lm

$(B)/render_local_test: tests/render_local_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/render_local_test.c $(LIB) $(LDLIBS)

$(B)/drawn_context_test: tests/drawn_context_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/drawn_context_test.c $(LIB) $(LDLIBS)

$(B)/dev_tools_test: tests/dev_tools_test.c src/sst_dev_tools.c src/sst_destinations.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/dev_tools_test.c src/sst_dev_tools.c src/sst_destinations.c $(LDLIBS)

$(B)/teleport_test: tests/teleport_test.c src/sst_teleport.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/teleport_test.c src/sst_teleport.c $(LDLIBS)

$(B)/destinations_test: tests/destinations_test.c src/sst_destinations.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/destinations_test.c src/sst_destinations.c $(LDLIBS)

$(B)/sword_state_test: tests/sword_state_test.c tests/render_local_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/sword_state_test.c $(LIB) $(LDLIBS)

$(B)/hand_offset_test: tests/hand_offset_test.c src/sst_hand.c src/sst_avatar.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/hand_offset_test.c src/sst_hand.c src/sst_avatar.c $(LDLIBS)

$(B)/pose_cadence_test: tests/pose_cadence_test.c src/sst_pose_buffer.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/pose_cadence_test.c src/sst_pose_buffer.c -lm

$(B)/world_test: tests/world_test.c $(LIB) $(HDR) src/sst_story_policy.inc | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/world_test.c $(LIB) $(LDLIBS)

$(B)/motion_test: tests/motion_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/motion_test.c $(LIB) $(LDLIBS)

$(B)/stream_test: tests/stream_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/stream_test.c $(LIB) $(LDLIBS)

$(B)/defeat_test: tests/defeat_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/defeat_test.c $(LIB) $(LDLIBS)

$(B)/spawn_test: tests/spawn_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/spawn_test.c $(LIB) $(LDLIBS)

$(B)/chest_test: tests/chest_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/chest_test.c $(LIB) $(LDLIBS)

$(B)/diagnostics_test: tests/diagnostics_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/diagnostics_test.c $(LIB) $(LDLIBS)

$(B)/queue_fixture: tests/queue_fixture.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/queue_fixture.c $(LIB) $(LDLIBS)

$(B)/udp_fixture: tests/udp_fixture.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/udp_fixture.c $(LIB) $(LDLIBS)

$(B)/udp_queue_test: tests/udp_queue_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/udp_queue_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/rejoin_test: tests/rejoin_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/rejoin_test.c $(LIB) $(LDLIBS)

$(B):
	mkdir -p $@

$(B)/scene_history_test: tests/scene_history_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/scene_history_test.c $(LIB) $(LDLIBS)

test: $(B)/prop_roster_test $(B)/origin_test $(B)/geometry_only_test $(B)/phase_admission_test $(B)/remote_frame_test $(B)/drawn_context_test $(B)/dev_tools_test $(B)/teleport_test $(B)/destinations_test $(B)/sword_state_test $(B)/carry_reader_test $(B)/carry_context_test $(B)/carry_cohort_test $(B)/carry_transport_test $(B)/carry_playout_test $(B)/render_local_test $(B)/pose_cadence_test $(B)/hand_offset_test $(B)/motion_queue_test $(B)/session_lifecycle_test $(B)/census_test $(B)/pickup_state_test $(B)/pickup_net_test $(B)/pickup_host_test $(B)/prop_state_test $(B)/prop_net_test $(B)/prop_host_test $(B)/scene_history_test $(B)/defeat_test $(B)/stream_test $(B)/spawn_test $(B)/unit $(B)/overlay_test $(B)/net_e2e $(B)/two_player_sim $(B)/host_fixture $(B)/remote_test $(B)/four_player $(B)/avatar_test $(B)/animation_test $(B)/world_test $(B)/motion_test $(B)/chest_test $(B)/diagnostics_test $(B)/rejoin_test $(B)/network_check $(B)/queue_fixture $(B)/udp_fixture $(B)/udp_queue_test
	./$(B)/presence_frame_test
	./$(B)/scene_incarnation_test
	./$(B)/scene_transport_test
	./$(B)/drawn_context_test
	./$(B)/dev_tools_test
	./$(B)/teleport_test
	./$(B)/destinations_test
	./$(B)/sword_state_test
	./$(B)/carry_reader_test
	./$(B)/carry_context_test
	./$(B)/carry_cohort_test
	./$(B)/carry_transport_test
	./$(B)/carry_playout_test
	./$(B)/render_local_test
	./$(B)/pose_cadence_test
	./$(B)/hand_offset_test
	./$(B)/motion_queue_test
	./$(B)/session_lifecycle_test
	./$(B)/debug_test
	./$(B)/net_regress_test
	./$(B)/protocol_compat_test
	./$(B)/enemy_motion_merge_test
	./$(B)/net_queue_alloc_test
	./$(B)/census_test
	./$(B)/pickup_state_test
	./$(B)/pickup_net_test
	./$(B)/pickup_host_test
	./$(B)/prop_state_test
	./$(B)/prop_roster_test
	./$(B)/prop_net_test
	./$(B)/prop_host_test
	./$(B)/scene_history_test
	./$(B)/unit
	./$(B)/overlay_test
	./$(B)/origin_test
	./$(B)/geometry_only_test
	./$(B)/phase_admission_test
	./$(B)/remote_frame_test
	./$(B)/remote_test
	./$(B)/avatar_test
	./$(B)/animation_test
	./$(B)/world_test
	./$(B)/spawn_test
	./$(B)/stream_test
	./$(B)/defeat_test
	./$(B)/motion_test
	./$(B)/chest_test
	./$(B)/diagnostics_test
	./$(B)/rejoin_test
	./$(B)/udp_queue_test
	python3 tests/queue_e2e.py
	python3 tests/queue_e2e.py tests/fixtures/queue-backlog.json
	python3 tests/refresh_e2e.py
	python3 tests/refresh_host_test.py
	bash tests/run_e2e.sh
	bash tests/run_sim.sh
	python3 tests/host_e2e.py
	python3 tests/room_alias_test.py
	python3 tests/udp_wire_test.py
	python3 tests/udp_client_test.py
	python3 tests/udp_fallback_test.py
	python3 tests/udp_checker_test.py
	python3 tests/udp_e2e.py

.PHONY: test

$(B)/sst-network-check$(EXEEXT): tools/network_check.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -o $@ tools/network_check.c $(LIB) $(LDLIBS)

$(B)/network_check: tools/network_check.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -o $@ tools/network_check.c $(LIB) $(LDLIBS)

$(B)/prop_state_test: tests/prop_state_test.c src/sst_props.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/prop_state_test.c src/sst_props.c -lm

$(B)/prop_net_test: tests/prop_net_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/prop_net_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/prop_host_test: tests/prop_host_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/prop_host_test.c $(filter-out src/sst_host.c,$(LIB)) $(LDLIBS)

$(B)/pickup_state_test: tests/pickup_state_test.c src/sst_pickup.c src/sst_chest.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/pickup_state_test.c src/sst_pickup.c src/sst_chest.c -lm

$(B)/pickup_net_test: tests/pickup_net_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/pickup_net_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/pickup_host_test: tests/pickup_host_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/pickup_host_test.c $(filter-out src/sst_host.c,$(LIB)) $(LDLIBS)

$(B)/census_test: tests/census_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/census_test.c $(LIB) $(LDLIBS)

$(B)/session_lifecycle_test: tests/session_lifecycle_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/session_lifecycle_test.c $(LIB) $(LDLIBS)

$(B)/debug_test: tests/debug_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/debug_test.c $(LIB) $(LDLIBS)

$(B)/net_regress_test: tests/net_regress_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/net_regress_test.c $(LIB) $(LDLIBS)

$(B)/protocol_compat_test: tests/protocol_compat_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/protocol_compat_test.c $(LIB) $(LDLIBS)

$(B)/enemy_motion_merge_test: tests/enemy_motion_merge_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/enemy_motion_merge_test.c $(LIB) $(LDLIBS)

$(B)/net_queue_alloc_test: tests/net_queue_alloc_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/net_queue_alloc_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

# Launcher-owned, asset-free session helper. Reuses every current native-host extension.
$(B)/sst-session-host$(EXEEXT): tools/session_host.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -o $@ tools/session_host.c $(LIB) $(LDLIBS)

.PHONY: session-host-test
session-host-test: $(B)/sst-session-host$(EXEEXT)
	python3 tests/session_host_test.py $(B)/sst-session-host$(EXEEXT)

$(B)/motion_queue_test: tests/motion_queue_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/motion_queue_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/origin_lifetime_test: tests/origin_lifetime_test.c src/sst_origin.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/origin_lifetime_test.c src/sst_origin.c -lpthread

$(B)/origin_reason_test: tests/origin_reason_test.c src/sst_origin.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/origin_reason_test.c src/sst_origin.c -lpthread

$(B)/model_bindings_test: tests/model_bindings_test.c src/sst_model_bindings.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/model_bindings_test.c src/sst_model_bindings.c

$(B)/origin_recovery_test: tests/origin_recovery_test.c src/sst_origin.c $(HDR) ../engine/runtime/nvn/source_policy.h | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/origin_recovery_test.c src/sst_origin.c -lpthread

$(B)/gpu_retirement_test: tests/gpu_retirement_test.c src/sst_origin.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/gpu_retirement_test.c src/sst_origin.c -lpthread

$(B)/prop_roster_test: tests/prop_roster_test.c src/sst_props.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/prop_roster_test.c src/sst_props.c -lm

test: $(B)/presence_frame_test

$(B)/presence_frame_test: tests/presence_frame_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/presence_frame_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/roster_probe: tools/roster_probe.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -o $@ tools/roster_probe.c $(LIB) $(LDLIBS)

# Same-area reload and unobserved scene departures must retire accepted state.
test: $(B)/scene_incarnation_test $(B)/scene_transport_test

$(B)/scene_incarnation_test: tests/scene_incarnation_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/scene_incarnation_test.c $(filter-out src/sst_net.c,$(LIB)) $(LDLIBS)

$(B)/scene_transport_test: tests/scene_transport_test.c tests/rejoin_test.c $(LIB) $(HDR) | $(B)
	$(CC) $(CFLAGS) -Wno-unused-function -fsanitize=address,undefined -o $@ tests/scene_transport_test.c $(LIB) $(LDLIBS)
