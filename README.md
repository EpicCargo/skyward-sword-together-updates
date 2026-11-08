# Skyward Sword Together

Four-player co-op prototype for **Skyward Sword HD** on SkyRecomp 2.0.2.

[Published downloads](https://github.com/EpicCargo/skyward-sword-together-updates/releases) · [Current source](source/) · [Recent fixes and validation](source/CHANGES-2026-11-08.md)

## Latest source update — November 8, 2026

- Fix launcher startup/cancellation races and clean up partially started room workers.
- Reclaim retired carried-object tracks and bind prop streams to the current authenticated player roster.
- Put destination coordinates before long arrival names and allow keyboard/gamepad focus to reach the travel button.

Windows hardware testing passed with **one actual game and three lightweight simulated peers**: four visible characters, room travel, genuine pot pickup/putdown, remote carrying and movement, stale-update recovery, invalid-context restoration, and remote placement. The final bounded run completed normally with no owned child processes or sockets left open.

Separate-PC multiplayer, all object types and start-to-finish campaign readiness remain unverified. Boss/miniboss synchronization stays off.

This update publishes source and validation records. Existing release downloads and the launcher's update feed are unchanged; the new travel UI fixes have not yet been packaged into a new release.

The repository contains no game assets, generated game libraries, console keys, personal saves or credentials. Use your own game files with the published launcher.
