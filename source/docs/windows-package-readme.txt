Skyward Sword Together - Windows test build, revision phase71, SkyRecomp2.0.2, protocol45

Phase71: reject animation source phases beyond their known native duration.

A known45-frame clip can no longer teach phase46 and reuse it as valid phase1
through looping selection, or seed an invalid neutral snapshot. Source readiness,
hand observations, palette capture and uniform rebuilding share the duration
bound. An invalid local pose cannot bypass the rigid replay policy. The exact
endpoint remains accepted; unknown duration stays unknown; confirmed duration
changes retain the existing full cache invalidation. No phase clamping or guessed
animation time is introduced.

This fixes a reproduced cache-admission defect, not a confirmed cause of the
reported head nodding. Native first-person body visibility remains unfinished.
Protocol45/ABI2/Launcher5 unchanged; existing compiled game libraries are reused.

Display70: carrier smoothing independent of names and local body visibility.

The render thread now refreshes one immutable interpolated peer snapshot per
presented frame before world command execution. Labels/body draws share that
snapshot; hiding names or entering first person no longer lets carrier positions
expire merely because those draw paths stop. Guest prop updates read the copied
snapshot, preserving scene/connection/presence and250ms/1000ms freshness guards.
This does not supply missing first-person avatar geometry or fix general head
nodding. No physics, wire, boss, pickup reward or compiled-game ABI changes.

Drawn/sheathed pose separation (pose69)

Walking poses and verified hand attachments now distinguish native sword-drawn
state, as well as carry context. Equal gait/action/phase cannot borrow a cached
pose from the opposite sword state. A transition keeps animation phase discrete;
position/yaw smoothing is unchanged. Normal, drawn, carry and carry+drawn evidence
use separate bounded identities. Unknown held states stay unsupported.

Only normal sheathed idle can seed the coherent neutral body fallback. The
original requested equipment visibility is kept separate from that internal
fallback. Existing geometry, freshness, phase/layout and donor checks remain.
Palette capacity stays8MiB/2048samples and512verified hand entries; the bounded
legacy attachment table is90KiB and parent metadata8KiB. These are metadata,
not new game assets. This does not generate unobserved animations or finish
sword swings, head/gaze/additive layers, first-person coverage or enemy AI.

Use matching pose69 builds for visual testing. Protocol45/GameABI2/Launcher5
remain compatible; an already prepared compatible game library is reused.
Boss/miniboss synchronization remainsOFF; pickups reward only the collector.

The engine is updated to official SkyRecomp2.0.2 while keeping SST's co-op changes,
low-memory default, worldscenes48 and Developer diagnostics. Existing compatible
finished game libraries retain ABI2/ID5e5bafbb450f894b and were reused in Linux and
Windows/Wine tests; this update does not require another full compilation for them.
New first-time builds use upstream's O0 policy. Fresh setup speed and resulting
O0 gameplay performance have not been benchmarked in this batch.

Blank Room and public now select the same default room in the built-in host.
Custom room names remain case-sensitive; Team must match. This fixes the confirmed
default-room mismatch, not the separately reported network connection timeout.

Use ESC > Developer for a read-only connection/animation snapshot, Freeze/Capture,
and Copy diagnostics. The clipboard report omits names, addresses and private game
data, and does not upload anything. Multiplayer shows Hosting/Joining and peer count.

Remote avatars use verified playback or one observed static idle pose rather than
copying the viewer's lock-on/sword actions on a cache miss. Let local Link stand idle
briefly after starting so the renderer can observe that pose. A completely cold cache
or unverified layout may hide parts/avatars. Missing hands and unsupported actions
are not claimed fully fixed. Supported small-vase carry/placement is available. Multiplayer offers position-only
Teleport to player separately from Sync and catch up recovery.

Use Settings > Video > Frame rate > 60 FPS on every PC for current co-op testing.

Verified rigid hand variants share their observed wrist poses. The displayed
geometry, finger shape, outfit and equipment still follow local Link. Only locally
observed supported poses are available. Verified hand offsets may survive removal of direct rigid snapshots when the
matching body/context remains available; missing evidence still suppresses hands.
Wire protocol 45 is unchanged. Use matching renderer revisions for visual testing.

