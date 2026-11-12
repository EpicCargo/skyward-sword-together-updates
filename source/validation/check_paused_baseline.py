#!/usr/bin/env python3
"""Reproduce paused deactivation against the exact prior bridge in an owned checkout.

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
BASELINE_HEADER='34c03cccaa1c58e5edd2b089136f2656e701f0c03e8cc62b5fa6d3c58103a045'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/source-checks/paused-baseline')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    engine=ROOT/'engine';header=engine/'runtime/hle/sst_prop_bridge.h'
    patches=[p for p in sorted((ROOT/'engine-patches').glob('*.patch'))
             if p.name >= '0031-cleanup-after-paused-deactivation.patch']
    if not patches or patches[0].name!='0031-cleanup-after-paused-deactivation.patch':
        raise RuntimeError('Missing paused-deactivation patch')
    native=json.loads((ROOT/'validation/native-source.json').read_text())
    candidate=native['native_file_sha256']['engine/runtime/hle/sst_prop_bridge.h']
    if digest(header)!=candidate:
        raise RuntimeError('Candidate bridge differs from its frozen native source')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    reversed_patches=[];failures=[];controls=[]
    try:
        # Later changes must not contaminate the exact patch30 baseline.
        for patch in reversed(patches):
            subprocess.run(['git','-C',str(engine),'apply','--reverse','--check',str(patch)],check=True)
            subprocess.run(['git','-C',str(engine),'apply','--reverse',str(patch)],check=True)
            reversed_patches.append(patch)
        if digest(header)!=BASELINE_HEADER:
            raise RuntimeError('Reversing the patch did not recover the exact patch30 bridge')
        executable=out/'prop-disconnect-baseline'
        command=['clang','-O1','-g','-Wall','-Wextra','-std=c11','-D_GNU_SOURCE',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-unused-function',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(engine/'runtime'),
            str(ROOT/'tests/prop_bridge_disconnect_test.c'),str(ROOT/'client/src/sst_props.c'),
            str(ROOT/'client/src/sst_world.c'),'-lpthread','-lm','-o',str(executable)]
        subprocess.run(command,check=True,timeout=120)
        for mode in (20,21,22,23):
            for profile in (496,497,521):
                for local_id in (1,3):
                    result=subprocess.run([str(executable),str(mode),str(profile),str(local_id)],capture_output=True,timeout=20)
                    label=f'mode{mode}-profile{profile}-local{local_id}'
                    (out/(label+'.stderr')).write_bytes(result.stderr)
                    if mode in (20,21):
                        if result.returncode!=-signal.SIGABRT or b'expect_position' not in result.stderr:
                            raise RuntimeError('Baseline did not fail the intended native-floor assertion: '+label)
                        failures.append(dict(mode=mode,profile=profile,local_player_id=local_id,
                                             returncode=result.returncode,assertion='expect_position'))
                    else:
                        if result.returncode!=0:
                            raise RuntimeError('Baseline negative control failed: '+label)
                        controls.append(dict(mode=mode,profile=profile,local_player_id=local_id,passed=True))
    finally:
        for patch in reversed(reversed_patches):
            subprocess.run(['git','-C',str(engine),'apply',str(patch)],check=True)
        if digest(header)!=candidate:
            raise RuntimeError('Candidate bridge was not restored after the baseline test')
    report=dict(passed=True,scope='Actual bridge over synthetic native arena/model/network stubs; no game/GPU.',
        baseline_header_sha256=BASELINE_HEADER,candidate_header_sha256=candidate,
        reproduced_failures=failures,negative_controls=controls,candidate_source_restored=True,
        sanitizers=['AddressSanitizer','UndefinedBehaviorSanitizer'])
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
