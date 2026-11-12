#!/usr/bin/env python3
"""Reproduce lost scene incarnations against the exact prior receiver source.

Run inside the compiler CPU resource guard locally; hosted CI runs this under
its own limits. The owned baseline copy cannot modify the candidate source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE_NET = '9f4caedde6f8e0d37dc15e1ec06f0c118340609ec8d46cedbe43565181fdfb9d'
ADDITION = b'''            // Consumers may miss an intermediate departure, or receive a
            // same-area reload. Retire accepted targets and playout history too.
            p->presence_generation = ++presence_serial;
'''
EXPECTED_FAILURES = [
    'FAIL scene retires only the affected authenticated incarnation',
    'FAIL accepted target cannot survive a scene incarnation',
    'FAIL new scene does not interpolate through old pose history',
    'FAIL new scene starts its own replay watermark',
    'FAIL new scene target retains authenticated incarnation',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'build/source-checks/scene-baseline')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    client = out/'client'
    shutil.copytree(ROOT/'client/src', client/'src')
    (client/'tests').mkdir()
    fixture = client/'tests/scene_incarnation_test.c'
    shutil.copyfile(ROOT/'client/tests/scene_incarnation_test.c', fixture)
    net = client/'src/sst_net.c'
    data = net.read_bytes()
    if data.count(ADDITION) != 1:
        raise RuntimeError('Expected exactly one reviewed scene incarnation change')
    baseline = data.replace(ADDITION, b'', 1)
    if hashlib.sha256(baseline).hexdigest() != BASELINE_NET:
        raise RuntimeError('Baseline must reproduce the exact prior receiver source')
    net.write_bytes(baseline)
    make = (ROOT/'client/Makefile').read_text()
    sources = next(line.split(':=', 1)[1].split() for line in make.splitlines() if line.startswith('LIB :='))
    executable = out/'scene-incarnation-baseline'
    command = ['clang', '-O1', '-g', '-Wall', '-Wextra', '-std=c11', '-D_GNU_SOURCE',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-o', str(executable),
        str(fixture), *[str(client/name) for name in sources if name != 'src/sst_net.c'], '-lpthread', '-lm']
    subprocess.run(command, check=True, timeout=180)
    reproduced = []
    controls = []
    for mode in range(1, 7):
        for profile in (496, 497, 521):
            for phase in (1, 2):
                label = f'mode{mode}-profile{profile}-phase{phase}'
                result = subprocess.run([str(executable), str(mode), str(profile), str(phase)],
                    capture_output=True, text=True, timeout=20)
                (out/(label+'.log')).write_text(result.stdout+result.stderr)
                expected = mode <= 3
                errors = [line for line in result.stderr.splitlines() if line.startswith('FAIL ')]
                if result.returncode != int(expected) or errors != (EXPECTED_FAILURES if expected else []):
                    raise RuntimeError('Baseline failed for a different reason: '+label)
                record = dict(mode=mode, profile=profile, phase=phase, returncode=result.returncode)
                (reproduced if expected else controls).append(record)
    report = dict(passed=True, baseline_receiver_sha256=BASELINE_NET,
        candidate_receiver_sha256=hashlib.sha256(data).hexdigest(),
        fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
        reproduced_failures=reproduced, negative_controls=controls,
        failure_checks=EXPECTED_FAILURES, candidate_source_untouched=True,
        scope='Actual receiver/ownership/playout with synthetic frames; no sockets, Windows or game.')
    (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Scene baseline: {len(reproduced)} intended failures, {len(controls)} controls PASS; candidate untouched.')


if __name__ == '__main__':
    main()
