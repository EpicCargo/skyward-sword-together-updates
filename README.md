# Skyward Sword Together

Four-player co-op prototype for **Skyward Sword HD** on SkyRecomp 2.0.2.

[Published downloads](https://github.com/EpicCargo/skyward-sword-together-updates/releases) · [Current source](source/) · [Recent fixes and validation](source/CHANGES-2026-11-08.md)

[![Source and co-op checks](https://github.com/EpicCargo/skyward-sword-together-updates/actions/workflows/source-checks.yml/badge.svg)](https://github.com/EpicCargo/skyward-sword-together-updates/actions/workflows/source-checks.yml)

## Latest source update — November 11, 2026

- Reject incomplete, duplicate or mismatched portable runtime archives before activating a version.
- Preserve existing Windows release manifests and leave no partial version after a failed copy.
- Run native prop/carry/session, launcher/networking and relay checks automatically on GitHub.
- Reject ambiguous prop discovery beyond the 32-object capacity, reuse retired binding slots immediately, and discard bindings when an actor's identity changes.

[November 11 changes and validation](source/CHANGES-2026-11-11.md) also record the travel9 portable's successful final Windows package check and completed one-hour native Windows networking soak (359 player replacements). The earlier RAM-guard stop is retained in the history. The new local candidate includes the tested navigation runtime and complete matching source; published downloads and the update feed are unchanged.

The latest source adds CPU-checked native binding fixes, including seven focused cases and 1,000 actor replacements at full capacity. These fixes still need a full Windows runtime build and hardware gameplay test. The accepted travel9 portable keeps its earlier runtime and source identity.

## Co-op source update — November 8, 2026

- Fix launcher startup/cancellation races and clean up partially started room workers.
- Reclaim retired carried-object tracks and bind prop streams to the current authenticated player roster.
- Put destination coordinates before long arrival names and allow keyboard/gamepad focus to reach the travel button.

Windows hardware testing passed with **one actual game and three lightweight simulated peers**: four visible characters, room travel, genuine pot pickup/putdown, remote carrying and movement, stale-update recovery, invalid-context restoration, and remote placement. The final bounded run completed normally with no owned child processes or sockets left open.

Separate-PC multiplayer, all object types and start-to-finish campaign readiness remain unverified. Boss/miniboss synchronization stays off.

This update publishes source and validation records. Existing release downloads and the launcher's update feed are unchanged; the travel UI fixes are packaged in the accepted local travel9 candidate; no new public binary release was published.

The repository contains no game assets, generated game libraries, console keys, personal saves or credentials. Use your own game files with the published launcher.