Previously admitted permanent scene changes can recover in later sessions through
a small proof journal, filtered against current ownership. It does not copy old
save tables, counters, rewards or boss state. Invalid history is preserved. The
journal lives beside native save data and is included in pre-merge backups when
present. A complete co-op campaign is still unverified.

1. Extract this ENTIRE ZIP into a fresh folder on your Windows PC.
2. Open skyrecomp.exe. Windows 10/11 x64, AVX2 CPU and a Vulkan 1.3 GPU driver
   are required. Use a test save for this experimental co-op build.
3. Select your own prepared Skyward Sword HD v1.0.0 game folder (exefs/ and
   romfs.bin), then use Set up game files. First setup uses the included portable
   compiler to build a Windows skygame.dll with one compiler job by default. The
   separate Update & Play launcher offers optional 2/4/8 jobs before setup starts.
   This can take tens of minutes; allow it to finish. Later launches reuse the DLL.
   A Linux skygame.so cannot be used as the Windows game-code library.
4. Press PLAY. Open ESC > Multiplayer.
5. One player chooses Host game (TCP port 7623, plus optional UDP movement). Other players
   enter that player's reachable IPv4 address and port, then choose Join game.
   Everyone must use protocol45. Blank Room and public match on updated built-in
   hosts; custom Room and Team values must match exactly.

For internet play, put the PCs on the same configured Tailscale network and
use the host's Tailscale IPv4, or forward TCP 7623 and allow it in the host's
firewall. Also allow/forward UDP 7623 to use UDP movement. If UDP is blocked,
movement uses TCP automatically. Multiplayer shows the active movement transport.
Story, rewards, damage and recovery always use TCP. See windows-testing.md. The Linux browser preview port 8765 is not
the co-op address. sst-network-check.exe can check a host before starting; append --udp
to also check the optional movement channel.

Boss/miniboss sync remains OFF. Supported regular enemies and reviewed story,
world and reward transactions sync. Remaining puzzles/rewards, cutscenes and
equipment/animation replication are incomplete. Kina, Beedle, Owlan, Item Check and Cawlin’s letter
share their crystal rewards. Letter crystals wait until each player hands over their
own letter; either teammate ending awards one pack in the player’s own chosen route. Owlan includes his follow-up completion; Beedle's
discount-used state stays personal. Opposite Item Check endings cannot grant twice.
Repeated snapshots coalesce and a full incoming queue triggers a team-state refresh.
Recovery controls offer team resync, current-area reload and catch-up at a
teammate's area entrance, with completion feedback after loading. Empty-bottle
containers, the Sealed Temple’s filled potion bottle, Potion Lady’s first gift
and Parrow’s final crystal/bottle bundle use native pouch
transactions. Full or locked pouches hold delivery until a usable slot is free.
A prior personal Parrow loan grants crystals alone and preserves existing contents.
All thirteen vanilla NPC five-crystal locations have reward adapters. Batreaux’s seven reviewed threshold rewards now share wallets, a heart piece and
rupee bundles. Higher wallets, extra-wallet purchases and held rupees are preserved.
Human transformation pairs with the Tycoon Wallet. All seven medals and nine
spare ammo pouch variants share from containers using native quantities. Completed
receipts prevent refills after use or storage. Batreaux’s Cursed Medal chest and
nine unambiguous Goddess Chest receipts recover from saves. Pouch shop
purchases remain personal. Pouch expansion chests now add capacity even when full,
preserve personal upgrades and repair missing saved/live capacity from completed
receipts. The pouch stays capped at eight slots; repeated resyncs do not add more.

Validated under Wine: full first-run game compilation, interrupted setup recovery,
Vulkan bedroom gameplay and quest-reward delivery from a Linux socket peer.
Windows/Linux host and client networking tests also pass. An actual Windows PC,
full co-op routes and separate-PC internet play still need testing. This is an
experimental test package.

Optional UDP movement was tested with three simulated teammates in Linux and
Windows/Wine gameplay. One-way UDP outages switch to TCP and recover automatically.
Loss, reordering, duplicates, corruption and delayed/stale packets have game-free
regression coverage through Co-op Anything's network-chaos tool. The preceding batch verified both letter endings, personal hand-over waits,
completion repair and native reload without another grant, plus all 256 bridge actors
in network tests. Protocol 28 passes 60 native checks per platform for bottle/pouch delivery,
Potion Lady/Parrow receipts, potion consumption without resync refills, saved
bottle recovery, Batreaux reward/ownership repair, capacity-safe bulk currency
and reload/catch-up completion. These checks do not replace a real four-player internet campaign test.

