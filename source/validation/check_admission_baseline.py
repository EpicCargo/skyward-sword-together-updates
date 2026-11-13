#!/usr/bin/env python3
"""Reproduce native packet admission against the exact patch33 bridge in an owned checkout.

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
BASELINE_HEADER='148bc6c90fd29c18b7ec53bb66637e6715400d5756b20940f2b7ed1969085f80'


EXPECTED_FAILURES={
1:['unbound packets do not consume native target capacity','bound carry survives unbound packet traffic'],
2:['accepted PLACE cannot cross a native actor lifetime'],
3:['queued pre-bind PLACE cannot cross a native actor lifetime','fresh post-bind placement remains admissible'],
4:['retired native keys release target capacity','new native key remains usable after forty replacements'],
5:['missing native actors cannot retain or acquire targets','reappearing native actor ignores an absent-lifetime placement','reappearing native actor accepts a new placement'],
11:['full native table admits current replacement key'],
12:['changed native identity cannot admit packets before rebind','changed native identity cannot cache a future placement','fresh post-bind placement remains admissible']}

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/source-checks/native-admission-baseline')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    engine=ROOT/'engine';header=engine/'runtime/hle/sst_prop_bridge.h'
    patches=[p for p in sorted((ROOT/'engine-patches').glob('*.patch'))
             if p.name >= '0034-admit-props-only-for-current-native-bindings.patch']
    if not patches or patches[0].name!='0034-admit-props-only-for-current-native-bindings.patch':
        raise RuntimeError('Missing native admission patch')
    native=json.loads((ROOT/'validation/native-source.json').read_text())
    candidate=native['native_file_sha256']['engine/runtime/hle/sst_prop_bridge.h']
    if digest(header)!=candidate:
        raise RuntimeError('Candidate bridge differs from its frozen native source')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    reversed_patches=[];failures=[];controls=[]
    try:
        # Later changes must not contaminate the exact patch33 baseline.
        for patch in reversed(patches):
            subprocess.run(['git','-C',str(engine),'apply','--reverse','--check',str(patch)],check=True)
            subprocess.run(['git','-C',str(engine),'apply','--reverse',str(patch)],check=True)
            reversed_patches.append(patch)
        if digest(header)!=BASELINE_HEADER:
            raise RuntimeError('Reversing the patch did not recover the exact patch33 bridge')
        executable=out/'prop-admission-baseline'
        command=['clang','-O1','-g','-Wall','-Wextra','-std=c11','-D_GNU_SOURCE',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-unused-function',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(engine/'runtime'),
            str(ROOT/'tests/prop_bridge_admission_test.c'),str(ROOT/'client/src/sst_props.c'),
            str(ROOT/'client/src/sst_world.c'),'-lpthread','-lm','-o',str(executable)]
        subprocess.run(command,check=True,timeout=120)
        for mode in range(1,13):
            for profile in (496,497,521):
                for local_id in (1,3):
                    result=subprocess.run([str(executable),str(mode),str(profile),str(local_id)],capture_output=True,timeout=20)
                    label=f'mode{mode}-profile{profile}-local{local_id}'
                    (out/(label+'.stderr')).write_bytes(result.stderr)
                    expected=EXPECTED_FAILURES.get(mode,[])
                    errors=[line.removeprefix('FAIL ') for line in result.stderr.decode().splitlines() if line.startswith('FAIL ')]
                    if result.returncode!=int(bool(expected)) or errors!=expected or any(line and not line.startswith('FAIL ') for line in result.stderr.decode().splitlines()):
                        raise RuntimeError('Baseline did not reproduce the intended admission result: '+label)
                    row=dict(mode=mode,profile=profile,local_player_id=local_id,returncode=result.returncode)
                    if expected:
                        row['failure_checks']=errors;failures.append(row)
                    else:row['passed']=True;controls.append(row)
    finally:
        for patch in reversed(reversed_patches):
            subprocess.run(['git','-C',str(engine),'apply',str(patch)],check=True)
        if digest(header)!=candidate:
            raise RuntimeError('Candidate bridge was not restored after the baseline test')
    report=dict(passed=True,scope='Actual native admission/lifetime bridge over synthetic arena/model/network stubs; no game/GPU.',
        baseline_header_sha256=BASELINE_HEADER,candidate_header_sha256=candidate,
        reproduced_failures=failures,negative_controls=controls,candidate_source_restored=True,
        sanitizers=['AddressSanitizer','UndefinedBehaviorSanitizer'])
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
