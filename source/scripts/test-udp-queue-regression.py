#!/usr/bin/env python3
"""Verify UDP saturation/fairness and prove the regression catches five broken senders."""
import argparse
import json
import os
from pathlib import Path
import resource
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/udp-queue-regression')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for proc in Path('/proc').iterdir():
        try:
            if (proc / 'exe').resolve() == ROOT / 'engine/build/skyrecomp':
                raise RuntimeError('Stop the SST game before compiling regression fixtures')
        except OSError:
            pass
    source = (ROOT / 'client/src/sst_net.c').read_text()
    tail = '\n    }\n    pthread_mutex_unlock(&mu);\n}\n\n// Flag values'
    assert source.count(tail) == 1
    mutations = {
        'capacity64': source.replace('SST_ENEMIES + 1', '65').replace('SST_ENEMIES+1', '65')
            .replace('motions>=SST_ENEMIES', 'motions>=64'),
        'discard-unsent': source.replace(tail, '\n        udp_clear_live();' + tail),
        'unfair-cursor': source.replace('udp_cursor=(slot+1)%(SST_ENEMIES+1)', 'udp_cursor=slot'),
        'keep-stale': source.replace('if ((int32_t)((uint32_t)now-live->updated)>SST_MOTION_TIMEOUT_MS)',
                                     'if (false)'),
        'expire-future': source.replace('(int32_t)((uint32_t)now-live->updated)',
                                       '(uint32_t)((uint32_t)now-live->updated)'),
    }
    libraries = next(line for line in (ROOT / 'client/Makefile').read_text().splitlines()
                     if line.startswith('LIB :=')).split(':=', 1)[1].split()
    fixture = (ROOT / 'client/tests/udp_queue_test.c').read_text()
    results = {}
    for name, code in {'fixed': source, **mutations}.items():
        assert name == 'fixed' or code != source
        implementation = out / (name + '-net.c')
        implementation.write_text(code)
        test = out / (name + '-test.c')
        test.write_text(fixture.replace('../src/sst_net.c', str(implementation)))
        executable = out / name
        command = shlex.split(os.environ.get('CC', 'clang')) + [
            '-O1', '-g', '-std=c11', '-D_GNU_SOURCE', '-I' + str(ROOT / 'client/src'),
            '-o', str(executable), str(test),
            *[str(ROOT / 'client' / path) for path in libraries if path != 'src/sst_net.c'],
            '-lpthread', '-lm']
        with (out / (name + '-compile.log')).open('w') as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        result = subprocess.run([str(executable)], capture_output=True, text=True,
                                timeout=15, preexec_fn=no_core)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        if name == 'fixed':
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode == -6 and 'Assertion' in result.stderr, (name, result)
        results[name] = {'passed' if name == 'fixed' else 'caught': True,
                         'returncode': result.returncode}
        print(name, 'PASS' if name == 'fixed' else 'CAUGHT', flush=True)
    (out / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
