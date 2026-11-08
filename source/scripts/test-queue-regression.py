#!/usr/bin/env python3
"""Use Co-op Anything's MIT shrinker against the real C client's former queue bug."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/queue-regression'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for proc in Path('/proc').iterdir():
        try:
            if (proc / 'exe').resolve() == ROOT/'engine/build/skyrecomp':
                raise RuntimeError('Stop this project\'s game before compiling the mutation fixture')
        except OSError:
            pass
    source = (ROOT/'client/src/sst_net.c').read_text()
    start = source.index('static bool queue_flag(')
    end = source.index('// Caller holds mu. Drain old flags', start)
    # Restore packet-counted buffering in this private source copy only.
    broken = '''static bool queue_flag(Flag *flags, int *count, uint16_t *slots, Flag flag) {
    (void)slots;
    if (*count == FLAG_QUEUE) return false;
    flags[(*count)++] = flag; return true;
}

'''
    mutant = OUT/'append-only-net.c'; mutant.write_text(source[:start]+broken+source[end:])
    libraries = next(s for s in (ROOT/'client/Makefile').read_text().splitlines() if s.startswith('LIB :=')).split(':=',1)[1].split()
    command = shlex.split(os.environ.get('CC','clang')) + ['-O1','-g','-std=c11','-D_GNU_SOURCE',
        '-I'+str(ROOT/'client/src'),'-o',str(OUT/'append-only-client'),str(ROOT/'client/tests/queue_fixture.c'),
        str(mutant)] + [str(ROOT/'client'/p) for p in libraries if p != 'src/sst_net.c'] + ['-lpthread','-lm']
    with (OUT/'compile.log').open('w') as log:
        subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
    spec = importlib.util.spec_from_file_location('sst_queue_replay',ROOT/'client/tests/queue_e2e.py')
    replay = importlib.util.module_from_spec(spec); spec.loader.exec_module(replay)
    sys.path.insert(0,str(ROOT/'third_party/coop-anything-flight'))
    provenance = json.loads((ROOT/'third_party/coop-anything-flight/UPSTREAM.json').read_text())
    for name, info in provenance['files'].items():
        assert hashlib.sha256((ROOT/'third_party/coop-anything-flight'/name).read_bytes()).hexdigest() == info['sha256'], name
    import shrink
    original = {'version':1,'config':{'expected':'converged','transport':'real TCP C client'},'steps':
        [{'op':'flags','index':i,'count':1} for i in range(10,20)] +
        [{'op':'flags','index':50,'count':700} for _ in range(7)] +
        [{'op':'flags','index':65530,'value':1,'pin':True}]}
    old = [str(OUT/'append-only-client')]
    failed = replay.replay(original,old); assert failed['diverged'],failed
    assert not replay.replay(original)['diverged']
    minimized = shrink.shrink_fixture(original,lambda f:replay.replay(f,old),max_runs=120,max_seconds=45)
    assert minimized['shrunk']['complete'] and len(minimized['steps']) < len(original['steps']),minimized
    fixed = replay.replay(minimized); assert not fixed['diverged'],fixed
    assert replay.replay(minimized,old)['diverged']
    # A second twin keeps coalescing but suppresses automatic overflow recovery.
    no_refresh = OUT/'no-refresh-net.c'
    no_refresh.write_text(source.replace('static void recover_after_drain(void) {',
                                        'static void recover_after_drain(void) { return;'))
    second = [str(no_refresh) if x == str(mutant) else
              str(OUT/'no-refresh-client') if x == str(OUT/'append-only-client') else x for x in command]
    with (OUT/'no-refresh-compile.log').open('w') as log:
        subprocess.run(second,check=True,stdout=log,stderr=subprocess.STDOUT)
    env = dict(os.environ,SST_QUEUE_CLIENT_COMMAND=json.dumps([str(OUT/'no-refresh-client')]))
    result = subprocess.run([sys.executable,str(ROOT/'client/tests/queue_e2e.py')],env=env,
                            capture_output=True,text=True,timeout=25)
    (OUT/'no-refresh-result.log').write_text(result.stdout+result.stderr)
    assert result.returncode and 'AssertionError' in result.stderr and "'recoveries': 0" in result.stderr,result
    for name,data in [('original.json',original),('minimized.json',minimized),('fixed.json',fixed)]:
        (OUT/name).write_text(json.dumps(data,indent=2)+'\n')
    summary = {'upstream_commit':'5b86e5bb10ce864ab9872f2589651f73ffcae5db',
        'tool':'coop-anything tools/flight/shrink.py (unmodified MIT code)',
        'mutation_caught':True,'automatic_recovery_mutation_caught':True,
        'fixed_client_passed':True,'original_steps':len(original['steps']),
        'reduced_steps':len(minimized['steps']),'oracle_runs':minimized['shrunk']['oracle_runs'],
        'one_minimal':minimized['shrunk']['complete'],'full_game_instances':0}
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n'); print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
