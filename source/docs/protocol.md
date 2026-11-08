# Wire protocol (version 45)

Written from scratch for Skyward Sword Together. The *design* follows Twili-Together's relay model
(rooms, teams, presence scoped to the same area, team world state cached for catch-up). Twili-Together
has no license, so none of its code is reused.

## Transport

TCP carries all session control, scene changes, flags, rewards, damage and recovery. The built-in
host also supports optional UDP for POSE and MOTION, with automatic TCP fallback. The standalone
Node relay is TCP-only. Every TCP frame is:

```
u32 length (little endian, counts type + body, 1 .. 65536)
u8  type
... body
```

Strings are `u8 length` + UTF-8 bytes (max 255). All integers and floats are little endian.

When the relay forwards a frame from a client it inserts the sender's `u16 player_id` directly after
`type`. Frames a client sends never carry its own id.

## Messages

| Type | Name | Direction | Body (client to relay) | Routing |
| --- | --- | --- | --- | --- |
| 0x01 | HELLO | c→s | `u16 proto`, `str name`, `str room`, `str team`, `u8[16] session` | handshake |
| 0x02 | WELCOME | s→c | `u16 your_id`, `u8 count`, then per player `u16 id, str name, str team, str scene, u8 room` | reply |
| 0x03 | PLAYER_JOIN | s→c | `u16 id, str name, str team` | room |
| 0x04 | PLAYER_LEAVE | s→c | `u16 id` | room |
| 0x05 | ERROR | s→c | `str message` (then the relay closes) | reply |
| 0x06 | PING | c→s | `u32 token` | answered with PONG |
| 0x07 | PONG | s→c | `u32 token` | reply |
| 0x08 | UDP_OFFER | s→c | `u8[16] random_token` | private per TCP connection, built-in host only |
| 0x09 | UDP_READY | c→s | `u32 acknowledged_probe`, or zero to fall back | private; host verifies recent probe |
| 0x0a | UDP_SCENE | c→s / s→c | request: `u32 local_cookie`; reply: `u16 source_id, u32 local_cookie, u32 host_scene_epoch` | reliable scene barrier; reply broadcast |
| 0x10 | SCENE | c→s→c | `str stage`, `u8 room`, `u8 layer`, `u8 entrance` | room (everyone, for map markers) |
| 0x11 | POSE | c→s→c | see below | room, same stage only, droppable |
| 0x20 | FLAG | c→s→c | `u8 kind`, `u16 index`, `u8 value` | team, cached |
| 0x21 | WORLD_REQUEST | c→s | empty, or nonzero `u32 request_token` | host replays the team cache; tagged requests end with WORLD_DONE |
| 0x22 | RESYNC | c→s→c | none | teammates announce fresh admitted progress and enemy health; forwarded body is sender ID |
| 0x23 | CHEST | c→s→c | `u16 index`, `u16 item` | team, immutable location reward, cached |
| 0x24 | WORLD_DONE | s→c | `u32 request_token` (no sender ID) | host-only completion marker after the requested cache |
| 0x30 | ENEMY | c→s→c | see below | team, same stage/room/layer; live only, not cached |
| 0x31 | MOTION | c→s→c | see below | elected area owner → same-area teammates; live/droppable, not cached |

### Optional UDP movement

Clients use the same resolved address and numeric port as their TCP connection. The built-in host
binds IPv4 UDP alongside TCP (default 7623); failure to bind UDP leaves TCP hosting operational.
`sst_use_udp = 1` enables the movement channel and is the default game setting. Zero forces TCP.
All four players must use protocol 45; mixed UDP/TCP players are supported.

Each datagram is at most 192 bytes, little endian:

```
u8[4] magic = "SSU1"
u8[16] connection_token
u32 sequence
u8 type                  (0 = probe/ack, 0x11 = pose, 0x31 = movement)
u32 crc32                (IEEE CRC of the complete packet with these four bytes omitted)
... body
```

Probe/ack bodies contain `u32 nonce`. Client movement bodies contain `u32 source_scene_epoch`
then the normal POSE/MOTION body. Host movement bodies contain `u16 source_id, u32 source_scene_epoch,
u32 recipient_scene_epoch` then that body. No reliable gameplay event is accepted over UDP.

The OS supplies a fresh 128-bit token for each accepted TCP connection; UDP remains disabled if
secure randomness fails. A valid probe must originate from the TCP peer's IPv4 address. It establishes
the UDP endpoint; only authenticated probes may change its port. The host answers a same-size
challenge. The client confirms receipt over TCP before the host switches downstream movement to UDP.
This uses bearer tokens, endpoint checks and corruption detection; it is **not encrypted** and the
CRC is not a cryptographic MAC. Room/team names are not authentication.

Each SCENE invalidates the sender's host-assigned scene epoch. The client follows SCENE with
UDP_SCENE; the reply reliably establishes its new epoch and echoes its local cookie. The client
discards UDP while its own scene is pending and rejects mismatched sender/recipient epochs. JOIN
clears the remote incarnation. Delayed packets from a previous scene, same-area return, replaced
connection or reconnect cannot re-enter the current movement stream. Packet sequence numbers
reject duplicates and reordered old samples. Probe/ack replay counters are separate from movement,
so later movement cannot invalidate a delayed heartbeat.

Probes run every 500 ms. A missing valid reply for about 1.5 seconds switches the client to TCP;
the host also checks UDP receive freshness and the client's reliable UDP_READY status. Probing
continues, allowing automatic recovery. ACKs from the last four probes are eligible only while
their send time is less than 1.5 seconds old, allowing round trips longer than one probe interval.
TCP remains the connection lifetime/heartbeat authority, so UDP traffic cannot keep a dead TCP
session alive.

Both movement directions use small token buckets: initially about 6 kB/s per peer, up to 24 kB/s,
with a quarter-second burst. Missing probe replies halve the client's budget (floor 1.5 kB/s);
successful probes grow it by 500 B/s. The host adapts its recipient budget from reliable feedback.
The client retains at most one pose and 256 enemy samples, coalesces by actor and gives the pose
priority, limits poses to 10 Hz at the lowest budgets and rotates the enemy send order so later
actors are not starved. Fresh unsent updates remain in the fixed queue until budget is available; each actor retains only its latest sample, and samples expire after 500 ms without observation. Age comparison tolerates a producer timestamp just newer than the network poll and 32-bit clock wrap. The cursor advances after successful sends, independently of producer timing and empty network polls. Excess distinct actors beyond the bridge limit drop instead of accumulating or spilling into TCP. The host processes
at most 64 datagrams per poll. This is a conservative initial pacing mechanism; broader internet
congestion/quality tuning is still pending. UDP adds no NAT traversal, relay service or host migration.

### POSE body (36 bytes)

PLAYER_JOIN starts a fresh presence even when its ID is already listed: a live socket replacement
can preserve that ID without a preceding PLAYER_LEAVE. Receivers clear the previous scene/pose and
interpolation history. Pose, enemy-damage and enemy-movement streams use the new presence; their
counters may restart. Receivers tag those streams with a local generation, including damage held
during native menus. This generation is not serialized and does not change the wire payload.

```
u32 tick
f32 x, y, z
i16 rot_x, rot_y, rot_z
u16 anim_id
f32 anim_frame
u16 action
u16 health        (quarter hearts)
u16 max_health
u8  equip_flags
u8  reserved
```

