#!/usr/bin/env python3
"""Long-running hybrid-network check with four tiny peers and no game instances/builds."""
import argparse
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "client/tests"))
from udp_e2e import Process, start_hybrid_adapter, wait_for  # noqa: E402


def rss(processes):
    total = 0
    for process in processes:
        try:
            pages = int(Path(f"/proc/{process.p.pid}/statm").read_text().split()[1])
            total += pages * os.sysconf("SC_PAGE_SIZE")
        except (OSError, ValueError, IndexError):
            pass
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument("--out", type=Path, default=ROOT / "build/udp-soak")
    parser.add_argument("--bin-dir", type=Path, default=ROOT / "client/build")
    parser.add_argument("--actors", type=int, default=8, help="placed actors streamed by the elected owner (1–256)")
    parser.add_argument("--max-growth-mib", type=int, default=64, help="RSS growth limit; sanitizer builds have runtime overhead")
    args = parser.parse_args()
    if not 30 <= args.seconds <= 3600:
        parser.error("use a duration from 30 to 3600 seconds")
    if not 1 <= args.actors <= 256:
        parser.error("use an actor count from 1 to 256")
    args.out.mkdir(parents=True, exist_ok=True)
    host = Process([str(args.bin_dir / "host_fixture")])
    port = int(host.line())
    relay = adapter = None
    peers, events, samples = [], [], []
    start = time.monotonic()
    try:
        relay, adapter = start_hybrid_adapter(("127.0.0.1", port), profile="lan")
        for i in range(4):
            peer = Process([str(args.bin_dir / "udp_fixture"), str(relay.port), str(int(i < 3)), f"Soak {i}"])
            peers.append(peer)
            assert peer.line() == "ready"
        first = wait_for(peers, lambda s: all(x["online"] and x["players"] == 3 and x["tick"] > 2 for x in s))
        ids = [s["id"] for s in first]
        owner_index = ids.index(min(ids))
        peers[owner_index].command(f"actors {args.actors}")
        wait_for(peers, lambda state: all(s["motion_unique"] == args.actors
                 for i, s in enumerate(state) if i != owner_index), timeout=30)
        profiles = ["lan", "bad_wifi", "transatlantic", "mobile_tether", "hell"]
        last_profile = last_outage = -1
        last_durable = last_log = -100
        partition_until = 0
        last_ticks = [s["tick"] for s in first]
        advanced_at = [time.monotonic()]*4
        base_rss = peak_rss = rss([host]+peers)
        damage_count = 0
        while time.monotonic()-start < args.seconds:
            now = time.monotonic()
            elapsed = now-start
            profile_slot = int(elapsed//15)
            if profile_slot != last_profile:
                profile = profiles[profile_slot % len(profiles)]
                relay.set_profile(profile, seed=2400+profile_slot)
                events.append(dict(at=elapsed, profile=profile))
                last_profile = profile_slot
            outage_slot = int(elapsed//60)
            if elapsed > 5 and outage_slot != last_outage:
                direction = ["s2c", "c2s", "both"][outage_slot % 3]
                relay.partition(direction, True)
                partition_until = now+4
                events.append(dict(at=elapsed, outage=direction))
                last_outage = outage_slot
            if partition_until and now >= partition_until:
                relay.partition("both", False)
                partition_until = 0
            if elapsed-last_durable > 5:
                peers[0].command("flag 1")
                peers[0].command("reward")
                peers[0].command("damage")
                damage_count += 1
                last_durable = elapsed
            state = [p.stats() for p in peers]
            for i, s in enumerate(state):
                assert s["online"] and s["id"] == ids[i] and s["players"] == 3, (elapsed, state)
                assert s["queued"] < 131072, (elapsed, state)
                if s["tick"] > last_ticks[i]:
                    last_ticks[i], advanced_at[i] = s["tick"], now
                assert now-advanced_at[i] < 6, ("movement stalled", elapsed, state)
                if i and elapsed-last_durable > .5:
                    assert s["flag"] == 1 and s["rewards"] == 1 and s["damage_events"] == damage_count, (elapsed,state)
            resident = rss([host]+peers)
            peak_rss = max(peak_rss, resident)
            assert resident < 512*1024*1024, resident
            if elapsed-last_log >= 30:
                sample = dict(at=round(elapsed,1), profile=profiles[profile_slot % 5],
                              udp=[s["udp"] for s in state], ticks=[s["tick"] for s in state],
                              motion_unique=[s["motion_unique"] for s in state], rss_mib=round(resident/1048576,2))
                samples.append(sample)
                print(json.dumps(sample), flush=True)
                last_log = elapsed
            time.sleep(.25)
        final = wait_for(peers, lambda s: all(x["flag"] == 1 and x["rewards"] == 1 and x["damage_events"] == damage_count for x in s[1:]))
        final_rss = rss([host]+peers)
        assert all(s["motion_unique"] == args.actors for i, s in enumerate(final) if i != owner_index), final
        assert final_rss-base_rss < args.max_growth_mib*1024*1024, (base_rss, final_rss)
        summary = dict(passed=True, seconds=round(time.monotonic()-start,2), full_game_instances=0, actors=args.actors,
                       events=events, samples=samples, final=final, damage_events=damage_count,
                       initial_rss_mib=base_rss/1048576, final_rss_mib=final_rss/1048576,
                       peak_rss_mib=peak_rss/1048576, chaos_stats=relay.get_stats())
        (args.out / "summary.json").write_text(json.dumps(summary, indent=2)+"\n")
        print("UDP soak PASS", flush=True)
    finally:
        errors = []
        for peer in reversed(peers):
            try:
                peer.close()
            except Exception as error:
                errors.append(str(error))
        if adapter is not None: adapter.close()
        if relay is not None: relay.close()
        host.close()
        assert not errors, errors


if __name__ == "__main__":
    main()
