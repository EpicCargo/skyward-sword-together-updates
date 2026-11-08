# World sync and recovery

The SSHD **v1.0.0** bridge runs on the guest's input thread, roughly 30 times per second. It preserves
the complete guest CPU state around native calls. Other executable versions fail closed. Each player
keeps their own save and game simulation; remote Links replay locally observed locomotion/sword poses. Missing coverage uses a coherent static idle frame when available, rather than the viewer's animation; a completely cold or incompatible cache can hide parts/avatars until verified geometry is observed.
This is an initial shared-world implementation, not verified support for a complete four-player
playthrough.

Revision coop56 also treats blank Room and `public` as the same built-in-host default.
Custom room names remain exact. **ESC -> Developer** exposes bounded connection, source
pose, rendered-mode, cache and wrist diagnostics, with snapshot freeze/capture and clipboard
copy. These controls do not mutate guest state or upload a report. The idle fallback does
not implement missing actions, independent outfits/equipment or movable-object replication.

Movable props such as vases currently remain separate in each player's simulation.
A remote player picking up a vase or putting it elsewhere does not replicate the held
object or its new world position. This was reported during the first two-PC internet
session on 2026-10-29. Shared permanent switches/reward receipts do not implement object
pickup, carry attachment, placement, throw or break lifecycle synchronization. Puzzles
that depend on these transient interactions are not covered by the current shared-world
implementation.

## Shared state

