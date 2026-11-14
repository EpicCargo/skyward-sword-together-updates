# In-process prop presentation counters

Set `SST_PROP_TRACE=1` on an isolated development runtime to log the prop bridge's counters. The trace runs on the existing game-thread bridge path under its mutex. It reads no extra actor memory, retains no actor addresses and changes no packet, ownership or presentation decision. It is off by default.

Each `[sst-props]` line contains the monotonic `at` time, bridge `enabled`/`discovered` state, `last_bindings`, and cumulative `received`, `denied`, `carried`, `placed`, `parked`, `owner` and `sequence` counters. Logging occurs at most once per second, with an immediate record after bridge reset or clock rewind. The presentation counters retain their existing process-lifetime semantics; `last_bindings` describes the last discovery walk.

`received` counts admitted packets, rather than bytes arriving on a socket. Increasing `denied` means a carry could not use an actual rendered carry body. Increasing `carried` or `placed` means the validated native presentation helper completed. Those counters establish execution paths; reviewed game images are still needed for object visibility and hand alignment. A sent packet or a prepared avatar pose alone is insufficient.

The actual bridge fixture passes its six presentation/local-holder scenarios across small pots, large pots and normal barrels with tracing off and on, using address, undefined-behavior and leak sanitizers. The trace-on fixture retains identical behavioral check output after removing diagnostic lines. This is synthetic CPU evidence; large pots/barrels, throws/breaking, independent full clients and complete hand animation remain native acceptance gates.
