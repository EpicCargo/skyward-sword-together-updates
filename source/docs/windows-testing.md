# Windows friend and internet testing

Current WSL continuation (2026-11-07): the experimental **network5 portable
candidate** is saved under
`D:\Skyward Sword Together WSL2\candidates\2026-11-07-Windows-network5-candidate`.
Use that candidate and its `READ-ME-FIRST.txt` for this continuation; standalone
ZIP names and build revisions below describe earlier deliveries.

The exact portable ZIP has passed clean extraction and file hashes on real
Windows, local update-channel selection, portable save-directory defaults,
launcher authentication and process shutdown. Its packaged compiler built and
ran an asset-free C sample, and its generator passed startup checks. Actual
Windows packaged Python/C host/C receiver also passed three minutes over verified
local TLS with the Worker shim. A separate Linux networking hour passed. Three longer Windows attempts stopped on free-RAM guards; the last reached
48 minutes of verified traffic with 28,756 prop checks and 288 replacements.
A full Windows networking hour remains unaccepted; interrupted attempts do not
count as passes. These checks do not establish real game setup, Windows graphics,
a separate-PC internet session or campaign compatibility. The earlier Wine
results below remain historical evidence. The user's game files remain on their
untransferred Linux partition.

The complete modified runtime now uses official **SkyRecomp 2.0.2** with SST's changes and builds as a Windows x64 executable. Full first-run game compilation,
Vulkan bedroom gameplay and native quest-reward delivery from a Linux socket peer pass under
Wine. Windows/Linux client and host networking tests also pass. An actual Windows PC and a
separate-PC internet session remain unverified. Both players need matching mod builds and their
own SSHD v1.0.0 files.

Renderer revision **hands47** keeps wire protocol 45. Six verified hand variants now share observed wrist poses, so a local idle hand can follow a remote roll, jump, sprint or supported sword pose. Both hands pass per-part hit/miss checks. Geometry and finger shape still follow local Link; independent equipment/outfits and unseen poses remain unfinished. Linux and Windows/Wine each pass 64 renderer/recovery checks. Older deliveries remain preserved.

World revision **scenes48** retains renderer **hands47** and wire protocol45. A bounded local journal preserves proof of previously admitted scene changes and replays only still-owned, currently allowed flags. Sixteen controlled native recovery checks pass on Linux and Wine; natural save/exit/relaunch campaign routes and real Windows/internet testing remain unverified.

Vase revision **props58** adds ordinary-vase carry and put-down sharing through
updated built-in hosts. Use matching props58 builds on every PC and leave
**Share carried vases** enabled. Test one player lifting, walking with and placing
a small vase, then have the other player pick it up at the new location. Large
pots, throw/break events, simultaneous pickup and cross-area object lifetime are
unfinished. A cold animation cache can still show neutral arms until that player
has observed the carry pose locally; missing hands are a separate remaining bug.

## Install the Windows test build

For co-op, select **Settings -> Video -> Frame rate -> 60 FPS** on every PC. During
the first two-PC internet playtest, this improved user-reported fast movement,
stamina drain and remote stutter. Experimental 120 FPS is not validated for multiplayer.

Prefer the Update & Play launcher. The standalone delivery is
`build/dist/SST-Windows-coop-protocol45-props58-final.zip`. Extract the entire ZIP into a fresh folder on
Windows, open `skyrecomp.exe`, select your prepared v1.0.0 folder and use **Set up game files**.
The prepared `exefs/` and `romfs.bin` assets are reusable across Linux and Windows. The Linux
executable and `skygame.so` are native Linux binaries; Windows needs its own executable and
`skygame.dll`. The ZIP includes a portable Windows Clang compiler and setup tools that build that
DLL from your files with one compiler job. First setup can take tens of minutes; subsequent
launches reuse the finished library. It includes no game assets, precompiled game code, console
keys or saves.

Requirements: Windows 10/11 x64, an AVX2-capable CPU and a GPU driver supporting Vulkan 1.3.
After setup, press **PLAY** and open **ESC > Multiplayer**. Use test saves for this experimental
build. Preserve `skyrecomp.log` before reopening after a failed run.