| State | Behavior |
| --- | --- |
| Story progress | Reviewed logical boolean IDs from [story-sync-policy.tsv](story-sync-policy.tsv). The actual HD definition must also be one bit. Native setters mark the game's manager dirty. Unknown flags, counters, temporary event states and identified boss flags are excluded. |
| Permanent equipment | Selected sword, traversal, tool, tablet and other equipment ownership bits. Runtime rejects counter definitions. Pouch objects, bottles, consumables, currency, ammo, materials and health rewards require transactions rather than ownership-bit sharing. |
| Scene switches | Permanent scene-table bits that **rise while connected outside boss encounters**. Loaded and saved copies are merged after entering the target scene table. Container receipt switches are excluded and delivered through reward transactions. Pre-existing scene tables are not bulk-announced because they can include boss flags. Temporary and zone tables stay local. |
| Supported chests/wardrobes | Rupees, materials (IDs 161–176), small keys, heart pieces (94), dungeon maps (50) and boss keys. A completed native TBox or permanent scene receipt plus container metadata identifies the reward. Receivers call native counters or dungeon ownership setters, set applicable first-collection ownership and record their own receipt in the same guest-thread operation. Opened chests never grant twice. |
| Beedle Life Medal and heart piece | Exact story receipt/item pairs814/114 and813/94. Native inventory delivery preserves personal currency. Incoming purchases wait inside F002r; returning rebuilds sold-out stock. Completed saved/live receipt repair never grants again. |
| Pouch expansion containers | Item 113 increases native capacity once per personal receipt, including with a full pouch. Personal upgrades are preserved; the larger intact saved/live capacity and completed receipts repair missing capacity/ownership. Receipt replay cannot add slots repeatedly; the practical limit is eight total slots. The original Goddess expansion chest (672) recovers from saves. Shop pouch purchases share ordered receipts in protocol32. |
| Medals and spare ammo pouches | All seven medals and all nine Seed Satchel/Quiver/spare Bomb Bag tiers insert into unlocked free pouch space with native quantities. Other contents and base ammo remain intact. Full pouches hold delivery; completed receipts prevent refill after use or storage. Native medal effects remain active. Batreaux’s Cursed Medal chest and nine unambiguous Goddess Chest identities recover from saves. Beedle’s Life Medal purchase is also shared with its sold-out receipt. Shop capacity upgrades share ordered receipts; other unreviewed purchases remain personal. |
| Bottle containers and Potion Lady | Native items 153 (empty) and 126 (Revitalizing Potion) insert into the first empty unlocked Adventure Pouch slot, initialize quantity one and record a personal container receipt. Full/locked pouches and restricted inventory modes hold delivery; other slots and filled bottle contents are preserved. Potion Lady’s first bottle pairs its native insertion with personal story receipt 35; an existing live/saved receipt repairs completion without another bottle. |
| Dungeons | Small keys, maps and boss keys arrive through chest transactions after entering their source dungeon. Only the required native counter/ownership field and personal receipt change; raw dungeon flags and key counts are never OR-merged. Boss completion stays personal. |
| Earth Temple key pieces | The five vanilla Eldin digging spots share one native counter increment per personal scene receipt, capped at five. Story completion waits for the full personal key. |
| Freestanding heart pieces | The five vanilla placed heart pieces use native counters, personal health/capacity effects and permanent scene receipts. Saved receipts support recovery without revisiting; active remote pickups are removed to prevent a second collection. |
| Single Gratitude Crystals | All 15 vanilla permanent pickups use native counter 38 and personal scene receipts. Saved collections recover without visiting the source; duplicate representations share one receipt. All thirteen NPC five-crystal rewards (fifteen receipt routes) are covered separately below; Batreaux’s reviewed threshold rewards share separately; his Cursed Medal chest shares through its container receipt. |
| NPC Gratitude Crystal packs | Wryna, Sparrot, Orielle, Bertie, Fledge, Dodoh, Mallara, Kina, Beedle, Owlan and the Item Check quest use personal story receipts and native five-crystal grants. Mallara includes her 20 rupees and auxiliary completion flag; Bertie includes his introduction flag. Completed receipts repair missing live/saved completion and auxiliary flags without granting inventory again. Peatrice and Peater are two endings of one pack: either recorded ending satisfies incoming copies of the other, preserving the local choice and preventing a second grant. Peatrice also repairs story 635. Owlan repairs story 481 together with completion 482. Beedle completion 477 is shared; discount-used flag 478 stays personal. Cawlin’s letter uses actual reward receipts 553/561, treating both endings as one pack; cleanup flag 558 alone cannot announce a reward. Each recipient first chooses a personal hand-over (552/555); incoming packs translate into that route, and wait if no unique choice exists. Pipit’s six post-reward flags repair together. Letter ownership, hand-over and branch prerequisites stay personal. Parrow’s final pack includes one empty bottle only if the personal loan receipt 382 is absent; a full/locked pouch holds the whole bundle. Existing completion repairs the loan receipt without another grant. Saved receipts recover without visiting; other reviewed NPC completion flags remain local to preserve their unclaimed rewards. |
| Batreaux threshold rewards | Scene receipts 70–75 in table 0 pair with Medium/Big/Giant Wallets, one heart piece and 300/600-rupee bundles. Story 360 pairs human transformation with the Tycoon Wallet. Receipts prevent duplicates; stronger wallets, personal extra-wallet capacity and rupees are preserved. Wallet flags can repair from completed receipts. Currency waits for the native wallet capacity to commit; the separate Cursed Medal chest now uses a native pouch transaction. |
| Silent Realm completion | All four trials pair personal completion receipts with Scale, Earrings, Clawshots and the Stone of Trials. Saved completions recover ownership and wheel state without refilling ammo. Active trial simulations hold incoming progress until return. Story 210 remains personal. Sky Keep opening waits for personal Hylia completion and Stone ownership. |
| Traversal gear | Digging Mitts, Mogma Mitts, Water Dragon’s Scale and Fireshield Earrings use checked native ownership and personal container receipts. These passive grants preserve the item-wheel unlock, ammunition and pouch. The saved Fire Sanctuary Mogma Mitts chest also recovers without a visit. |
| Dungeon tools | Bomb Bag, all Bow and Beetle variants, Clawshots, Slingshot/Scattershot, both Nets, Gust Bellows and Whip use native ownership setters with a personal chest receipt and item-wheel unlock. Original saved receipts support recovery across areas. First Bow/Slingshot acquisition and upgrades supply base ammunition; existing ownership preserves spent ammunition and upgrades. Bomb Bag receipts preserve the existing bomb count. Upgrade chains include base ownership; resync repairs missing lower tiers without refilling ammo. Other inventory and equipped selection are preserved. |
| Regular placed enemies | Matching stage/room/layer, profile, placed object ID and parameters. Health decreases are sent as damage; receiving them updates the comparison baseline to prevent echoes. Concurrent hits add. Native menus/dialogue hold up to 512 matching events, bound to their original actor instances and room visit. Re-enabling enemy sharing requests fresh health independently of story sharing. Remote deaths use native actor deletion, mark enemy-defeat state and suppress duplicate drops. |
| Regular enemy movement | One eligible player in each area supplies position, facing and velocity at about 10 Hz. Other players interpolate the updates. Ownership passes to another eligible player when the current owner leaves, disconnects, enters an event or disables movement sharing. Native AI, targeting and animation still run locally. |
| Saved reward catalogue | 125 reviewed, unambiguous permanent collection receipts (119 containers and six Batreaux scene rewards) recover their fixed vanilla rewards without loading the source area. Ambiguous and unsupported containers still need live metadata. |
| Late join/recovery | Host replays admitted progress and chest rewards; teammates announce fresh progress and current regular-enemy health on request. Enemy events are live and never permanently cached by the host. |

