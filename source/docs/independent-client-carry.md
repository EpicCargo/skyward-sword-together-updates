# Independent full-client carry checks

Two complete Linux game processes now connect with independent saves, caches,
settings, displays and animation services. The first run completed 12,000 frames
per client with both game exits zero, no owned games left, and 3,744.3 MiB peak
combined RSS. These are real clients on the same WSL machine, not virtual
machines, scalar peers or separate PCs. See the scoped
[receipt](../validation/wsl-full-clients-20261114.json).

## Reproduced missing carry pose

The host picked up small pot119 normally. The guest had never picked up a pot.
Both were in the Academy hall, F001r/room0/layer1/entrance2. The guest received
the host's carry descriptors, but replay used `ordinary-neutral` with zero
cached carry parts. The guest showed the host standing normally and no held
pot. At the reviewed cold snapshot, the receiver had admitted 2,819 descriptors
but recorded 3,006 carry denials and zero carry presentations. These counters
measure different events and must not be subtracted or treated as packet loss.

The host's normal putdown then produced placement updates at the guest. A later
guest action67 was visibly a quest-barrel pickup, excluded from prop sharing;
it does not establish small-pot handback or warm-cache success. Raw harness
gameplay acceptance remains false. This is a reproduced limitation, not a
completed synchronization fix.

Earlier scalar-peer runs used a full client that had already performed a local
small-pot pickup. They validate steady alignment with a learned local pose;
they do not cover an independent cold observer. The animation service copies
locally observed skeletal palettes. A remote semantic carry identity alone
cannot supply the missing skeleton pose. CPU recompilation is already complete
and recompiling the same game code does not change this cache limitation.

## Regression and next implementation

`carry_cohort_test` now explicitly follows ordinary idle, repeated remote carry
requests, an unsupported native held identity, verified local small carry, and
local putdown. Receiving remote requests must not teach a pose. Unsupported
barrels must not seed small-pot carry. A verified same-scene carry snapshot must
remain usable after the viewer puts their own object down. CI runs this actual
animation service fixture under address, undefined-behavior and leak checks.
The fixture documents current behavior; it does not generate a cold carry pose.

## Learned-pose control and handback

A second run completes18,000frames per client in1,367.1seconds, both exits zero,
with3,749.7MiB peak combined RSS and no owned games left. The same runtime is
unchanged. The host holds pot119 while the
guest picks up a different small pot117 normally. After that genuine local
pickup, the guest learns all15 carry parts and displays the host carrying119.
Both distinct held pots are visible, and each client applies the other's carry.
The guest continues displaying the host's carried119 after putting its own117
down. Both floor pots are later visible in the host's view.

The guest also picks up the host's placed119 while both clients remain connected.
An NPC-dialogue approach is excluded. The existing same-area developer go-to-player
action supplies positioning, then normal A controls perform the native pickup
and putdown. Leftover dialogue controls cause two source CARRY/PLACE cycles; the
first held state has a reviewed native `Put down` image, while the second cycle
has source-phase evidence only. The host's carry presentation total rises
from1,378 to1,510 during these intervals. No independent actor-lifetime census
was performed. See the [warm-control receipt](../validation/wsl-full-client-warm-20261114.json).

This confirms the missing local pose as the cold-observer blocker. It does not
resolve it for a new player, establish full pickup/throw animation, or verify
simultaneous grabs of the same object.

The next runtime work is an independent render pose source that evaluates the
owned game's local animation resources without changing the player's native
held links or learning from forced viewer draws. It must match all required
body parts, preserve the held-object identity and provide the same wrist frame
to prop placement. Native acceptance requires a cold observer, a learned-pose
control, reverse roles, then pickup/putdown transitions and contention. Do not
substitute an ordinary standing body while claiming correct carried-hand
alignment, or accept a quest barrel from action67 alone.

## Concurrent test ownership

Existing single-game guards retain their original limit. This private paired
fixture uses a separate guard: empty game inventory before launch, at most two
distinct Linux game PIDs in the newly created owned process group, no Windows
game, fresh request-correlated Windows telemetry, 3 GiB free on both operating
systems, 12 GiB aggregate RSS and a finite 30-minute deadline. Third games or
unrelated groups stop only the owned test. Eight focused guard contracts passed.
Both clients use the exact accepted patch51 runtime and real verified libraries.
Game files, saves, shaders, palettes, screenshots and prop matrices remain
private; public evidence contains scoped counts and hashes.
