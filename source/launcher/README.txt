Skyward Sword Together — Update & Play

Players
Extract the entire SST-Launcher-Windows.zip once and open SST Update and Play.exe.
On Linux, extract SST-Launcher-Linux.zip and open Start SST.sh (Python3 required).
The local launcher page has Update & Play, Play installed build, Open game setup,
and a rollback action. Choose the prepared game folder once; game files and game
code libraries are never included. First Windows setup happens in the native SST
window using the bundled compiler. No Python installation or GitHub account is
needed on Windows.

To retain older Windows SST data, choose the existing extracted SST folder containing
skyrecomp.ini and save/ as the save/settings folder. The updater does not move or
delete it. Do not choose a managed versions/ folder for game files or saves.
Linux defaults to the existing ~/.local/share/skyward-sword-together data location.

Launcher5 offers 1, 2, 4 or 8 compiler jobs under Game folder & settings. It defaults
to one compiler to keep memory use down. Suggested starting points: 8 GB RAM -> 1,
16 GB -> 2, 32 GB or more -> 4. More jobs also need enough CPU cores and available
memory; reduce the setting if the PC struggles. The advanced 8-job choice is for
32 GB+ PCs with at least 8 logical processors and enough available memory. More
jobs do not accelerate every stage: reading/code generation, a slow final file
and linking can still limit speed. Eight jobs have not been speed-benchmarked.
Save settings before Open game
setup. The choice applies when SST next starts, not to an already running build.
Existing completed game libraries are reused. If setup is interrupted normally,
completed objects with matching stamps can be reused from the same game's
.gamebuild folder; preserve that folder. No speedup factor has been benchmarked.

For co-op, use Settings -> Video -> Frame rate -> 60 FPS on every PC. The first
two-PC internet playtest reported improved running/stamina timing and smoother
remote movement at 60 FPS. Experimental 120 FPS is not validated for multiplayer.
Reviewed ordinary vases/barrels support carry and floor placement. Supported fixed
pickups disappear for everyone and reward only their collector. Dynamic drops and
many other object types are unfinished. A controlled Linux test verified independent
remote body animations and visible hands in the tested outfit. Other outfits and
independent sword equipment remain incomplete.

For room-code play, close the game first. The host clicks Host & Play and shares the
six-character code; friends enter it and click Join & Play. No router forwarding,
VPN, GitHub account or Cloudflare account is needed for this mode. Leave the launcher
running while playing; closing its browser tab is fine. Leave room or Quit launcher
ends that online connection while the game can continue offline. Rooms expire after
six hours, and the free testing service has daily limits. A new room requires
starting from the launcher again. Existing direct Host/Join remains available.

The update channel is:
https://github.com/EpicCargo/skyward-sword-together-updates/releases/latest/download/sst-updates.json

The initial bundle includes a verified runtime. Future versions can download only
changed files when a compatible patch exists. Otherwise a full runtime is fetched
automatically. The old runtime remains available for rollback. Rollback never alters
or restores game saves. Keep the same runtime build/protocol as your friends.
Play installed build is available when the channel is offline. Room-code play uses
the configured SST online service; an update-channel outage does not erase saves.

Publisher (after an SST batch has passed native verification)
One command builds the feed, full packages, small patch and fresh launcher bundles,
then publishes a draft release and makes it live after every asset is uploaded:

python3 launcher/release.py --verification-dir build/verification/YOUR-VERIFIED-BATCH --id YYYY.MM.DD-YOUR-BUILD --sequence YOUR-MONOTONIC-NUMBER --out build/updates/YOUR-NEW-OUTPUT --publish

Omit --publish to prepare everything locally. Never reuse a published output folder
or tag. The script checks the input ZIP and Linux binary against saved verification.
It reuses GitHub CLI authorization; it never stages, commits or pushes this worktree.
Only public runtime/tools/source packages go into the release directory. Keep game
assets, keys, generated game libraries, saves, journals and local settings out.

Implementation/limits
Standard-library Python server bound only to127.0.0.1; random per-window token and
same-origin checks protect actions. HTTPS is required for online update sources;
loopback HTTP is allowed for tests. Download and per-file SHA256 checks, path and
symlink rejection, versioned staging and atomic active-version switches preserve
previous installs. No package can target user data through the managed update path.
This is a testing launcher. Windows PE logic/bootstrap are tested under Wine; actual
Windows hardware and complete multi-PC/campaign play remain unverified.