The HD reader populates `anim_id` with the verified native 443-entry player-animation table index
(0–442); 65535 denotes an unavailable controller. `anim_frame` is the native clip frame. The
renderer recognizes observed locomotion clips 20/21/25/26/27 and finite sword clips 191-210
in native sword action 109. Directional upper-body aiming is not reconstructed from one clip.
Native clip duration is local-only metadata for interpolation. Known locomotion loops interpolate
across their boundary; finite sword restarts remain discrete. No wire fields or protocol change.

`equip_flags` bit 7 marks a boss encounter; bit 6 marks an active event/cutscene or native map/inventory menu; bit 5 advertises
regular-enemy movement sharing. Recovery checks
these and a recent normal action before choosing a teammate's entrance. WORLD_REQUEST also returns
fresh cached poses from teammates in other areas, solely to validate recovery (under one second old
at the host). Normal pose streaming in the embedded host remains restricted to the same area.

### FLAG kinds

Kinds used by the game bridge: `0` story logical ID, `1` item logical ID and `2` scene bit.
Kind `3` is reserved and ignored by the game bridge: dungeon inventory requires a reward receipt.
Scene indices are `table * 128 + word * 16 + bit` for 26 tables.
The host OR-caches values by room/team/kind/index. The bridge accepts only value 1 and its policy
allowlist. Story/item definitions must actually be one bit in the HD runtime. Counters must never be
OR-merged as raw bytes. See [world-sync.md](world-sync.md) for admitted state and limits.

### CHEST rewards

`index` identifies native collection or completion receipts:

- TBox: `save_table * 32 + chest_flag` (0–831, unchanged).
- Permanent scene receipt: `832 + save_table * 128 + scene_flag` (832–4159).
- Reviewed NPC quest receipt: `4160 + logical_story_flag` (4160–5361; only the fifteen crystal receipt routes, Potion Lady’s bottle, Batreaux’s final wallet receipt and four trial identities listed below are admitted).

Only scene flags 0–127 are durable here; temporary/zone flags are excluded. A TBox with both
receipts uses its TBox identity. Wardrobes use their permanent scene receipt. `item` is the native item ID.
Supported rewards are rupees (2, 3, 4, 32, 33), materials (161–176), small keys (1), heart pieces
(94), native dungeon maps (50), boss keys (25, 26, 27, 29, 30, 31), Eldin key pieces (28),
reviewed tools (19, 20, 49, 52, 53, 71, 75, 76, 77, 90, 91, 92, 105, 137, 140), passive traversal gear (56, 68, 99, 138), wallet tiers (108–111), the 15 permanent single Gratitude Crystals (48), all thirteen reviewed NPC five-crystal rewards (35), and empty bottles (153) and filled Revitalizing Potion bottles (126) from container identities below 4160.
Small keys are valid only in source tables 7, 11, 12, 15, 17, 18 and 20. Their source table stays part
of the receipt identity; receivers defer the grant until that same table is loaded by both the chest
and dungeon managers. Keys at the carrying limit also wait without consuming their receipt. Both hosts validate the
payload and retain the first reward for each location. Duplicate or conflicting submissions cannot
replace or OR that reward. Forwarded bodies prepend the sender ID; cached replies use sender 0.

The 15 reviewed active tool IDs above are admitted in both container receipt ranges. Receivers
validate native boolean ownership and prerequisite classification, use the native item-ID setter,
unlock story 58 for the item wheel and record the personal receipt together on the guest thread.
Already-owned tools consume new receipts without clearing stronger upgrades or changing spent
ammo, health, currency or equipped selection. Generic item FLAG IDs 19/49/53/92/137 are ignored;
the reviewed original gift IDs are listed below. Saved vanilla catalogue receipts recover across
areas. Source events/tutorials remain local; boss guards defer world mutations.

First Bow/Slingshot acquisition and genuinely new upgrades supply native base ammunition up to
20, preserving a higher existing seven-bit count. Owned or higher-tier ownership suppresses
refills during replay and lower-tier repair. Bows use counter slot 1/logical 498; Slingshot and
Scattershot use slot 4/logical 493. Bomb Bag preserves the base bomb counter (slot 2/logical 499)
on first ownership and replay. Pouch ammunition is unchanged. Other ammo/pouch acquisitions
are not admitted by this adapter.

Heart-piece rewards use native counters 8 and 9, adding health capacity only when the personal
remainder wraps to zero, then invoking native capped healing. The native total-piece and health
caps apply. Boss heart containers (93) remain unsupported; neither heart type is an ownership-only
FLAG. Protocol 15 added Clawshots and native-menu safety. Protocol 16 adds passive traversal gear receipts and reserves six further Cawlin/Pipit reward follow-up flags. Protocol 17 adds Bomb Bag receipts; ordinary ownership FLAG 92 no longer grants that item independently. Protocol 18 adds Nets and equipment upgrades, with native prerequisite repair and first-ammo handling. Protocol 19 adds Bertie’s pack and reserves his introductory story 386 as an auxiliary receipt. Protocol 20 pairs three Silent Realm completions with their traversal rewards and holds incoming world changes during local trial gameplay. Protocol 21 adds Hylia’s completion/Stone pair, reserves raw Stone ownership and gates Sky Keep opening on the personal reward. Protocol 22 adds Kina and both Item Check endings, reserves auxiliary story 635 and treats the endings as one personal crystal pack. Protocol 23 adds Beedle and Owlan, reserves Owlan’s follow-up story 481, coalesces repeated flag snapshots and requests bounded recovery after incoming queue overflow. Earlier hosts reject the new rewards; all players must use the same protocol.

Dungeon maps are admitted only in tables 11, 12, 14, 15, 17, 18 and 20. Boss-key sources are:

| Item | Dungeon table |
| --- | --- |
| 25 (Ancient Cistern) | 12 |
| 26 (Fire Sanctuary) | 15 |
| 27 (Sandship) | 18 |
| 29 (Skyview) | 11 |
| 30 (Earth Temple) | 14 |
| 31 (Lanayru Mining Facility) | 17 |

These rewards wait until the native dungeon and chest managers identify their source table; scene
receipts also require their scene table loaded. The bridge validates native item classification and
dungeon flag definitions, calls the native item-ID flag setter, mirrors only map/boss-key ownership
to the matching save table and records the receipt. It never retargets a live flag manager. Native
map item 50 sets dungeon logical flag 2 (word 0 bit 1); boss keys set flag 12 (word 0 bit 7).
Item 51 is a different unused item and remains unsupported. Generic kind-3 flags cannot bypass
this transaction or restore a consumed item. Acquisition tutorials/cutscenes stay local.

Key pieces are valid only at five vanilla F200 digging spots: scene table 4, flags
8, 9, 10, 12 and 64 (wire indices 1352, 1353, 1354, 1356 and 1408). These receipts
are reserved from ordinary scene FLAG messages even before their rewards are known. A native
Soil actor with subtype 4 and its completed scene receipt identifies a collected piece.
Receivers increment native counter 5 (ItemMgr logical 505), capped at five, and record their
personal receipt even outside Eldin. Native first-ownership is set. Completing the key sets story
120 through its native setter; ordinary story-120 updates wait until the personal counter is five.
Active remote digging spots are deleted in the same guest-thread operation so their scene flag
cannot trigger another native pickup. Reloading shows the native collected hole. Key-piece
presentations, quest dialogue and the entrance interaction are not replayed for teammates.

