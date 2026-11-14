# Skyward Sword Together

Four-player co-op prototype for **Skyward Sword HD** on SkyRecomp 2.0.2.

[Published downloads](https://github.com/EpicCargo/skyward-sword-together-updates/releases) · [Current source](source/) · [Recent fixes and validation](source/CHANGES-2026-11-08.md)

[Remaining bugs and gameplay checks, in priority order](source/COOP-REMAINING.md)

[![Source and co-op checks](https://github.com/EpicCargo/skyward-sword-together-updates/actions/workflows/source-checks.yml/badge.svg)](https://github.com/EpicCargo/skyward-sword-together-updates/actions/workflows/source-checks.yml)

## Latest source update — November 14, 2026

The latest native prop checks verify one small pot: genuine pickup/putdown, steady remote hand alignment, movement, stale-carry floor restoration and remote placement. The placed pot also remains on the floor after the test peers depart. Carrying nameplates now sit above the pot instead of obscuring it. This is one actual WSL game with three lightweight peers; large pots, barrels, pickup/throw animations and independent full clients remain unverified. [Scoped prop evidence](source/validation/wsl-props-20261114.json).

Patch50 fixes concurrent Vulkan queue access during shutdown and serializes submit, present, idle and ImGui upload operations. The exact Linux runtime now closes normally while rendering, with zero reported validation messages. Peer test status files also publish atomically, preventing readers from seeing partial JSON. A separate 9,000-frame native run passed directional movement, three visible peers, stale-pose hiding/recovery, departure and fresh-ID rejoining. This remains one full game with simulated peers. A guarded Linux CI build now complements the Windows build; it packages no game files or substitute libraries. [Queue tests and native shutdown scope](source/docs/vulkan-queue-sync-test.md).

Patch49 fixes final command names in live test scripts with terminal line breaks. Patch48 adds Vulkan sampling for the fragment texture-handle form that blocked 72 opening-scene shader variants. The actual translator passes 34 focused cases; synthetic GPU tests pass 768 exact pixel checks across all 32 bindings and three mip levels, with zero validation warnings/errors. The private offline audit now compiles all 4,192 graphics stages. The exact patch48 WSL runtime also passed the opening, normal Knight Academy movement and a separate 24,000-frame loopback run with three rendered peer avatars and planned peer departure. Independent full clients and complete co-op remain unverified. Metal bindless sampling remains unsupported.

Patches39–47 also repair the rendering/build foundation, including copied vertex streams for WSL software Vulkan, shader-length validation and texture-handle constant data. The game CPU library already contains all four recompiled modules. [Current changes and evidence](source/CHANGES-2026-11-13.md) and [remaining acceptance checks](source/COOP-REMAINING.md) distinguish source checks from gameplay. Source pushes leave installed games and published binary downloads unchanged.

## Earlier source update — November 11, 2026

Patch32 parks a remotely carried prop before native simulation can resume between paused synchronization ticks, retaining packet replay protection. Its regression workflow checks the exact patch31 baseline and 42 pause/resume cases, alongside 138 deactivation cases across three object adapters and both host/guest IDs. The latest Windows runtime build and native gameplay checks remain pending. Earlier patch30 native runs stopped on memory pressure; pickup after remote placement and deactivation floor restoration remain unverified. The latest fully accepted gameplay evidence remains patch29.

- Reject incomplete, duplicate or mismatched portable runtime archives before activating a version.
- Preserve existing Windows release manifests and leave no partial version after a failed copy.
- Include the complete verified source inventory in default Windows source archives.
- Restore a trusted Windows dependency-cache backup offline, with complete checksums and preservation of existing files.
- Run native prop/carry/session, launcher/networking and relay checks automatically on GitHub.
- Reject ambiguous prop discovery beyond the 32-object capacity, reuse retired binding slots immediately, and discard bindings when an actor's identity changes.
- Check remote placement followed by a local pickup/putdown for both host and guest IDs, including delayed packets, floor restoration and a remote owner's departure.
- Recover pinned engine restoration when the original upstream is unavailable, while retaining exact tree and native-file verification.

[November 11 changes and validation](source/CHANGES-2026-11-11.md) also record the travel9 portable's successful final Windows package check and completed one-hour native Windows networking soak (359 player replacements). The earlier RAM-guard stop is retained in the history. The new local candidate includes the tested navigation runtime and complete matching source; published downloads and the update feed are unchanged.

The latest native binding fixes passed seven focused cases, 1,000 actor replacements at full capacity, an isolated Windows runtime cross-build and a native Windows gameplay run. The new runtime completed all eight carry/restoration/placement phases; 97 stable hall samples retained one unique pot actor instance. Owned processes and sockets closed normally.

An earlier baseline run independently passed with 77 valid hall samples. Each run used one full game with three lightweight peers. Separate full game clients remain untested. The accepted travel9 portable keeps its earlier runtime/source identity; the newly tested runtime has not been installed or publicly released.

## Co-op source update — November 8, 2026

- Fix launcher startup/cancellation races and clean up partially started room workers.
- Reclaim retired carried-object tracks and bind prop streams to the current authenticated player roster.
- Put destination coordinates before long arrival names and allow keyboard/gamepad focus to reach the travel button.

Windows hardware testing passed with **one actual game and three lightweight simulated peers**: four visible characters, room travel, genuine pot pickup/putdown, remote carrying and movement, stale-update recovery, invalid-context restoration, and remote placement. The final bounded run completed normally with no owned child processes or sockets left open.

Separate-PC multiplayer, all object types and start-to-finish campaign readiness remain unverified. Boss/miniboss synchronization stays off.

This update publishes source and validation records. Existing release downloads and the launcher's update feed are unchanged; the travel UI fixes are packaged in the accepted local travel9 candidate; no new public binary release was published.

The repository contains no game assets, generated game libraries, console keys, personal saves or credentials. Use your own game files with the published launcher.