Silent Realm tears, timers, Guardians and switches stay local. The bridge holds incoming world changes and disables recovery while inside S000/S100/S200/S300, then applies queued completion rewards after returning. It does not infer trial completion from merely owning the corresponding tool.

Boss and miniboss actor profiles are never eligible enemy targets. Boss arenas, current-room encounters and stage-wide boss actors
pause all world application and recovery. A miniboss instantiated in another room does not pause
the rest of the dungeon. Regular-enemy synchronization also filters actors to the current room. Boss completion flags are excluded from the story policy;
scene changes observed during these encounters do not enter the shared cache. Each player completes
their own bosses. Recovery also waits for the event manager to leave a cutscene/dialogue and Link to
return to a normal idle/walking action. Native map/inventory execution and drawing controls also pause application and recovery, even when Link reports an idle action. The encounter and event state travel in pose safety bits.

## Previously admitted scene history

World revision scenes48 stores a fixed 436-byte journal beside the native save-data
files (`save_0/sst-scenes-v1.bin`). It contains only permanent scene changes already
admitted and applied during co-op outside encounters, with live source-table
metadata. It is not a snapshot of pre-existing save tables. Replay intersects
current native ownership and current receipt/policy exclusions; it learns known
team progress without queuing local save mutations. Unknown or locally cleared
bits are never invented by disk recovery. Live host/peer snapshots can still
reapply higher team progress through the existing guarded path.

The file is versioned, bounded and checksummed; replacement is atomic. Corrupt or
unreadable history is preserved and disk recovery stays disabled for that session,
while live sharing continues. Failed writes preserve the last complete file and
retry later. Backups include this file when present before the first remote merge.
The scope is the existing save-data directory; ownership is checked against the
currently loaded native file. This adds no boss/miniboss synchronization and no
new network message. Controlled native cache reinitialization and area transitions
are tested; a natural save/exit/relaunch campaign still needs validation.

## Recovery controls

Open **ESC → Multiplayer**:

- **Request team state** merges the host cache and asks active teammates for fresh progress and enemy
  health. It never replaces your complete save.
- **Resync and reload area** requests shared state, waits briefly for replies, then calls the game's
  entrance loader for your current stage, room, layer and entrance. This refreshes doors and puzzles
  whose actors only read progress when created.
- **Catch up**, beside a teammate, requests fresh state and loads that teammate's area entrance.
  It does not copy their exact coordinates. The teammate must have a recent sample outside a boss or
  cutscene. Catch-up requests refresh the latest teammate state first, so an older displayed position in another area does not disable the button. The final destination check rejects other teams, defeated players and stale poses. Requests time out after ten seconds of unsafe local gameplay. Repeated state refreshes preserve a pending reload/catch-up and do not extend its deadline. A different explicit navigation request replaces the previous destination. Pending recovery refreshes cross-area teammate state once per second while waiting, without extending its deadline. Close the game map or inventory to allow the transition.

After the new player instance finishes loading, reload and catch-up report completion. The teammate table explains unavailable recovery destinations, including stale state, defeat, events/menus, boss encounters and busy actions. Failed catch-up requests name their reason, including a teammate leaving the session. A fresh defeated pose takes precedence over its menu flag; old poses are labelled as needing fresh state.

The menu shows how many received rewards are waiting, including rewards deferred to their source dungeon and small keys waiting for a free slot. Bottle rewards explain when they need a free unlocked Adventure Pouch slot; move an item to Item Check or unlock the pouch to allow delivery. The native map/inventory, dialogue, cutscenes, movement restrictions and disabled progress sharing show their waiting conditions. Conflicting reward identities remain held for review.

The first remote progress application/recovery makes a backup under
`<SST data directory>/sst-backups/save_0/YYYYmmdd-HHMMSS/`. It copies the slot directory's regular
files, including `wiiking2.sav`, and records both in-memory world buffers as `unsaved-world.bin` for
diagnosis. Twenty backup directories are retained. A failed backup blocks progress changes and
recovery. To restore disk progress, close the game and copy the backed-up regular save files into
`save/save_0/`; `unsaved-world.bin` is a memory capture, not a loadable save file.

## Current limits

- This is set-only progress sharing. Resettable puzzle state, movable blocks, enemy AI/targeting,
  damage animations, synchronized cutscenes and every individual puzzle script are not replicated.
  Some area changes require a reload. A full playthrough is needed to refine the switch/story policy.
- Chest sharing currently covers rupees, materials, heart pieces, small keys, dungeon maps, boss keys, the 15 reviewed tool IDs and four passive traversal items, using vanilla v1.0.0 location identities.
  Other equipment rewards, boss heart containers, other pouch contents, unsupported bottle gifts, shop purchases and
  other pickups still need complete reward handling. Unsupported chests remain personal. Equipment
  bits do not implement all acquisition cutscenes/UI.
