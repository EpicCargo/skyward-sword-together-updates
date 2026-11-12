# Native disconnect observation

The development fixture uses one full Windows game and one Python process that
owns three scalar peers. Its original observer stops the owned job if the peer
process exits before the final game capture. A normal own-client Disconnect closes
the embedded host, so immediate peer exit prevented subsequent native floor
recovery from being observed.

`validation/expected_disconnect.py` provides an explicit fixture intent, not a
production networking option or gameplay pass. Before host EOF, the controller
may arm one request with kind `native-own-disconnect`, a positive request ID and
an integer duration of 1–90 seconds. Repeated intent cannot extend the deadline;
changing the request is rejected. The actual adapter additionally requires a
fresh native source and an owned profile without a previous final capture.

When an armed host EOF occurs, the actual adapter closes all three TCP sockets
in its normal cleanup path. Its process then waits within the original deadline
for that profile's fresh final game capture. The original observer, job ownership,
heartbeat, memory limits and cleanup remain in effect. Unarmed or expired EOF,
protocol errors, missing capture and timeout still fail. Socket observation alone
never establishes native gameplay acceptance. Peer pose fields retained after EOF
are historical; only fresh direct native observations can establish floor recovery.

Nine helper contract cases pass normally and optimized. Four real-TCP cases in
each mode exercised the actual modified scalar adapter with three connections:
armed EOF/capture, unarmed EOF, armed EOF without capture, and malformed protocol
while armed. Those adapter checks retained Python `-I`; its one explicit helper
is loaded from its frozen owned file without adding import paths. A Windows
isolated-Python file-lock self-test and the actual native Disconnect run also
passed. The adapter source and detailed private fixtures are preserved in the
source handoff; the public scalar receipt identifies their hashes in
`validation/native-disconnect-fixture-20261111.json`.

The native run independently verified the focused Disconnect button, the UI's
Disconnected state, native Gear pause, subsequent unpaused same-actor floor
samples, final capture and normal process exit. It restored four original stage
files and removed its exclusively created helper. See
`validation/patch32-native-disconnect-20261111.json` for the exact runtime/source
identity and scoped result. Reconnect, room/cutscene/death changes, independent
full clients and campaign readiness remain separate tests.