Freestanding heart pieces use the same item-94 transaction and native health effects. The five
vanilla receipts are Faron Woods table 1 flags 33/68 (indices 993/1028), Eldin table 4 flag 102
(1446), Volcano Summit table 5 flag 104 (1576) and Skyview table 11 flag 103 (2343). These scene
flags are reserved at both hosts and in the bridge, even before visiting their areas. Their saved
receipts can announce a collected reward on a new process without spawning the original actor.
Actor metadata uses placed Item profile 0x281, subtype 0, permanent scene flag from params bits
10–17 and matching fixed native item ID from bits 0–8. Dynamic/presentation actors and temporary
receipts are excluded. Metadata persists in memory after native deletion and conflicts quarantine
the receipt. Active matching pickups are removed synchronously after a remote grant; subsequent
loads skip them through their native collected flag. Other placed pickup rewards remain personal.

Single Gratitude Crystal item 48 is admitted only at the 15 vanilla permanent scene receipts:
`934, 935, 936, 938, 940, 941, 942, 943, 944, 945, 946, 947, 3620, 3636, 3639`.
Both hosts and the bridge reserve them from ordinary switches before visiting the area. Receivers
call native counter 38 (HD logical item 502, cap 127), set first ownership and mark the personal
receipt together. They can receive crystals during the day and outside the source area; local
night-time spawning and story prerequisites still govern ordinary collection. The known receipts
recover already collected rewards after a fresh process start. Active received pickups are deleted
in the same tick; reloads skip them through native flags.

NPC five-crystal item 35 is admitted only for these personal story receipts:

| NPC | Story flag | Wire index | Reward |
| --- | --- | --- | --- |
| Wryna | 326 | 4486 | 5 crystals |
| Sparrot | 373 | 4533 | 5 crystals |
| Orielle | 383 | 4543 | 5 crystals |
| Parrow | 384 | 4544 | 5 crystals; conditional empty bottle and auxiliary 382 |
| Bertie | 387 | 4547 | 5 crystals, auxiliary story 386 |
| Fledge | 394 | 4554 | 5 crystals |
| Dodoh | 398 | 4558 | 5 crystals |
| Mallara | 575 | 4735 | 5 crystals, 20 rupees, auxiliary story 361 |
| Kina | 472 | 4632 | 5 crystals |
| Beedle | 477 | 4637 | 5 crystals; native cage-return/discount eligibility |
| Owlan | 482 | 4642 | 5 crystals, auxiliary story 481 |
| Cawlin letter: Toilet Hand | 553 | 4713 | 5 crystals; shares one pack with Pipit |
| Cawlin letter: Pipit | 561 | 4721 | 5 crystals; follows 558/559/557/560/663/554 |
| Peatrice | 636 | 4796 | 5 crystals, auxiliary story 635 |
| Peater | 639 | 4799 | Alternative ending of the same Item Check reward |

The bridge waits for the local reward event to finish before discovering completed receipts.
Receivers use native item setters and capped counters, then set the primary and auxiliary story
receipts together. Each receipt grants once, including saved-receipt discovery after a restart. A completion bit retained in either live or saved flags proves delivery: missing primary/auxiliary flag copies are repaired through native setters without adding crystals or rupees again.
Peatrice/Peater receipts are mutually exclusive reward outcomes. If either ending already exists in the receiver's live or saved flags, the other ending grants no crystals and does not change their choice. Repairs use the existing ending's own primary/auxiliary flags. A fresh receiver adopts the first ending applied; receiving both grants one pack. Source discovery announces only the ending actually recorded locally. Parrow’s final pack uses the conditional native pouch transaction described below.

Cawlin’s letter endings share one personal pack. A receiver with either actual receipt
553/561 already recorded gets no second reward and keeps that ending. Otherwise, the
bridge waits for one local hand-over: story 552 for the Toilet Hand or 555 for Karane.
It translates either incoming ending into that local route, then grants five crystals and
writes its receipt. Pipit’s receipt 561 also sets/repairs 558,559,557,560,663,554.
Story 558 alone is a sleep cleanup flag as well, so it is never accepted as a reward receipt.
No choice or both choices holds delivery; the Multiplayer page distinguishes these states.
Hand-over/ownership/branch flags stay personal. Cached opposite endings count as one waiting
reward. These rules are protocol 29; older peers are rejected at connection.

Beedle’s discount-used flag 478 stays personal, whether already set or still clear. Owlan completion repairs follow-up 481 without a second grant. Mallara's rupees respect the receiver's wallet cap. The 41 reviewed NPC receipt/auxiliary/choice flags
`35,326,360,361,373,382,383,384,386,387,394,395,398,472,473,477,478,481,482,547,548,549,550,551,552,553,554,555,556,557,558,559,560,561,575,635,636,639,663,664,771`
are reserved from generic story FLAG handling in both hosts and the bridge. Potion Lady’s bottle completion 35 and Parrow’s loan/full-pouch flags 382/395 cannot bypass their transactions through ordinary flags. Batreaux’s separate Cursed Medal chest uses its native container receipt; other unsupported NPC acquisitions remain personal.

Reviewed bottle containers use native items 153 (empty) and 126 (Revitalizing Potion). Both require an owned Adventure Pouch, normal inventory mode and
an empty slot among the four base slots plus up to four purchased expansions. The native pouch
setter initializes quantity one. Other slots, filled bottles and their contents remain unchanged.
A full/locked pouch leaves the whole reward pending; menus/events and restricted inventory modes
also defer delivery. After checked insertion the bridge records the personal container receipt.
Unexpected setter results quarantine the reward rather than retrying it. Raw item FLAG 126/153 is rejected
by both hosts. The Sealed Temple chest (320/item 126) and Fire Sanctuary chest (486/item 153) recover from saved receipts without visits. Other bottle containers still require original-container metadata after restarting. Consuming a potion changes native pouch contents; its retained chest receipt prevents a refill during resync.

Parrow receipt 384 grants five capped crystals. A prior bottle loan in live or saved story 382
suppresses bottle creation, even with a full pouch. Otherwise the entire bundle waits for a free
slot, inserts one native empty bottle, records 382, then grants crystals and records 384.
Existing 384 repairs 382 without granting again. The personal full-pouch branch 395 is preserved.
This preserves any existing filled bottle rather than consuming or replacing it. Older protocol
saves may already contain independently shared 382; the adapter conservatively trusts that receipt.
Potion Lady’s first free bottle uses wire identity 4195 (4160 + story 35), with item 153 only. Native flow gives the bottle before setting 35. A full/locked pouch holds both insertion and completion; an existing live or saved 35 repairs its other copy without another bottle, including after later storage/consumption. No crystal counter changes. Other unsupported bottle/pouch gifts remain personal.

Receivers use their personal native opened-chest bits as receipts. Applying a new reward calls the
native counter or dungeon ownership setter and writes the receipt together on the guest thread, after a save backup.
An already opened chest never grants again, including after a resync or reconnect. This message does
not contain raw inventory counters or opened bits that could hide an ungranted reward. TBox,
permanent scene-flag chests, permanent-receipt wardrobes and the five key-piece digging spots can enter this cache. Unsupported items
and repeatable/temporary receipts remain personal. Reward metadata is kept for the running session
and re-announced on recovery; it is not an independently persisted inventory ledger.

Receipt flags are protected from ordinary SCENE FLAG application. Native actor metadata marks
all recognized container receipt flags, including unsupported rewards. Incoming scene CHEST
identities also protect their flag before world application. Hosts retire a matching cached switch
when a scene reward arrives and suppress subsequent switch packets for it. Generic scene updates
wait until their target scene table is loaded, allowing local container metadata to be checked.
For boxes with both receipt types, the auxiliary scene switch is mirrored only after the TBox
receipt proves reward delivery. Wardrobes stay interactive and use their native empty/collected
behavior after a remote grant.

### ENEMY body