The package contains no console keys, game assets, precompiled game code or
saves. Use your own game files. Runtime logs go to skyrecomp.log; preserve a
failed run's log before launching again.


Protocol 29 validation: 93 controlled native checks on Linux and Windows/Wine,
including seven medal/nine spare ammo variants, native Cursed/Life Medal effects,
actual quiver consumption without resync refill and removal of Batreaux's original
medal chest. Recovery has 123 reviewed saved collection receipts. All 29 Node relay
checks, 19 mixed-platform network cases and 42 engine patches pass. Actual Windows
PCs, internet play between PCs and a full campaign still need live testing.


Protocol 30 validation: 123 controlled native checks each on Linux and Windows/Wine.
Pouch expansion containers arrive even when full, preserve personal upgrades, repair
missing saved/live capacity from receipts and stop at eight usable slots. Replays
cannot add slots repeatedly or recreate stored items. The original Goddess expansion
chest recovers from saves and is removed natively after delivery. A Life Medal in
slot 8 changes native healing capacity. Recovery now covers 124 saved collection
receipts. All 30 relay tests, 19 mixed-platform cases and 43 patches pass. Actual Windows
PCs, internet co-op, full campaigns, pouch shop sharing and crafting still need work.

Protocol 31 adds Beedle's Life Medal and heart-piece purchase transactions.
They share their original sold-out receipts, preserve your rupees, and wait while
you are inside Beedle's shop. Leave the shop to receive them; returning rebuilds
stock from the completed purchase. A full pouch holds the Life Medal until a
slot is free. Stored medals and completed heart pieces are never granted again
by resync or receipt repair. Pouch shop upgrades, extra wallets and crafting
remain personal. Boss and miniboss synchronization remains disabled.

Read beedle-purchases.md in this folder for supported shop behavior.
Matching source and protocol notes are included in co-op-source.zip.

Protocol31 validation: 136 controlled native checks each on Linux and Windows/Wine,
including thirteen new Beedle purchase/stock cases. The 12-minute four-player native
lobby uses one game plus three lightweight peers. All31 relay tests,19 mixed-platform
network cases, two engine regressions and44 engine patches pass. Evidence:
build/verification/shop31-batch/. Actual Windows hardware/internet and full natural
routes still need testing. Read beedle-purchases.md in the Windows folder
(docs/beedle-purchases.md in source) for supported shop purchases.

Protocol32 adds all three Beedle pouch upgrades and all three extra wallets.
Each purchase has an ordered receipt. Missing earlier purchases wait for team-state
refresh; later receipts cannot skip them. Purchases wait while you are inside the
shop, then stock rebuilds when you return. Native purchase counts repair lost
saved/live capacity. Pouch capacity stops at eight slots; extra wallets stop at
three. Your rupees and pouch contents remain personal. Extra wallets require no
free pouch slot or pouch unlock. Temporary inventories hold delivery and repair.

Protocol32 validation:162 controlled native checks each on Linux and Windows/Wine,
including26 ordered pouch/extra-wallet purchase cases. A6-minute four-player replay
uses one game and three lightweight peers. All32 relay tests,19 mixed-platform cases,
two engine regressions and45 patches pass. Evidence: build/verification/shop32-batch/.
Actual Windows hardware, internet play and natural campaign/shop routes still need
validation. See beedle-purchases.md in the Windows folder (docs/ in source).

Protocol33 validation:186 controlled native checks each on Linux and Windows/Wine,
including24 unique Bug Medal and Net stock cases. The6-minute four-player replay uses
one game and three lightweight peers. All33 relay tests,19 mixed-platform cases,
two engine regressions and46 patches pass. Evidence: build/verification/shop33-batch/.
Actual Windows hardware, internet play and natural campaign/shop routes need validation.