- Heart-piece chests increment each player’s own remainder modulo four and total-piece counter.
  Completing four adds one heart up to the native 20-heart limit; each new receipt heals through the
  native routine, including Life Medal bonuses. Replaying a receipt cannot heal again. This includes
  permanent scene-receipt chests and wardrobes such as Zelda’s room. Boss heart
  containers, other freestanding pickups and repeatable/temporary container rewards still need handling.
  The five permanent freestanding heart pieces are also shared. Their known save receipts can
  recover rewards after restarting, even though collected item actors no longer spawn.
- Dungeon maps and boss keys also wait for their native source dungeon before ownership and receipt
  are applied together. Other dungeon fields are preserved. Existing ownership is no longer copied
  independently through generic flags; reviewed saved receipts recover on a new session without an area visit; other containers need live
  native metadata. First-map tutorials and boss-door scripts remain personal.
- The reviewed active tools, including upgraded Bows/Beetles, Scattershot and both Nets, can
  arrive across areas. Container deliveries use personal receipts; original shop/quest gifts use
  checked ownership, prerequisite and item-wheel grants. The vanilla catalogue also recovers
  saved rewards without visits. First-use tutorials and equipped selection remain personal;
  unsupported NPC/pouch transactions still need separate coverage. Native restrictions remain active.
- Small-key rewards wait while the receiver is in another dungeon/area. Enter the source dungeon to
  receive them; both native managers must agree on its table. A full key counter leaves the reward
  pending until a key is spent. Opening the same chest locally first satisfies its receipt, so a
  queued message cannot grant it again. Key use and lock-opening scripts remain local; this does not
  yet reconcile every shared door or key from enemies/digging spots.
- The five key-piece digging spots can deliver across areas. Repeated receipts cannot increase the
  counter again. A remotely collected active spot disappears until reloading shows its hole. First
  pickup/Fi events and the Earth Temple door interaction remain local. Ordinary soil rewards and
  randomized key-piece locations are not supported by this adapter.
- Single Gratitude Crystals can arrive across areas and during the day. Receivers retain their
  own count, capped natively at 127, and each of the 15 receipts can grant once. Original night-time
  spawning and quest prerequisites still apply to local collection. All thirteen vanilla five-crystal rewards use fifteen reviewed NPC story receipt routes. Cawlin’s letter reward waits for the recipient’s personal hand-over choice, then uses that ending’s receipt. Parrow’s final pack uses personal loan proof and a conditional native bottle insertion; Batreaux’s reviewed threshold rewards share separately; his Cursed Medal chest shares through its container receipt.
- Shared rupee/material rewards respect each player's wallet/material cap, as a normal pickup does. A remote
  grant removes an active unopened TBox so it cannot be collected twice; reload the area to show the
  saved opened box. Wardrobes keep their model and native empty interaction. A TBox’s auxiliary
  permanent scene switch is mirrored after its inventory receipt; chest-specific event scripts are
  not replayed. Metadata survives disconnects and
  room changes within the current game session; after every game/host restarts, previously opened
  chest contents are rediscovered when a player revisits the area. The five known freestanding heart
  receipts, 15 single crystal receipts, fifteen NPC crystal routes, Potion Lady’s gift and 125 reviewed collection rewards can be read directly without
  a visit. Three ambiguous container identities and unsupported rewards have no complete save catalogue.
- Enemy support is an explicit regular-profile list, only for placed actors with stable IDs. Dynamically
  spawned actors and unsupported profiles remain local. A locally reloaded room can respawn ordinary
  enemies; fresh teammate snapshots may remove enemies already defeated on their side. Snapshot
  reconciliation is best effort if damage is occurring at the same instant as recovery.
- Movement follows the lowest eligible player ID in the area. The menu has a separate **Share regular
  enemy movement** option; disabling damage/defeat sharing also disables movement. Stale targets expire
  after half a second. This aligns transforms without replicating complete AI/animation state or making
  the remote Link render copies into physical actors. Position correction can still interact with
  local physics and targeting. Broad profile coverage needs further gameplay testing.
- Catch-up preserves the local time/trial mode. It does not reconcile every story layer, available
  entrance, trial state or personal item required at the destination. Use teammates in ordinary areas.
- Progress options default on; the menu can disable progress or enemy sharing independently. Boss
  synchronization has no enable switch. All participants must use protocol version 39.