```
str stage
u8 room, layer
u64 key             (params1 << 32 | profile << 16 | placed_object_id)
u32 sequence        (monotonic within the sender's connection generation)
u16 profile
u16 damage
u16 health
u8 mode             (0 = damage event, 1 = health snapshot; snapshot damage must be zero)
```

Forwarded packets prepend the sender ID. Receivers require an active matching actor in the exact
area, reject duplicates/old sequence numbers and never heal from a snapshot. A room reload resets
actor records; native remote writes update local baselines to avoid sending the damage back. During native menus/dialogue, up to 512 matching same-visit events wait in arrival order. Each is bound to the actor instance that existed on arrival. Offline, disabled sharing, loading, room/layer changes, bosses and trials clear held events. Overflow requests fresh team state after resuming. Re-enabling enemy sharing requests current health even with progress sharing disabled. Boss
profiles are rejected independently of packet routing. RESYNC requests are throttled to one per
second per sender. Entering a new area requests fresh state after SCENE has been published.

### MOTION body

```
str stage
u8 room, layer
u64 key             (same placed-actor identity as ENEMY)
u32 sequence
u16 profile
f32 pos_x, pos_y, pos_z
i16 rot_x, rot_y, rot_z
f32 velocity_x, velocity_y, velocity_z
```

The suffix after stage/room/layer is 44 bytes. The lowest eligible player ID in the exact area owns
the stream. Eligible players have a pose under 1500 ms old, positive health and pose flags with bit 5
set and bits 6/7 clear. Both relays reject movement from other senders. Clients independently verify
ownership, profile, identity, finite bounded transforms and sequence order. The native bridge only
writes active, living, supported regular actors; boss encounters and local events block application.

Owners send at approximately 10 Hz. Followers interpolate position/facing over 100 ms, copy velocity
and stop applying a target after 500 ms without fresh data. Actor reloads, respawns, ownership changes
and connection generations discard interpolation history. Queues coalesce pending movement by
sender/area/actor, and congested movement streams drop updates while reliable world/damage messages
retain their existing delivery behavior. A motion packet never changes health or creates an actor.

## Rules

- The relay rejects a HELLO with a different `proto` (ERROR, close).
- A reconnect with the same `session` replaces the old connection and keeps the player id.
- Streamed POSE and ENEMY in the embedded host require matching stage, room and layer. Requested
  fresh pose snapshots may cross areas for recovery. The legacy Node relay streams poses by stage.
- Empty room or team names are valid: everyone with an empty room shares the public room.


The reviewed vanilla collection catalogue is `docs/reward-catalogue.tsv` (125 identities, including six NPC scene receipts). Run
`scripts/update-reward-catalogue.py --check` to validate its generated C and relay tables.
Only unambiguous supported fixed rewards are included; native TBox metadata uses all six receipt
bits from rotation Z, with values 32–63 falling back to permanent scene receipts. Goddess item IDs
are decoded using the verified native variant table. Three conflicting container identities (29,
483, 692) are excluded from save-only discovery. The bridge still requires an actual personal
collected receipt to announce a catalogue entry; seeing a known location never grants it.
Catalogue scene-only receipts 864/1550/2070/3157 are reserved before an area visit. The catalogue
is vanilla-specific and does not implement arbitrary randomized rewards.


Native pause menus use the existing pose safety bit 6, so they hand off enemy movement ownership
and cannot serve as final catch-up destinations. Pending local recovery refreshes teammate state
at most once per second until it completes or reaches its original ten-second deadline.

Client diagnostics reuse PING/PONG: four-byte heartbeat tokens are echoed unchanged. Only a
four-byte PONG matching the current pending token supplies a round-trip sample. Malformed and
unmatched replies cannot set it. The timestamp includes time in the local mod send queue and
remote processing. Measurements reset on connection and are not per-peer mesh latencies.


Protocol 18 permits original ownership gifts for 20/52/71/75/76/77/90/91/105/140. Base Bow,
Beetle, Bellows, Whip and Bomb Bag still require a reward receipt. Gifts wait for the local
acquisition event to finish before publication. Upgrade receivers validate and grant the
native prerequisite chain: 53→75→76→77, 19→90→91, 52→105 and 71→140. Each new ranged tier
fills its base counter up to 20, retaining a higher existing count. An owned tier, or proof of
an already-owned higher tier, suppresses the refill. This also protects out-of-order lower-tier
recovery. Both gift replay and owned-upgrade receipts can repair missing prerequisites and wheel
58. Equipped selection and stronger ownership are preserved.


## Silent Realm completion receipts (protocol 21)

