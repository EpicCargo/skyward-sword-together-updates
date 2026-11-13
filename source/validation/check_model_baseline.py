#!/usr/bin/env python3
"""Reproduce model callback interruption at actor callbacks against the exact patch37 bridge in an owned checkout.

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
BASELINE_HEADER='ab2572f5c7a36d0495990512607cd6ac3d3fd7a6b845e4890bd91d938a541514'


def expected_failures(mode,profile,stage):
 if profile==521 or mode==11:return []
 return ['interrupted model tail cannot write into the changed context']+(['interrupted model presentation stops further guest calls'] if stage<3 else [])+(['interrupted model presentation cannot commit applied statistics'] if mode not in (1,4,13) else [])+(['partially presented carry retains once-only valid floor cleanup'] if mode==13 else [])


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/source-checks/actor-model-baseline')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    engine=ROOT/'engine';header=engine/'runtime/hle/sst_prop_bridge.h'
    patches=[p for p in sorted((ROOT/'engine-patches').glob('*.patch'))
             if p.name >= '0038-stop-stale-prop-model-callbacks.patch']
    if not patches or patches[0].name!='0038-stop-stale-prop-model-callbacks.patch':
        raise RuntimeError('Missing actor-session patch')
    native=json.loads((ROOT/'validation/native-source.json').read_text())
    native_header=engine/'runtime/hle/sst_prop_native.h'
    baseline_native='3dc48fd44941c2fd1e4a452b7883ce2d321d298b53e98037ce4c56753bdbb200'
    candidate_native=native['native_file_sha256']['engine/runtime/hle/sst_prop_native.h']
    if digest(native_header)!=candidate_native:raise RuntimeError('Wrong candidate native adapter')
    candidate=native['native_file_sha256']['engine/runtime/hle/sst_prop_bridge.h']
    if digest(header)!=candidate:
        raise RuntimeError('Candidate bridge differs from its frozen native source')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    reversed_patches=[];failures=[];controls=[]
    try:
        # Later changes must not contaminate the exact patch37 baseline.
        for patch in reversed(patches):
            subprocess.run(['git','-C',str(engine),'apply','--reverse','--check',str(patch)],check=True)
            subprocess.run(['git','-C',str(engine),'apply','--reverse',str(patch)],check=True)
            reversed_patches.append(patch)
        if digest(header)!=BASELINE_HEADER:
            raise RuntimeError('Reversing the patch did not recover the exact patch37 bridge')
        if digest(native_header)!=baseline_native:raise RuntimeError('Wrong exact patch37 native adapter')
        executable=out/'prop-model-baseline'
        command=['clang','-O1','-g','-Wall','-Wextra','-std=c11','-D_GNU_SOURCE',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-unused-function',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(engine/'runtime'),
            str(ROOT/'tests/prop_bridge_model_test.c'),str(ROOT/'client/src/sst_props.c'),
            str(ROOT/'client/src/sst_world.c'),'-lpthread','-lm','-o',str(executable)]
        subprocess.run(command,check=True,timeout=120)
        for mode in range(1,15):
            for profile in (496,497,521):
                for local_id in (1,3):
                    for stage in (1,2,3) if profile!=521 else (0,):
                        result=subprocess.run([str(executable),str(mode),str(profile),str(local_id),str(stage)],capture_output=True,timeout=20)
                        label=f'mode{mode}-profile{profile}-local{local_id}-stage{stage}'
                        (out/(label+'.stderr')).write_bytes(result.stderr)
                        expected=expected_failures(mode,profile,stage)
                        errors=[line.removeprefix('FAIL ') for line in result.stderr.decode().splitlines() if line.startswith('FAIL ')]
                        if result.returncode!=int(bool(expected)) or errors!=expected or any(line and not line.startswith('FAIL ') for line in result.stderr.decode().splitlines()):
                            raise RuntimeError('Baseline did not reproduce the intended model result: '+label)
                        row=dict(mode=mode,profile=profile,local_player_id=local_id,stage=stage,returncode=result.returncode)
                        if expected:row['failure_checks']=errors;failures.append(row)
                        else:row['passed']=True;controls.append(row)
    finally:
        for patch in reversed(reversed_patches):
            subprocess.run(['git','-C',str(engine),'apply',str(patch)],check=True)
        if digest(header)!=candidate or digest(native_header)!=candidate_native:
            raise RuntimeError('Candidate bridge/native adapter not restored')
    report=dict(passed=True,scope='Actual model-boundary actor bridge over synthetic arena/model/network stubs; no game/GPU.',
        baseline_header_sha256=BASELINE_HEADER,candidate_header_sha256=candidate,
        baseline_native_header_sha256=baseline_native,candidate_native_header_sha256=candidate_native,
        reproduced_failures=failures,negative_controls=controls,candidate_source_restored=True,
        sanitizers=['AddressSanitizer','UndefinedBehaviorSanitizer'])
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