Rejoining with the same player ID starts fresh pose, enemy-damage and enemy-movement counters.
Receivers tag each connection locally, so a restarted counter is accepted and an older stream
cannot replace it. Map-held hits keep their original connection tag: equal sequence numbers
before and after a rejoin count as separate hits, while duplicates within a connection apply once.
This changes no wire format and keeps boss/miniboss exclusions in place.

For isolated native verification, `SST_RECOVERY_FILE=/absolute/path` accepts one local command file
containing `resync`, `reload`, or `peer <id>` and deletes it after reading. This exercises the same
mailbox as the menu. `SST_TRACE=1` records admitted native flag changes, enemy updates and reloads.
Game files, raw memory captures, saves and keys are never distributed with the source.


The Multiplayer tab reports the last measured host/relay round trip, time since the last packet,
and bytes waiting in the mod's send queue. Round trip uses matched heartbeat replies and resets
on every connection; this is not a separate measurement between every pair of players. Teammates
in boss encounters, events or native menus are identified beside the recovery controls.

Repeated incoming flag snapshots are coalesced by logical identity. If a burst still exceeds a
bounded progress or enemy-damage queue, the client drains old updates before requesting fresh
team state automatically. These requests are limited to one per second and wait for a stalled
outgoing connection to clear. The Multiplayer tab shows the number of these backlog refreshes
for the current connection. Recovery snapshots reconcile enemy health best effort; this does
not add host migration or persist a session cache after the host exits.

Beedle's Life Medal and heart piece use original purchase flags814/813 and exact
receipt/item pairs (protocol31). Both wait while F002r shop actors are active. Returning
rebuilds sold-out stock; recipient currency remains personal. Completed receipts repair
without a repeated medal or heart grant. Pouch upgrades and extra-wallet purchases
use multi-bit shop counters and remain personal pending separate review.

Protocol32 shares the three pouch shop upgrades and three extra wallets with ordered
purchase receipts5362–5367. Original counters707/708 prove completed prefixes and
repair missing live/saved capacity without another additive grant. New purchases
wait for missing predecessors or while inside F002r; the UI explains how to resync.
Pouch purchase proof combines with completed expansion containers and keeps the
eight-slot limit. Extra wallets use native508 and its three-wallet cap, preserve
currency, and need no pouch space or unlock. Temporary inventories hold repairs.


Protocol33 shares the unique Bug Medal through5368/item70 and the original acquired
flag70. Storage/replay and legacy carried or completed container proof repair ownership
without a second medal. Duplicate medal containers complete without another insertion.
Bug Medal grants and missing stock repairs hold inside Beedle F002r. Net71/Big Net140
raw and typed rewards also hold there when base Net is missing. Leaving commits safe
rewards; returning rebuilds original shop stock. Pouch/map/temporary-mode guards apply.


Protocol 34 adds first-acquired shield receipts 5369–5378 for items 116–125, including
crafted tiers. Native acquired flags prevent replay after storage, break or spent
durability. A new first acquisition grants one shield at native starting durability;
repeat purchases, recipes, materials and base shields stay personal. Generic shield
containers keep independent opened receipts. Legacy carried and completed container
proof can repair ownership without adding or repairing a shield. Deliveries and missing
flag repairs hold inside Bazaar F004r until exit. No boss/miniboss sync is enabled.


Protocol 35 shares first acquired spare-ammo variants 128–136 with fixed receipts
5379–5387. Crafted tiers use the same first-acquisition rule; repeated recipes and
purchases remain personal. Empty bags prove prior ownership and are not refilled or
replaced by replay. New first acquisitions get their native starting amount; main
weapon ammo, materials and currency stay personal. Generic container receipts are
independent. Legacy empty-bag/container proof repairs acquired flags without insertion.
Ammo grants and missing-flag repair hold inside Bazaar F004r until exit. Boss/miniboss
sync remains off, and rewards use reliable TCP.


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


Protocol37 confirms the host's cached shared progress before native resync, reload or
catch-up. Use Multiplayer > Resync and reload area when stuck. Recovery waits for a
matching acknowledgement and drained progress/reward queues, then normal gameplay.
Full-pouch, shop and choice rewards stay queued. Close the map and return from temporary
inventory; bosses/trials remain local. If a request times out or the connection changes,
a late reply cannot reload you; request again when the connection is stable.
Validation:24 new controlled native cases on Linux and Windows/Wine; Linux also repeats
137 earlier regressions (161 total). Full C,38 relay tests,21 mixed-platform network
cases,50 patches and a six-minute four-player replay pass. Actual Windows hardware,
full natural campaign and separate-PC internet remain unverified. Everyone needs
protocol37. Evidence: build/verification/recovery37-batch/.


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
