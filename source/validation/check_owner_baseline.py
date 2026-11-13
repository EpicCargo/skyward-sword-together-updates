#!/usr/bin/env python3
"""Reproduce remote owner retirement at actor callbacks against the exact patch36 bridge in an owned checkout.

This compiles asset-free CPU fixtures. Hosted CI invokes it with sanitizers;
local invocation belongs inside the normal compiler/resource guard.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import signal
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BASELINE_HEADER='c4f93c4e21df8205228c3cd91dbf130a6a02dac6fb34df44547a341b8c70f835'


EXPECTED_FAILURES={1: ['retired peer cannot apply a queued placement before world refresh'], 2: ['retired peer cannot apply a queued placement before world refresh'], 3: ['retired peer cannot apply a queued placement before world refresh'], 4: ['retired remote carry returns to the saved floor before world refresh', 'remote retirement cannot repeat an old floor write'], 5: ['retired remote carry returns to the saved floor before world refresh', 'remote retirement cannot repeat an old floor write'], 6: ['retired remote carry returns to the saved floor before world refresh', 'remote retirement cannot repeat an old floor write'], 7: ['departure cannot rewind an already settled native placement'], 14: ['callback retirement rejects late packets from the old tuple'], 15: ['retired remote carry returns to the saved floor before world refresh', 'invalid callback roster disables packet authority', 'remote retirement cannot repeat an old floor write'], 16: ['retired remote carry returns to the saved floor before world refresh', 'invalid callback roster disables packet authority', 'remote retirement cannot repeat an old floor write'], 18: ['retired remote carry returns to the saved floor before world refresh']}
def expected_failures(mode,local_id):
 return ['native pickup can send after the old remote owner retires'] if mode==17 and local_id==3 else EXPECTED_FAILURES.get(mode,[])



def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/source-checks/actor-owner-baseline')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    engine=ROOT/'engine';header=engine/'runtime/hle/sst_prop_bridge.h'
    patches=[p for p in sorted((ROOT/'engine-patches').glob('*.patch'))
             if p.name >= '0037-refresh-remote-prop-authority-at-callbacks.patch']
    if not patches or patches[0].name!='0037-refresh-remote-prop-authority-at-callbacks.patch':
        raise RuntimeError('Missing actor-session patch')
    native=json.loads((ROOT/'validation/native-source.json').read_text())
    candidate=native['native_file_sha256']['engine/runtime/hle/sst_prop_bridge.h']
    if digest(header)!=candidate:
        raise RuntimeError('Candidate bridge differs from its frozen native source')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    reversed_patches=[];failures=[];controls=[]
    try:
        # Later changes must not contaminate the exact patch36 baseline.
        for patch in reversed(patches):
            subprocess.run(['git','-C',str(engine),'apply','--reverse','--check',str(patch)],check=True)
            subprocess.run(['git','-C',str(engine),'apply','--reverse',str(patch)],check=True)
            reversed_patches.append(patch)
        if digest(header)!=BASELINE_HEADER:
            raise RuntimeError('Reversing the patch did not recover the exact patch36 bridge')
        executable=out/'prop-owner-baseline'
        command=['clang','-O1','-g','-Wall','-Wextra','-std=c11','-D_GNU_SOURCE',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-unused-function',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(engine/'runtime'),
            str(ROOT/'tests/prop_bridge_owner_test.c'),str(ROOT/'client/src/sst_props.c'),
            str(ROOT/'client/src/sst_world.c'),'-lpthread','-lm','-o',str(executable)]
        subprocess.run(command,check=True,timeout=120)
        for mode in range(1,19):
            for profile in (496,497,521):
                for local_id in (1,3):
                    result=subprocess.run([str(executable),str(mode),str(profile),str(local_id)],capture_output=True,timeout=20)
                    label=f'mode{mode}-profile{profile}-local{local_id}'
                    (out/(label+'.stderr')).write_bytes(result.stderr)
                    expected=expected_failures(mode,local_id)
                    errors=[line.removeprefix('FAIL ') for line in result.stderr.decode().splitlines() if line.startswith('FAIL ')]
                    if result.returncode!=int(bool(expected)) or errors!=expected or any(line and not line.startswith('FAIL ') for line in result.stderr.decode().splitlines()):
                        raise RuntimeError('Baseline did not reproduce the intended owner result: '+label)
                    row=dict(mode=mode,profile=profile,local_player_id=local_id,returncode=result.returncode)
                    if expected:
                        row['failure_checks']=errors;failures.append(row)
                    else:row['passed']=True;controls.append(row)
    finally:
        for patch in reversed(reversed_patches):
            subprocess.run(['git','-C',str(engine),'apply',str(patch)],check=True)
        if digest(header)!=candidate:
            raise RuntimeError('Candidate bridge was not restored after the baseline test')
    report=dict(passed=True,scope='Actual remote-owner actor bridge over synthetic arena/model/network stubs; no game/GPU.',
        baseline_header_sha256=BASELINE_HEADER,candidate_header_sha256=candidate,
        reproduced_failures=failures,negative_controls=controls,candidate_source_restored=True,
        sanitizers=['AddressSanitizer','UndefinedBehaviorSanitizer'])
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
