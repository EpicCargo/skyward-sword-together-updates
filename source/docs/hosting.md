# Four-player hosting

The game has a built-in host. One player hosts and up to three friends connect to that player's PC.
Traffic between guests passes through the hosting game; this is a player-hosted star topology.
No Node.js process or dedicated server is needed. Full mesh networking, automatic NAT traversal and
host migration are not implemented.

Set **Settings -> Video -> Frame rate -> 60 FPS** on all PCs before testing co-op.
The first real two-PC internet session reported better running/stamina timing and
smoother remote movement after both players selected 60 FPS. Experimental 120 FPS
is not validated for this session. Movable props such as carried or repositioned
vases remain local and are not yet replicated.

1. Launch `scripts/play.sh` and open Multiplayer in the settings menu.
2. Enter your name. In the built-in host, a blank Room and `public` select the same default room.
   Other room names remain case-sensitive and must match exactly. Team values must also match.
3. The host presses **Host game**. The default port is **7623** (TCP, with optional UDP movement).
4. Friends enter the host's reachable IPv4 address, such as `192.168.1.50:7623`, and press **Join game**.
5. Load your own saves and enter the same stage, room and scene layer to see each other.

For LAN play, use the host PC's LAN address. Across the internet, the host address must be reachable
through a VPN or a forwarded TCP port. For UDP movement, allow/forward **UDP on that same port**
as well. Multiplayer's **Use UDP for movement** option defaults on; the status shows UDP or TCP.
If UDP is blocked or stops responding, movement falls back to TCP and retries UDP automatically.
Story progress, rewards, damage and recovery always use TCP. The mod does not open router ports automatically.
Disconnecting the host ends the session. Guests can reconnect while the host is still running.

The Multiplayer tab now shows whether this PC is hosting or joining and how many other
players are present. **Online** with zero other players does not prove your friend joined
the same host. **ESC -> Developer** shows connection and avatar diagnostics; Copy diagnostics
copies a bounded report to your clipboard for troubleshooting. It does not upload anything.

This is a prototype: remote Links use the local character's outfit/equipment. Observed locomotion
and finite sword poses can replay independently; unobserved poses still use local animation.
Casual and hero-tunic bodies have been verified, including Goddess/True Master Sword captures.
Filtered story/equipment progress, area switch changes and
ordinary enemy damage now feed each player's game. Each player keeps their own save. Bosses and
minibosses remain independent, and full-game synchronization is incomplete. Use matching builds and
saves from the same campaign. See [world-sync.md](world-sync.md) for coverage, backups and recovery.

Enemy movement follows one player in each area and passes to another when that player leaves or
enters a cutscene. **Share regular enemy movement** can be toggled separately in Multiplayer.
All players need the updated protocol 45 build; earlier protocols are rejected during connection.
The full Windows runtime test package now builds and opens its Vulkan multiplayer menu under Wine.
Windows gameplay has been tested under Wine. Separate-PC internet play and an actual Windows PC
remain unverified. See [Windows setup and internet testing](windows-testing.md) for the ZIP and checker.

## Optional standalone relay

The original Node relay remains available for development and compatibility:

```sh
node server/relay.js            # TCP port 7623
PORT=9000 node server/relay.js  # another port
```

Enter its address with **Join game**. The four-player limit applies to the built-in host; the legacy
relay retains its existing room behavior and uses TCP even when the UDP option is on. Neither transport currently provides encryption or account
authentication. Room names separate groups; they are not passwords.


The Multiplayer tab includes host/relay round trip, last-packet age and send-queue size for
connection diagnosis. Paused map/inventory screens hold recovery until they close. If a recovery
request times out, close the game menu and request it again.

For a connection check without game files, run `client/build/network_check HOST:7623 ROOM TEAM --udp`
on Linux, or `sst-network-check.exe HOST:7623 ROOM TEAM --udp` from the Windows ZIP. Omit Room and
Team when blank. The optional UDP result reports bidirectional reachability or TCP fallback.
`scripts/avatar-peers.py --udp` can exercise three simulated teammates alongside one game;
these simulated peers do not play quests or prove a four-person campaign.


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