## Check the internet connection first

One option is to put both PCs on the same Tailscale network, following its
[official quickstart](https://tailscale.com/docs/how-to/quickstart). Use the host PC's Tailscale
IPv4 address. The alternative is forwarding TCP 7623 (and UDP 7623 for optional movement) on the host's router to the host PC and
allowing that port in the host firewall. The browser preview on 8765 is separate and stays local.

On the Linux host, launch `scripts/play.sh`, open Multiplayer and press **Host game**. The default
port is 7623. Leave Room and Team blank for a first connection check, or give your friend their
exact values. Keep the hosting game open during the check.

Movement uses UDP when **Use UDP for movement** is enabled and the route works; it falls back
to TCP automatically. Add `--udp` to the checker command to test this optional channel too.
The checker reports TCP success even when UDP is unavailable, since that connection can still play.
Story progress, rewards, damage and recovery always use TCP. Enabling UDP does not create router
port forwarding or automatic NAT traversal.

Use `sst-network-check.exe` included in the Windows co-op ZIP. In PowerShell inside the
extracted folder, replace the example address with the host's reachable IPv4 address:

```powershell
.\sst-network-check.exe 100.101.102.103:7623
```

For nonempty Room and Team values:

```powershell
.\sst-network-check.exe 100.101.102.103:7623 "Our room" "Our team"
```

PASS means the host accepted the protocol, Room and Team and answered a heartbeat. The checker
briefly occupies one player slot, then disconnects. It sends no scene, poses, shared progress or
enemy commands, and requires no game files or keys. A successful check establishes connectivity;
it does not validate Windows game rendering or a co-op playthrough.

If it reports refusal or timeout, check that Host game is active, the address/port is correct,
and the VPN/firewall/router permits the connection. A protocol mismatch requires matching builds;
a Room or Team rejection requires matching those fields.

## First co-op game test

The host chooses Host game; the friend chooses Join game with the same address used by the checker.
Use test saves at similar story progress, enter the same stage, room and layer, and first check
movement and ordinary enemies. Each machine learns observed avatar poses locally. Unobserved
poses still use the local animation, and outfit/equipment replication is incomplete. Boss and
miniboss synchronization remains off. Recovery offers team resync, area reload and catch-up at
a teammate's area entrance. Full story routes and separate-PC internet play remain unverified.

## Developer checks

`python3 scripts/build-windows-network.py --cc /path/to/x86_64-w64-mingw32-clang` compiles the
checker and small tests. It needs a MinGW compiler with winpthreads; it creates no game executable.
`python3 scripts/test-windows-network.py` runs them in `build/wine-network-test`, an isolated
Wine prefix, with two Windows/two Linux peers and both host platforms. Build the Linux C tests
first. The script stops only that prefix's Wine server after testing.

`python3 scripts/build-windows.py` builds the full runtime with one job in `engine/build/windows`
and packages the ZIP separately from Linux objects. It needs the cached LLVM-MinGW toolchain,
Vulkan headers/VMA from `build-engine.sh`, and compatible public SDL3/FFmpeg/shaderc C headers.
The verified build used SDL 3.4.18, FFmpeg 9.0.2 and shaderc 2026.4.1. Support DLLs and the generator helper
are selected from the official upstream v2.0.2 Windows release with byte ranges and per-member
CRC/length checks; the large texture pack and upstream precompiled game code are excluded.
`--no-fetch` reuses cached support; `--package-only --no-fetch` repackages an existing build.
Existing ZIPs are preserved. For another build of the same protocol, choose a fresh path,
for example `--output build/dist/SST-Windows-coop-protocol45-candidate1.zip`.
The builder rejects an existing output before fetching or compiling and publishes completed
archives without replacing a delivery created by another build.
The isolated full-runtime Wine smoke test uses `build/wine-runtime-test`, separate from normal
Wine applications. The full setup check was interrupted at its 35-minute test limit and then
resumed: 1,346 compiled units were reused, and setup finished after another 204 seconds. The
generated DLL loaded and reached F001r bedroom gameplay. Kina and Peatrice grants increased
crystals 0→5→10; the opposite Item Check receipt added nothing. This establishes controlled
setup/gameplay compatibility under Wine. Actual Windows hardware, full co-op routes and internet
coverage still need testing.

The Windows package now prefers a bundled LLVM-MinGW Clang compiler. The former bundled Zig
compiler failed its first real compile under Wine with `manifest_create Unexpected`, although
its version command worked. The native Windows Clang toolchain is pinned to release 20261020
and checked against SHA256 `fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd`.
The package includes its x64 C compiler/linker, public headers and runtime libraries. No compiler
installation or PATH changes are needed. `LARECOMP_CC` remains an explicit override; older
packages can still use the legacy Zig fallback.

Generated units that spend thirty seconds in optimization are retried once at `-O0`.
The successful fallback choice is recorded in resume stamps. Large units retain their existing
unoptimized path. Each Windows build child has a separate job object, so stopping one compiler
ends its descendants and leaves unrelated jobs alive. The regression in
`engine/tools/test_windows_process.c` verifies cancellation, normal-exit cleanup and unrelated
process survival under Wine.


Protocol 23 also passes the same queue-backlog suite with a Windows PE client under
Wine, including automatic progress/health refresh. The final extracted protocol 23
runtime reached normal bedroom gameplay and granted Beedle/Owlan packs 0→5→10 from
a Linux socket peer. It reused the previously generated private game DLL (same library
ID); the full first-setup compiler check above was performed in the protocol 22 batch.
The builder and bundled compiler are unchanged. No real Windows PC or internet session
was used in these checks.

Protocol 24 adds optional UDP movement with TCP fallback. The extracted runtime also reached
normal bedroom gameplay with three simulated UDP teammates visible, switched those peers to TCP
during a one-way UDP outage and recovered afterwards. Tests remain limited to Wine on this Linux
machine; an actual Windows PC and separate-PC internet session are still unverified. The network
suite includes mixed Linux/Windows UDP hosts and clients, reliable progress/rewards/damage during
outages, delayed ACKs, stale scene/connection rejection, queue saturation and reconnects.

Protocol 25 additionally verifies Cawlin’s letter reward transactions in the extracted Windows
runtime under Wine. Either teammate ending uses the recipient’s personal hand-over choice and
awards one pack. No choice or conflicting choices waits with an explicit reason. Eighteen
controlled native checks cover source discovery, native map deferral, follow-up repair, capped
grants, replay and area reload, plus four older NPC transactions. These use isolated saves and
seeded prerequisites, not a complete ordinary quest route. Three UDP teammates also render in
normal gameplay; forced one-way loss falls back to TCP and UDP recovers. The game-free mixed
Windows/Linux suite verifies all 256 bridge actors and queue expiry/fairness. Actual Windows
hardware and a separate-PC internet campaign remain unverified.


Protocol 27 additionally passes 38 controlled native checks in the extracted runtime under
Wine: Potion Lady's first bottle, full/locked pouch waits, live/saved receipt repair,
Sealed Temple potion contents and native use, no resync refills, saved bottle-catalogue
recovery, and reload/catch-up persistence. A separate check visits Sealed Temple's original
room and verifies its native potion chest metadata and actor removal after remote delivery.
The same checks pass on Linux. The private game library is reused with the same library ID;
first-setup compiler behavior is unchanged. No actual Windows PC or separate-PC internet
campaign was used. Evidence: `build/verification/pouch-gifts-batch/`.


Protocol 28 passes 60 controlled native checks under Wine, adding Batreaux's seven reviewed
threshold rewards, four wallet tiers, extra-wallet preservation, ownership/completion repair,
heart-piece effects, 300/600-rupee bundles and bulk delivery across native capacity commits.
A combined burst verifies all Batreaux rewards plus Mallara's crystal/rupee bundle without
currency clipping. Linux passes the same checks. This reuses the same private game library
ID; setup compiler behavior is unchanged. Evidence: `build/verification/rewards28-batch/`.
Actual Windows hardware and separate-PC internet/campaign coverage remain unverified.


Protocol 29 passes **93 controlled native checks under Wine**, matching the complete fresh
Linux run: 87 receipt/pouch/quest/recovery checks plus actual Cursed/Life Medal behavior,
Small Quiver consumption/resync and removal of the original Batreaux medal chest. All seven
medal and nine spare ammo pouch item IDs have native quantity/delivery coverage. The tested
Windows executable is the same byte-for-byte executable in the final protocol30 ZIP; the
package manifest, source/42 patches and privacy exclusions are checked separately. Final
sampled test-group RSS: 5163.4 MiB under the 6144 MiB limit. One full game runs at a
time, without compilation. Evidence: `build/verification/pouch29-batch/`. Actual Windows
hardware, separate-PC internet play and full natural quest/campaign routes remain unverified.


Protocol 30 passes **123 controlled native checks under Wine**, matching the complete final
Linux run. New coverage includes capacity delivery with a full pouch, saved/live ownership
and capacity repair, preserving personal upgrades, eight-slot limits, bulk expansion/item
replay, invalid/restricted inventory holds, reload/catch-up, native removal of the original
Goddess expansion chest and actual Life Medal/potion behavior in slot 8. Native process-group
peak: **5169.3 MiB**, with a 6144 MiB cap and no concurrent full game or compilation.
The final ZIP contains the same tested executable plus matching source/43 patches; manifest
and privacy checks are separate. Evidence: `build/verification/pouch30-batch/`. Pouch shop
sharing, crafting, real Windows PCs and internet/full campaign routes remain unfinished.

## Beedle purchase batch (protocol 31)

The latest build shares the Life Medal and heart piece through original purchase
receipts 814 and 813. Test leaving and returning to Beedle's shop: shared purchases
wait while the current stock actors are active, and the next visit reconstructs
sold-out stock. The recipient keeps their rupees. A full pouch holds the Life Medal;
completed receipts repair their live/saved copies without recreating stored inventory
or healing again. Controlled Linux/Wine results are recorded in
`build/verification/shop31-batch/`; actual Windows hardware and internet play still
need separate tests.

For a later four-player manual shop test:

1. Use matching protocol45 builds, Room and Team. Keep one recipient inside
   Beedle's shop and another outside with a full pouch.
2. Have the buyer purchase the Life Medal normally. Check the buyer's normal
   charge. The other players keep their own rupees. The player inside sees a
   pending shop reward; the player with a full pouch waits for a usable free slot.
3. Leave the shop or free a slot, then return to normal gameplay. Each recipient
   should receive one medal. Returning to the shop should remove its medal offer.
4. Buy the heart piece and check the recipient's remainder/health behavior.
   Repeat resync and area reload after storing a medal and taking damage: neither
   should create an extra item or heal again. Re-enter to check sold-out stock.

The automated fixtures inject purchase proof and inspect native stock actors; this
manual route is still needed to validate real purchase dialogue and separate PCs.

Protocol31 validation: 136 controlled native checks each on Linux and Windows/Wine,
including thirteen new Beedle purchase/stock cases. The 12-minute four-player native
lobby uses one game plus three lightweight peers. All31 relay tests,19 mixed-platform
network cases, two engine regressions and44 engine patches pass. Evidence:
build/verification/shop31-batch/. Actual Windows hardware/internet and full natural
routes still need testing. Read beedle-purchases.md in the Windows folder
(docs/beedle-purchases.md in source) for supported shop purchases.

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


## items59 collector-only testing

Only the collector receives rupees/consumables. Teammates' supported fixed pickups
are removed; their wallets, health, stamina, ammo and materials are unchanged.
Normal non-quest barrels can be carried/placed. This does not finish all-object
sync: dynamic dropped-actor matching, crates, bombs, respawns, specialized capture
sources and several quest object lifecycles remain unfinished. Hands remain a
separate incomplete renderer issue. Use matching builds and60FPS on everyPC.