Protocol 34 shares the first acquisition of each shield type, including crafted tiers,
with one personal copy and native starting durability. Existing damaged, stored or broken
shields are not replaced on resync; repeat purchases and crafting costs remain personal.
Shield rewards wait until leaving the Bazaar. Validation: 247 controlled native checks
per platform, including 61 shield cases; 34 relay tests, 19 mixed-platform cases and 47
patches. The six-minute replay uses one game and three lightweight peers. Evidence:
build/verification/craft34-batch/. Actual Windows hardware, natural campaign/crafting
routes, shield combat/break routes and separate-PC internet still need validation.

Protocol 35 adds first acquisition of all nine spare-ammo pouch variants, including
crafted tiers. Empty bags still count as owned; resync never refills or replaces them.
Main bombs, seeds and arrows, materials and repeat purchases/recipes remain personal.
Ammo delivery and ownership repair wait until leaving the Bazaar. Validation: 314
controlled native checks per platform, including 67 spare-ammo cases and actual Bow use
from a newly received quiver. All 35 relay tests, 19 mixed-platform cases and 48 patches
pass. The six-minute replay uses one game and three lightweight peers. Evidence:
build/verification/ammo35-batch/. Actual Windows hardware, natural campaign/crafting
routes and separate-PC internet still need validation.

Protocol36 adds checked progression for all six sword tiers, stronger-tier preservation,
temporary-inventory holds and saved/live recovery. Reload the area if sword equipment
has not refreshed. All participants need protocol45. Controlled native evidence:
39 sword checks on Linux and Windows/Wine; Linux also repeats314 earlier checks
(353 total). Full C,36 relay,19 mixed-platform network cases and49 patches pass.
Evidence: build/verification/sword36-batch/. Natural campaign/cutscene routes,
actual Windows hardware and separate-PC internet remain unverified.


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

Protocol38 adds automatic health exchanges for newly discovered placed enemies.
Same-area wounds/defeats can recover without manual resync, including after a removed
native actor. Boss/miniboss sync stays off. Validation:23 controlled native cases on
Linux and Windows/Wine,137 earlier Linux native regressions (160 total),38 relay tests,
22 mixed-platform cases,51 patches and a six-minute four-player replay. Native AI and
animation remain local. Broad natural spawn/campaign routes, actual Windows hardware
and separate-PC internet remain unverified. Evidence:build/verification/enemies38-batch/.

Protocol39 hardens enemy sharing across long sessions and initial health observations.
The lobby remains four players. Fresh sender IDs no longer exhaust a16-entry enemy
ledger; an unconfirmed first zero cannot claim a defeat. Confirmed native defeats
still synchronize. Validation:32 controlled native cases on Linux and Windows/Wine,
137 earlier Linux regressions (169 total),38 relay tests,23 mixed-platform cases,
52 patches and a six-minute replay with three lightweight peers. Natural campaign,
initialization routes, actual Windows hardware and internet remain unverified.
Evidence:build/verification/streams39-batch/. Use matching protocol45 builds.


Protocol40 adds conservative native defeat confirmation after a missed final health
sample. A previously tracked living instance must have shown a clear native defeat
bit before disappearing with that bit set. Existing boss/miniboss exclusions stay.
Validation:102 native checks each on Linux and Windows/Wine,24 mixed-platform cases,
38 relay tests,53 engine patches and a six-minute replay with three lightweight peers.
Native sword inputs kill a Bokoblin in both normal and debug-held observation cases;
the game performs damage/deletion/defeat flags. Full campaign, actual Windows hardware
and internet remain unverified. Use matching protocol45 builds on all four players.
Evidence: build/verification/defeats40-batch/.


Protocol41 shares all eight learned-song inventory items, including the Song of the
Hero parts and complete song. Full/locked pouches do not block songs. Menus, events,
temporary inventory and encounter guards hold updates; Multiplayer shows songs waiting.
Saved/live ownership repairs preserve other inventory and personal currency/ammo.
Validation:37 song cases plus32 cave checks each on Linux and Windows/Wine; Linux also
passes137 earlier inventory/recovery cases (206 Linux,69 Windows/Wine total). Full C,
39 relay cases,24 mixed-platform cases,54 patches and six-minute replays on both platforms pass.
Teaching/cutscene/campaign, actual Windows hardware and internet remain unverified.
Use matching protocol45 builds. Evidence: build/verification/songs41-batch/.


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