| Trial | Completion story | Wire index | Reward item |
| --- | --- | --- | --- |
| Farore | 206 | 4366 | 68 (Water Dragon's Scale) |
| Din | 207 | 4367 | 138 (Fireshield Earrings) |
| Nayru | 208 | 4368 | 20 (Clawshots) |
| Hylia | 209 | 4369 | 180 (Stone of Trials) |

Each identity admits exactly its listed item; the item cannot be substituted, including with
NPC crystal item 35. Both hosts reserve story 206–210 from ordinary FLAG forwarding. Raw item FLAG 180 is also reserved: Stone ownership travels only with receipt 4369. Story 210 remains personal; the native Hylia reward did not set it.

The bridge discovers completed live or saved receipts after normal gameplay resumes. Receivers
validate the native story definition, item class and ownership path, grant the permanent item
(and wheel 58 for Clawshots), then persist completion. A retained completion receipt also repairs
missing ownership, the wheel and live/saved completion copies. These four grants do not alter
ammunition, rupees, health or pouch state. Original traversal gifts remain valid independently: owning a traversal item alone does not claim completion of its Silent Realm. The Stone has no standalone FLAG grant. Sky Keep opening story 22 waits for personal completion 209 and Stone ownership; a cached opening cannot skip that reward.

S000/S100/S200/S300 trial simulations stay local. Incoming progress and rewards are retained until
the player returns; trial tears, timers, Guardians and trial switches are not published as ordinary
world state. Recovery is disabled inside trials, and those areas are unavailable catch-up targets.
The existing pose event bit 6 also marks the trial as ineligible for enemy movement ownership.

### Bounded client queues (protocol 23)

Incoming and offline FLAG messages coalesce by kind/index with OR-merged values and latest-source
attribution. The queues hold 4,096 unique flags each; their fixed hash indices add 32 KiB total.
FLAG bodies with trailing or missing bytes are rejected. If an incoming flag or damage queue
overflows, a drained client requests WORLD_REQUEST and RESYNC, at most once per second. A pending
refresh survives the cooldown and outgoing backpressure. Motion remains droppable. The local
connection diagnostics count automatic backlog refreshes; no additional wire field is needed.


### Batreaux and wallet capacity (protocol 28)

Batreaux's scene-table-0 receipts 70–75 use wire identities 902–907, paired strictly
with items 108,94,109,33,110,33. They represent the 5/10/30/40/50/70-crystal tiers.
Wire 907 grants two native 300-rupee increments under one personal receipt; 905 grants
one. The existing heart-piece transaction handles 903, including a new heart and native
healing when it completes four pieces. All six recover from saved receipts without visits.
The final 80-crystal reward uses story 360/wire 4520 with item 111 only, so human
transformation follows verified Tycoon Wallet delivery. Other item pairs are rejected.

Wallets use checked native class-3 ownership setters. Higher tiers satisfy lower rewards
without a downgrade; existing extra-wallet counter 508 and held rupees are preserved.
Missing wallet ownership can be restored from an already completed collection receipt
without granting money or health again. Raw wallet FLAG IDs 108–111, story 360 and
scene bits 70–75 are reserved in both hosts and the bridge.

Native capacity can lag an ownership write. Wallet receipts wait until the actual capacity
matches the live wallet/extra-wallet flags. All incoming currency, including mixed NPC
crystal/rupee bundles, also waits for that capacity so a late-join burst cannot clip money
against an older smaller wallet. Extra wallets, NPC dialogue presentation and other unsupported rewards remain personal.


### Medals and spare ammo pouches (protocol 29)

Container CHEST transactions admit Bug/Heart/Rupee/Treasure/Potion/Cursed/Life Medals
(items 70,100–104,114) and the three Seed Satchel, Quiver and spare Bomb Bag tiers
(items 128–136). No standalone ownership FLAG can grant these items in either host.
Bottles retain their previous transaction rules. Shops, upgrades and loose pickups
without a supported container receipt are not shared by these additions.

The bridge checks the original item descriptor and calls the native normal-pouch setter
with its default quantity. Medals and bottles use one; seed satchels use 10/20/30;
quivers and spare bomb bags use 5/10/15 each. The entire eight-word pouch is checked:
only the chosen empty unlocked slot may change, with exactly the reviewed item/quantity.
A native result of zero with no inventory changes leaves the reward pending. Any other
unexpected result quarantines the reward before a repeat grant. No writes target Item
Check, temporary inventories, existing pouch slots or base weapon ammo counters.

Full/locked pouches wait for room; maps/events, restricted inventories, trials and boss
encounters retain their existing application guards. Medals retain native effects,
including Life Medal health capacity and Cursed Medal restrictions. Personal container
receipts survive storage, ammo consumption, resync and area reload, so rewards are not
recreated or refilled. Active copies of received supported containers are removed through
the existing container handling.

Saved recovery adds chest 5/item104 (Batreaux) and nine unambiguous Goddess Chest
identities 674,675,681,682,683,684,689,691,693. Shared identity 692 has different vanilla
rewards in F020/F023 (Life Medal/Small Quiver), so it is deliberately excluded from saved
recovery and still requires local container metadata. This does not add shop or upgrade
purchase synchronization.


### Pouch expansion capacity and repair (protocol 30)

Container CHEST transactions admit item 113, with the original Goddess expansion chest at
wire index672 (TBox table21/bit0). Original F020/F023 alternatives decode to the same item,
so this identity joins saved recovery. The general raw item FLAG113 is reserved in both
hosts and excluded by the client policy. Owning the boolean alone is not an expansion.

The bridge validates the native class3 descriptor, logical counter490 and its native
getter/adder/setter functions, then invokes the native checked counter adder. It preserves
all eight pouch words and persists the verified ownership/capacity fields and personal
receipt together. This needs a normal unlocked pouch, but no free slot. Menus/events,
temporary inventories, trials and boss guards retain their existing application rules.

Counter490 can represent seven in native code, while the usable pouch has four base slots
and four expansions. Incoming rewards and receipt recovery therefore stop at four extra
slots, including when a player already bought upgrades personally. Existing out-of-range
capacity is held unchanged. This code does not copy a teammate's capacity or shop purchases.

For repairs, the target preserves the greater intact saved/live expansion count and the
minimum proved by completed known personal expansion receipts, capped at four. A new
unopened reward adds one to the preserved personal count before that cap. A completed
receipt repairs missing ownership/capacity without another additive grant. Unprovable lost
shop purchases cannot be inferred if both capacity copies are gone. Stored items and spent
ammo retain their independent container receipts and are not recreated during capacity repair.

A replay can include both an expansion and a reward for its new slot. The capacity receipt
commits after native validation; pouch insertions retain protocol29's unchanged-refusal
retry until the native inventory reader sees the new capacity. The loaded original Goddess
chest uses the existing native deletion path after remote delivery, including at max capacity.

### Beedle purchase receipts (protocol 31)

CHEST identities 4973=(4160+813) and 4974=(4160+814) accept only heart piece94 and
Life Medal114 respectively. They use vanilla purchase completion, never ownership
alone. Raw story813/814 FLAG messages are rejected by both host implementations.
Both purchases remain pending inside F002r; leave the shop to receive them, then
re-enter to rebuild stock. Currency remains personal. The medal uses checked native
pouch insertion; the heart uses the existing checked remainder/total/capacity/healing
transaction with a copy-on-write story receipt. Either live or saved completion proves
delivery and repairs the other copy without another inventory grant. Protocol30 peers
must update. Bug Medal, pouch shop counters707 and extra-wallet counter708 have not
been added by this batch.

### Ordered shop capacity purchases (protocol32)

Six new CHEST identities extend the upper bound from5362 to5368. Indices5362–5364
pair only with pouch expansion113, and5365–5367 pair only with extra wallet199.
Their ordinals1–3 use original story counters707/708, each a two-bit field in word119
(shifts9/11). Generic story707/708 and item490/508 FLAG messages are excluded.

A completed count proves the corresponding prefix. Either saved or live proof repairs
the other copy. A new ordinal applies only after all earlier purchases are complete;
missing predecessors stay pending with a request-team-state reason. Both families
hold new delivery while F002r stock actors are active. Saved-only stock repairs also
wait until leaving; live-to-saved repair does not change active stock.

Pouch repair combines completed shop purchases with completed known expansion-container
receipts and preserves greater intact personal capacity, capped at four extra slots.
Extra-wallet repair preserves the larger saved/live count and purchase proof, capped at
three, using the original native508 counter adder. Native wallet limit caching may lag
one frame; currency transactions keep waiting until it commits. Extra wallets preserve
rupees, wallet tiers and pouch words and require no pouch space or unlock. Temporary
inventory modes hold both delivery and repair. Saved proof never grants another upgrade
merely because a packet is replayed. Peers on protocol31 must update.

Deferred F002r receipt repairs retain observed completed counts and boolean shop
receipts in the current bridge session. The native frame can overwrite a saved
shadow from incomplete live flags while the stock actors are active. The pending
proof survives that writeback until both native copies have been repaired after
exit, and clears on game reset. Proof observation runs on each input frame; native
mutations retain their normal30Hz safety gates. New unseen purchases still require
their own ordered receipt and do not use this local repair proof as a delivery claim.

The isolated native test uses a local `shop-proof` recovery-mailbox command only
when `LARECOMP_DEBUG_DATA` is enabled. It clears the four reviewed live shop fields
and seeds completed saved shadows immediately before the normal observer. This
aligns an otherwise transient fault with the input hook; the test then lets real
native writeback run and verifies repair after exit without any inventory grant.
The command is not a network message or a multiplayer-menu action.


### Unique Bug Medal and Net stock (protocol33)

CHEST5368 pairs only with item70, the unique Bug Medal. Both hosts cache that fixed
pair and reject other combinations and raw item70 FLAGs. The recipient's native
acquired flag70 proves delivery even after storage. A valid carried legacy medal or
completed typed medal container can repair that flag without another inventory item.
A second medal container marks its personal receipt without inserting another Bug Medal.
This uniqueness rule does not apply to Life Medal114.

Delivery and missing acquired-flag repairs wait inside Beedle's F002r shop. Observed
saved-only completion proof is retained until stock can safely refresh after exit.
Incoming Bug Net71 and Big Bug Net140 also wait there when base Net ownership is
missing, whether received as raw tool flags or typed container rewards. These reliable
rewards remain on TCP. The Multiplayer tab includes the shop hold in its pending count.
All participants must use protocol45. Boss/miniboss exclusions remain unchanged.


### First shield acquisitions (protocol 34)

Fixed CHEST identities 5369–5378 pair exactly with shield items 116–125. Both hosts
reject other pairs and raw shield acquired-item FLAGs. Native acquired flag N proves
the first personal acquisition of shield N, regardless of storage or subsequent loss.
It is not a receipt for each repeat purchase or recipe. A first missing acquisition
inserts one personal shield at native starting durability and records the acquired flag.
A carried valid legacy shield or completed typed container can repair missing ownership
without changing durability or adding another shield. Generic container receipts remain
independent and can each deliver their shield once even if that type was acquired before.

New shield grants and missing acquired-flag repair wait inside Bazaar F004r, retaining
observed saved-only proof across native writeback. Exit to apply; map/events, full-pouch,
temporary inventory and boss guards still apply. No recipe or currency transaction is
replayed. All participants must use protocol 45; reliable rewards remain on TCP.


### First spare-ammo acquisitions (protocol 35)

CHEST identities 5379–5387 pair exactly with pouch items 128–136: all three seed
satchel, quiver and bomb-bag tiers. Both hosts reject mismatched pairs and raw acquired
FLAGs. Native acquired flag N proves the first personal acquisition of bag N, including
after it is emptied, stored or lost. A new first acquisition inserts one personal bag
at native starting quantity and records ownership. It is not a receipt for every
repeat recipe or purchase; no main-weapon ammunition or material cost is copied.

A carried empty bag remains ownership proof. Valid quantity is zero through native
maximum, distinct from shields that require nonzero durability. Completed typed ammo
containers can repair missing acquired flags without another insertion. Generic
containers retain independent opened receipts and can each deliver their bag once.
All such delivery and missing-flag repair wait in Bazaar F004r until exit. Observed
saved-only proof uses a 32-bit mask shared with shield repair. Map/events, locked/full
pouch, temporary inventory and boss guards continue to apply. Everyone must use
protocol 45. All rewards and recovery remain on reliable TCP.


### Checked sword progression (protocol36)

Sword tier order is Practice10, Goddess11, Goddess Longsword12, Goddess White9,
Master13, True Master14. Both hosts admit these typed rewards for container identities
below4160. The native bridge validates one-bit definitions and class3 descriptors,
then uses ITEM_SET_FLAG for the strongest of incoming, live, saved and retained proof.
Weaker receipts complete without adding lower ownership bits or downgrading equipment.
Temporary inventory blocks source announcement and delivery. Map/menu holds application.
Saved-only stronger proof survives native writeback until safe repair; opened sword
containers repair missing ownership. Pouch contents, non-sword item words, currency,
health and capacity are checked unchanged. No additional story/cutscene prerequisites
are synthesized. Sailcloth15 and Harp16 are not swords. The original Sparring Hall
Practice Sword receipt is2/item10; the reviewed catalogue now contains125 identities.
Equipment is verified after area reload; use Resync and reload area if the visible sword
has not refreshed. All participants must use protocol45. Rewards/recovery use TCP.


### Host-acknowledged recovery (protocol37)

WORLD_REQUEST0x21 accepts an empty body for ordinary untracked cache requests, or
exactly four bytes containing a nonzero u32 token. Other lengths and zero tokens are
rejected. A tagged reply ends with WORLD_DONE0x24, containing that token and no sender
ID. The marker is host-originated; clients cannot forward it through either host.
The embedded host emits it after its complete incremental flag-cache cursor, with
cached CHEST and fresh cached teammate poses already queued ahead of it. The Node
relay emits it after its synchronous team-cache replay. All messages use reliable TCP.

The client keeps one active tracked request. Its serial survives connection restarts;
active/completed state is cleared on disconnect. A different request supersedes the
old token. Malformed/mismatched markers are ignored. A replay is ready only after its
matching marker and after both incoming FLAG and CHEST consumers have drained. A
flag-queue overflow invalidates the active replay, even if its marker subsequently
arrives. This is transport/consumption confirmation, not a guarantee that pouch,
shop, dungeon, choice or disabled-progress holds are resolved. Live teammate RESYNC
announcements remain independent; they are not collectively acknowledged by this marker.

Native recovery requires that ready result on the same connection generation plus
its existing minimum settling delay, normal inventory, living Link, safe action,
no native map/event, valid destination, idle loader and successful backup. Repeated
state-only requests preserve pending navigation and its original ten-second deadline.
Timeout, disconnect or invalidation cannot be revived by a late marker. A new manual
request obtains a fresh token. Boss/miniboss synchronization remains disabled.


### Spawn-time enemy reconciliation (protocol38)

The ENEMY layout is unchanged. Protocol38 identifies the updated runtime health policy;
all participants still use the same version. A newly tracked placed actor sends one
snapshot with zero damage. Same-area peers with strictly lower current health answer
with a fresh zero-damage snapshot. A retained same-visit defeat can answer even after
its native actor is gone. Equal/lower snapshots do not elicit answers; duplicate/old
sequence and connection-generation rejection remains. Concurrent damage still uses
its existing additive reconciliation, while snapshots can only lower health.

This repairs a snapshot that arrived before local actor discovery: the new actor's
announcement obtains current lower health from a peer that still knows it. It does
not queue an unbound old damage delta or reuse a prior actor instance. Map/event holds
keep FIFO and visit/instance binding, including retained-death probes without a live
actor. Area/load changes and sharing resets clear these records. The host forwards
these reliable TCP events only to matching team/stage/room/layer; enemy health is not
part of its persistent world cache. Native AI, targeting, animation and physics remain
local. Boss/miniboss profiles, boss arenas and Silent Realms stay excluded.


### Enemy sender history and initial health (protocol39)

The ENEMY body is unchanged. Sender IDs are not bounded by the number of simultaneously
connected players. Each nonzero u16 ID has a seen bit, last sequence and last local
presence generation in fixed arrays. Lookup is constant-time; old streams are not
recycled merely because somebody left. Duplicate sequences, older generations and
wrapped sequence comparisons retain their previous rules. Native visit/reset clears
this history; outgoing sequence remains monotonic across visits. The ledger is520 KiB
and belongs to static/heap SstEnemies state, not a Windows thread stack.

Runtime observe_live treats its first zero-health sample as provisional. That record
cannot announce or answer as a defeat, including during a general enemy announcement.
Incoming health events do not make the uncertain local baseline authoritative. Once
a positive native sample appears, it emits one ordinary zero-damage spawn snapshot and
can obtain fresh peer state. A later real zero is a normal confirmed defeat and remains
available after native deletion. Instance/visit changes restart this process. Explicit
trusted-baseline callers of observe retain their existing behavior. Initial already-dead
actors without a positive live baseline do not originate a defeat through observe_live.
Boss/miniboss and trial exclusions remain; native AI and animation remain local.


### Native missing-actor confirmation (protocol 40)

The ENEMY body stays unchanged. After normal discovery, the bridge reads the verified
8192-byte native defeat table while the entrance loader is idle. A track must have
been observed positive/non-provisional with its native bit clear in this same visit
and instance. If it is later absent and the native bit is set, it emits one ordinary
snapshot with health0 and damage0. Presence alone never confirms; disappearance alone
never confirms; an already-set bit on a new instance cannot reuse earlier evidence.
Zero-health records remain available for same-visit spawn reconciliation. A first
provisional zero cannot arm this proof, and new load/area/instance clears it. Existing
menu/event/offline/boss/trial gates apply. Tracker size remains538704 bytes; no native
flag/health mutation or per-packet allocation is needed for this observation path.
All players must use matching protocol40 builds. Native AI/animation remain local.


### Permanent song ownership (protocol 41)

ITEM FLAG IDs186–193 now allow set-only permanent song ownership. The wire format
does not change. Story flags do not grant these items, and no new CHEST pairs are
admitted. The native bridge validates word11/bits5–12, class3 descriptors and native
methods, then calls the original item setter and checks other inventory/pouch/health
before repairing the saved ownership copy. Existing native guards defer this path
during unsafe gameplay and temporary inventory. A per-game8-bit remembered proof
retains already observed ownership for safe repair. It cannot create an unearned
song or infer a complete song from the parts. Standard flag cache/replay and team
filtering apply. All players need matching protocol41. Full teaching routes remain
unverified; boss/miniboss sync stays off.


### Observed animation retention (protocol 42)

Observed idle/walk/run and supported finite sword poses now survive area loading,
reload and catch-up. The renderer stores its own origin-relative palette copies
under the existing8 MiB/2048-sample limits. It reuses only verified matching geometry,
palette layout and supported native action/clip IDs. Loading pauses capture; new
capture settles first. Disabling remote characters, rebooting or restarting the
render clock clears the library; a changed native clip length invalidates its poses.
No new assets, equipment state or pose data are sent over the network. Unseen poses,
independent equipment/outfits and full campaign/internet verification remain unfinished.
Use matching protocol45 builds. Boss/miniboss sync stays off.


### Observed movement replay (protocol 43)

The existing POSE format now supports locally observed native sprint189/action3,
roll42/action12 and jump44/45/action16. Sprint loops; roll/jump phases remain finite.
Held airborne phase zero settles from three fresh source samples, without advancing
its phase. Wrong actions cannot borrow these poses. Geometry/layout gates,8 MiB/
2048-sample limits, reload retention and explicit release remain. No new assets or
fields go over the wire. Equipment/outfits, multi-track aiming, unseen poses and full
campaign/internet validation remain unfinished. Use matching protocol45. Boss sync is off.


### Verified rigid attachment fallback (protocol 44)

Certain rigid pieces can use a verified cached body joint when their exact pose is
missing. Links require same-frame matrix matches across three source ticks for
the particular native clip; conflicting observations invalidate them. Exact poses remain
preferred. Cached part/parent matrices must also agree at matching phases; any
conflicting cached relationship disables that link until explicit reset or changed
clip metadata clears it. Recognized geometry must also be near native Link in the gameplay camera
before co-op capture/replay. The 8 MiB/2048-sample retained cache stays bounded, with
about 256 KiB of fixed correlation scratch and 44 KiB of clip-specific link records. No new assets or fields go over
the wire. Unverified rigid pieces can be absent during independent body replay. This does not
finish hand variants, independent equipment/outfits, unseen
poses or campaign/internet validation. Use matching protocol45; boss sync stays off.


### Skinned body capture (protocol 45)

A skinned part can have a model origin outside native Link bounds while its bone
palette is still near Link. The source filter now accepts at least two finite,
nondegenerate nearby bone origins among the first eight matrices. Rigid-part
bounds stay unchanged. The controlled formerly incomplete roll phase has zero
additional skinned cache misses through reload and selective rigid eviction.
No new assets or pose fields are transmitted;8 MiB/2048-sample caps remain.
Hand variants, independent equipment, other routes and full campaign/internet
validation remain unfinished. Use matching protocol45; boss sync stays off.


### Ordinary carried vases (props58, optional protocol45 extension)

Reliable TCP type0x32 carries one ordinary SSHD Tubo (profile0x1f0). Its body is:
length-prefixed stage (1..7 ASCII identifier bytes), room/layer/entrance u8;
placementID u16, profile u16, params u32, original room/layer u8;
sequence u32, phase u8 (1 carry,2 place), source pose tick u32;
source Link origin f32x3, yaw i16; vase world matrix f32x12, position f32x3,
rotation i16x3. All multibyte values are little endian; exact length103+stageLength.
The built-in host prepends authenticated senderID u16. No addresses, native actor
instances, models, assets, saves or scene journals travel in these packets.

Both peers and the built-in host need props58 or newer. Older optional-message
handlers ignore it; the separate Node relay has not gained vase support. The
host requires fresh, living, same-area/team poses outside boss/event states.
Carry and placement have separate bounded rate/queue allowances. Receivers
validate finite orthonormal matrices, positions, sequence, presence generation
and exact stage/room/layer/placement/profile/params identity.32 vase records and
128 incoming records bound memory; carry expires after1500ms. PLACE records
remain only in the loaded area and require periodic source announcements for
late arrivals. These events are not persistent progress and never enter the
host's world flag cache or the saved scene journal.

Native application runs on the executing guest actor, using its validated model
and full source matrix. Carry is positioned relative to the render thread's
immutable interpolated teammate pose. Putdown aligns previous position, clears
carry velocity and returns the actor to native physics. Missing/ambiguous actors
are skipped; native locally held actors retain their genuine carry references.
A simultaneous same-vase pickup can disagree until a player puts it down; no
forced local release or fabricated remote holder is attempted. Throws, breaks,
large pots, other object types and cross-area object lifetime remain unfinished.
Carry hand poses still need an observed matching animation in the bounded cache.


### Collected pickups (items59; optional reliable0x33)

Pickup sharing uses collector-only resources: native collection affects only the
collector's currency, health, stamina, ammo or materials. Other peers process an
authenticated consumed receipt without an inventory grant. Existing container
currency/treasure receipts now mark opened state without paying peers; Mallara's
quest crystals remain shared while her accompanying20rupees stay personal.

Direction0 request body: stage string(1..7 ASCII), room/layer/entrance u8,
placedID u16, params u32, resolved itemID u16, sequence u32, opaque token u64.
Direction1 commit/direction2 no-award snapshot append host area epoch u32 and
collectorID u16. Encodings are explicit little endian, at most38bytes here;
transport presence/arrival metadata never travels as native addresses.

The42 approved Item0x281 IDs are rupees2/3/4/32/33, Rupoor34, heart6,
arrows7/8, bombs40/41, stamina42, seeds57/60, bugs141..152 and materials161..176.
Normal placed class0 accepts scene flags0..127 orFF; dynamic class0 requiresFF
and an opaque token. Exact native class1 spawnDrop0x001bfc00 is allowed only with
FFFFplacement/nonzero token and a concrete item. Normal rarity61/62 resolve only
165/166/168;63 to171/173;64 to174/175/176. Both approved resolutions of a single
fixed source share one consumed identity. No other presentation classes qualify.

Host receipts are bounded to16 areas/256pickups each; an epoch ends only when
all players leave its area. Client consumption records are bounded4096. Reliable
TCP backpressure preserves committed receipts; local claims retry until hostACK.
Snapshots remove consumed placed pickups without replaying rewards. Matched
permanent pickups mark the receiver's actual scene receipt and native deletion
unregisters interaction. Story progression/equipment adapters remain separate.

IMPORTANTscope: receiver actor removal currently matches only fixed placed
actors by exact area/ID/params. Source-only dynamic drop receipts are captured,
but parent/spawn bindings for another PC's corresponding dropped actor are still
missing. Stamina respawn cycles, specialized insect/flower/net sources, shared
drop spawning, throw/break lifecycles and crash-safe session receipt recovery are
unfinished. The registry is not proof of every family working in natural play.

Generalized carried-object traffic now uses optional0x34; legacy0x32 remains
small-vase-only to avoid disconnecting58hosts. Reviewed native descriptors exist
for1f0 pots,1f1 big pots and209normal non-NPC barrels. Actualnormalbarrel source
pickup and remote matrix/putdown tests pass; bigpot nativehandpickup remains
unproven. Quest-controlled barrels, bombs, crates and other families stay out.
Use matching items59 runtimes for these optional features. Boss/miniboss and
SilentRealm guards remain; game-libraryID/ABI and completed setup are unchanged.

## Experimental small-carry pose context (not delivered in render65)

Protocol45's36-byte POSE body keeps its existing layout. The flags byte is at
wire offset34; offset35 is reserved zero. Bits0..1 are sword equipment,5..7 are
world/motion state. Bits2..4 now describe a bounded pose context:0 is the ordinary
base pose,0x04 the observed steady small-carry layer, and0x1c an unclassified held
state. Other context values are not eligible for pose learning/exact replay.
Old hosts preserve this byte; old renderers ignore the new context meaning.
Both participants need the new implementation for separation, despite unchanged
wire parsing. No model, palette, bone transform or resource pointer is sent.

The first supported context is native slot5 LiftS_UtoD431 at phase4.5/length9
while the actual player's carried-object getter is nonnull. Configured fields
that cannot be read, unknown overlays, nonfinite/different phase or duration
produce an unsupported held context, not a normal pose. The carried pointer is
never dereferenced by this classifier. Ordinary optional layouts with no carried
field retain their old context0 behavior.

The primary clip/action/phase alone is insufficient: native vase holding uses
26/action67 and walking uses20/action3, with the separate431 layer still active.
Walking20/3 also occurs without carrying. Cache identities include the masked
context, including rigids; context changes restart existing settling. Neutral
fallback uses one complete observed context0 cohort and clears only context bits
in its internal fallback pose. It never treats a carry palette as neutral or
mixes a carry hand donor into a normal body. Hand offset entries and parent
validation keep independent ordinary/small-carry identities in the existing
bounded cache. A missing direct hand sample can use only its own context's
verified child offset, a matching observed body pose and the active body layout;
the older clip-only attachment table remains restricted to context0. Existing
confirmation counts, wrist joints, geometry bounds and tolerances are unchanged.
Same-layer interpolation retains existing cadence; a discrete context edge does not invent an animation phase
between different postures. Root position interpolation remains unchanged.

This separates locally observed poses. It does NOT evaluate unseen native
animation assets or generate cold carry poses. Dynamic overlay phases, other
lift families/outfits, lifting/putting animations and full cold-state support
require further native verification. Boss/miniboss sync remains OFF; currency
and consumable collection still rewards only the collector.

The private carry66 candidate also recognizes two starting-outfit carry hand
shapes whose position-set fingerprints match pl_hands polygon0/7. Raw IDs61/62
map to the observed left/right hand nodes52/53 only for model-only draws; new
skinned layouts are rejected. An instrumented native control confirmed both
as nonindexed model-only draws, learned both context4 hand sides, then removed
all direct carry hand samples after local putdown. All three simulated peers
requested carry context4: verified child placements continued840->1704 and both
hands remained visibly attached. This is one warm starting-outfit vase case;
it does not prove cold poses, other outfits or independent finger-shape choice.
No unseen poses or alternate finger meshes are synthesized. The candidate is
still unpublished and needs release verification before normal distribution.

## Native sword-use equipment state (sword67 candidate)

Equipment bits0..1 now use the verified SSHD1.0.0 is_using_sword predicate
(u16 player+0x6404 equals0x11) after an exact bounded player-vtable check. Native
sword use yields SWORD|DRAWN even before any hand geometry was observed. A
not-in-use value clears DRAWN while preserving observed sword presence; it does
not infer inventory ownership or grant equipment. A configured but unavailable
field/type fails closed for DRAWN. Optional layouts with the field disabled
retain their legacy geometry observation. The publisher and model-only equipment
cache identity use the same merge helper and fresh metadata. Carry context,
world/motion bits, source cadence and the36-byte POSE/protocol45 stay unchanged.

The baseline native control reported no sword equipment in348 observations where
the actual player was using it. The product candidate matched in324 stable
in-use observations and79 stable away observations through attacks, stance,
walking, targeting and ordinary B putaway. Edge samples may straddle the33ms
publisher/50ms fixture snapshot and are not misrepresented as zero-lag. This is
equipment-state metadata: unseen attacks, directional aiming layers such as
18/0, independent sword/finger meshes/outfits and enemy animation remain outside
this fix. No cold animation is generated.

## Developer travel and movement (unreleased dev68 candidate)

The Developer tab has a native-data destination browser across102stages and247
room-zones, with6608 arrival tuples. Shared logical layers are distinguished from
resource archives. Fifteen rooms use an explicit native first-record fallback:
request255 against a first PLY ID0xffff, matching the HD parser's byte-request /
halfword-ID lookup behavior. This is not an invented PLY ID. Event/title-only
variants are visible but disabled; story/layer behavior remains game-controlled.
Metadata existence and unit tests do not prove every arrival works during play.

Multiplayer's new Teleport to player action is separate from Sync and catch up.
It captures connection/presence/scene identity at click time and uses only current
position/rotation. Cross-area travel loads a verified arrival in the SAME target
stage/room/layer, waits for a new local actor and a fresh post-arrival ordinary
peer pose, then places the local player. It never requests a progress snapshot,
backs up/applies a host save or grants rewards. Normal background story sharing
still follows the user's multiplayer settings. Peer departure/rejoin, changed
areas/connections, invalid/dead/stale poses and timeouts cancel the request.

Moon jump (holdL+A) and noclip (left stick, Aup/Bdown, Rboost/right-stick camera)
are temporary local controls, off by default and absent from saved settings.
They reset on teleport/loading/death or unavailable control. Native writes run
only inside the verified local player's virtual update. Noclip scopes the actual
HD Acch disable-bit0 around the original update, preserving other flags and
restoring the original bit for the same actor. It does not skip the whole native
update or affect other actors. Camera and movement inputs validate BEFORE any
collision/velocity changes. Native SetPosRot uses all6verified arguments and
checks positional readback. Exact executable/type/pointer/scene guards apply.

The existing compiled-game ABI and function table already contain these native
methods; no generated-game hook/recompilation has been added. Controlled Linux gameplay verified grounded moon-jump ascent (through the native
SmallJump initializer, not a raw action write), release/landing, noclip through
floor and wall, restored collision, exact same/cross-area peer placement and
representative arrivals including an unspecified first-record room. Health stayed
unchanged in these controls. The full6608-entry metadata catalog is not a full
arrival playthrough; story/layer variations remain game-controlled. Boss/miniboss
synchronization staysOFF. No game-library regeneration is required for the
existing verified ABI2 library.


Pose69 renderer-only identity: the existing equipment DRAWN bit0x02 joins the
existing carry discriminator0x1c in SST_POSE_IDENTITY_MASK0x1e. Wire format and
protocol45 do not change. Supported palette identities are0/2/4/6; other carry
values fail closed. Ownership0x01 and world bits0xe0 do not change palette identity.
Interpolation of primary phase is discrete across this identity boundary while
root/yaw interpolation follows the existing rules. Native source state remains
validated by the sword67 getter; no guessed time advancement or action writes.


Display70: renderer-only frame scheduling supplies the immutable interpolated
carrier snapshot independently of local Link/label drawing. Protocol45/ABI2
unchanged. Guest callbacks never advance interpolation; render-frame consumers
share one result. Scene/entrance, generation, source freshness and presence
checks remain; neither snapshot TTL250ms nor pose expiry1000ms is widened.
