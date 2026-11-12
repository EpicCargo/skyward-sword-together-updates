#!/usr/bin/env python3
"""Build an asset-free Windows runtime on CI, using frozen public source headers.

The worker requires GitHub Actions and the ordinary resource runner. Local full
builds continue to use their Windows/Linux preflights. --validate-inputs is file-only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'third_party/windows-build-headers-v202'
TOOLCHAIN='llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64'
TOOLCHAIN_URL=f'https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/{TOOLCHAIN}.tar.xz'
TOOLCHAIN_SHA256='55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335'
TOOLCHAIN_BYTES=84017268
LIBRARIES=('SDL3','avcodec','avformat','avutil','swscale','shaderc_shared','vulkan-1')


def digest(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate metadata key: '+key)
        result[key]=value
    return result


def safe_name(name):
    if (not isinstance(name,str) or not name or '\\' in name or ':' in name or '\0' in name or
            PurePosixPath(name).is_absolute() or any(p in ('','.','..') for p in name.split('/'))):
        raise ValueError('Unsafe source/archive path: '+repr(name))
    return name


def verify_sdk(sdk=SDK):
    sdk=Path(sdk)
    if sdk.is_symlink():raise ValueError('SDK directory must be regular')
    manifest=sdk/'FILES-SHA256.json'
    if manifest.is_symlink() or manifest.stat().st_size>1024*1024:raise ValueError('Invalid SDK inventory')
    files=json.loads(manifest.read_text(),object_pairs_hook=unique)
    if not isinstance(files,dict) or not 1<=len(files)<=512:raise ValueError('Invalid SDK file count')
    total=0
    for name,expected in files.items():
        safe_name(name)
        if not isinstance(expected,str) or not re.fullmatch('[0-9a-f]{64}',expected):raise ValueError('Invalid SDK checksum')
        if Path(name).suffix not in ('.h','.hpp','.def','.txt','.md','.json'):raise ValueError('Non-source SDK member')
        path=sdk
        for part in name.split('/'):
            path=path/part
            if path.is_symlink():raise ValueError('Symlink in SDK source')
        if not path.is_file() or path.stat().st_size>8*1024*1024:raise ValueError('Missing/oversized SDK source')
        data=path.read_bytes();total+=len(data)
        if b'\0' in data or hashlib.sha256(data).hexdigest()!=expected:raise ValueError('Corrupt/non-text SDK member: '+name)
    if total>16*1024*1024:raise ValueError('SDK is too large')
    actual=set()
    for path in sdk.rglob('*'):
        if path.is_symlink():raise ValueError('Symlink in SDK inventory')
        if path.is_file():actual.add(path.relative_to(sdk).as_posix())
    if actual!=set(files)|{'FILES-SHA256.json'}:raise ValueError('SDK contains unlisted source files')
    required={'include/SDL3/SDL.h','include/libavcodec/avcodec.h','include/libavformat/avformat.h',
              'include/libavutil/avutil.h','include/libswscale/swscale.h','include/shaderc/shaderc.h',
              'vulkan/include/vulkan/vulkan.h','vma/include/vk_mem_alloc.h'}
    required|={'definitions/'+name+'.def' for name in LIBRARIES}
    if not required<=set(files):raise ValueError('Incomplete Windows source SDK')
    return files


def download(url,target,expected,size):
    target=Path(target)
    if target.exists() or target.is_symlink():
        if target.is_symlink() or not target.is_file() or target.stat().st_size!=size or digest(target)!=expected:
            raise FileExistsError('Existing download was preserved')
        return
    partial=target.with_name(target.name+'.part')
    owned=False
    try:
        output=partial.open('xb');owned=True
        with output,urllib.request.urlopen(url,timeout=90) as response:
            length=0;checksum=hashlib.sha256()
            while block:=response.read(1024*1024):
                length+=len(block)
                if length>size:raise ValueError('Toolchain download exceeds pinned size')
                checksum.update(block);output.write(block)
        if length!=size or checksum.hexdigest()!=expected:raise ValueError('Toolchain download checksum/size mismatch')
        os.link(partial,target)
    finally:
        if owned:partial.unlink(missing_ok=True)


def extract_toolchain(archive,destination):
    destination=Path(destination)
    destination.mkdir(exist_ok=False)
    with tarfile.open(archive,'r:xz') as saved:
        members=saved.getmembers();names=set();total=0
        if len(members)>20000:raise ValueError('Oversized compiler inventory')
        for member in members:
            name=member.name.removeprefix('./').rstrip('/');safe_name(name)
            if name.split('/')[0]!=TOOLCHAIN or name in names:raise ValueError('Wrong/duplicate compiler archive root')
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):raise ValueError('Special compiler archive member')
            names.add(name);total+=member.size
        if total>1024*1024*1024:raise ValueError('Compiler archive expands beyond limit')
        # Python's data filter additionally confines symlink/hardlink targets.
        saved.extractall(destination,members=members,filter='data')
    return destination/TOOLCHAIN


def inspect_pe(path):
    with Path(path).open('rb') as stream:
        dos=stream.read(64)
        if len(dos)!=64 or dos[:2]!=b'MZ':raise ValueError('Not a PE executable')
        stream.seek(struct.unpack_from('<I',dos,60)[0]);header=stream.read(88)
    if (len(header)!=88 or header[:4]!=b'PE\0\0' or struct.unpack_from('<H',header,4)[0]!=0x8664 or
            struct.unpack_from('<H',header,24)[0]!=0x20b):raise ValueError('Expected an AMD64 PE32+ runtime')
    return dict(machine='AMD64',image_base=struct.unpack_from('<Q',header,48)[0],image_size=struct.unpack_from('<I',header,80)[0])


def build():
    if os.environ.get('GITHUB_ACTIONS')!='true' or not os.environ.get('SST_RESOURCE_RUNNER_PID'):
        raise RuntimeError('CI worker only; use the normal guarded local Windows build on your PC')
    head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    if os.environ.get('GITHUB_SHA')!=head:raise RuntimeError('CI source commit mismatch')
    files=verify_sdk()
    native=json.loads((ROOT/'validation/native-source.json').read_text())
    engine=ROOT/'engine'
    for name,expected in native['native_file_sha256'].items():
        if digest(ROOT/name)!=expected:raise RuntimeError('Native source changed: '+name)
    tree=subprocess.check_output(['git','-C',str(engine),'rev-parse','HEAD^{tree}'],text=True).strip()
    if tree!=native['engine_tree']:raise RuntimeError('Wrong restored native engine')
    if any((engine/name).exists() for name in ('generated','extracted','romfs.bin')):raise RuntimeError('Runtime CI must not contain game inputs')
    work=ROOT/'build/windows-runtime-ci';work.mkdir(parents=True,exist_ok=False)
    archive=work/'toolchain.tar.xz';download(TOOLCHAIN_URL,archive,TOOLCHAIN_SHA256,TOOLCHAIN_BYTES)
    compiler=extract_toolchain(archive,work/'tools')
    prefix=work/'prefix';shutil.copytree(SDK/'include',prefix/'include');(prefix/'lib').mkdir()
    for name in LIBRARIES:
        subprocess.run([str(compiler/'bin/llvm-dlltool'),'-m','i386:x86-64','-d',str(SDK/'definitions'/(name+'.def')),
                        '-l',str(prefix/'lib'/('lib'+name+'.dll.a'))],check=True,timeout=60)
    output=work/'runtime';binary=output/'skyrecomp.exe'
    env=dict(os.environ,MAKEFLAGS='',MFLAGS='',SST_BUILD_JOBS='1')
    includes=':'.join(str(SDK/p) for p in ('vulkan/include','vma/include'))
    env.update(C_INCLUDE_PATH=includes,CPLUS_INCLUDE_PATH=includes)
    command=['make','-C',str(engine),'-j1','PLATFORM=windows','B='+str(output),'DEPS='+str(prefix),
             'CC='+str(compiler/'bin/x86_64-w64-mingw32-clang'),
             'CXX='+str(compiler/'bin/x86_64-w64-mingw32-clang++'),
             'WINDRES='+str(compiler/'bin/x86_64-w64-mingw32-windres'),str(binary)]
    subprocess.run(command,check=True,env=env,timeout=1200)
    pe=inspect_pe(binary)
    symbols=subprocess.check_output([str(compiler/'bin/llvm-nm'),'--defined-only',str(binary)],text=True)
    addresses={}
    for line in symbols.splitlines():
        fields=line.split()
        if len(fields)==3 and fields[2] in ('sst_props_roster','sst_prop_discovery_stats','sst_dev_native_stats'):
            addresses[fields[2]]=int(fields[0],16)
    if len(addresses)!=3:raise RuntimeError('Required native symbols missing from linked runtime')
    rva=addresses['sst_dev_native_stats']-pe['image_base']
    if not 0<rva<=pe['image_size']-64:raise RuntimeError('Invalid developer counter RVA')
    artifacts=work/'artifacts';artifacts.mkdir()
    for name in ('skyrecomp.exe','skyrecomp.dll.a'):shutil.copyfile(output/name,artifacts/name)
    shutil.copytree(SDK/'licenses',artifacts/'licenses')
    shutil.copyfile(compiler/'LICENSE.TXT',artifacts/'licenses/LLVM-LICENSE.txt')
    shutil.copyfile(ROOT/'ENGINE-LICENSE',artifacts/'licenses/SkyRecomp-LICENSE.txt')
    for name in ('COPYING','COPYING.MinGW-w64-runtime.txt','COPYING.MinGW-w64.txt','COPYING.winpthreads.txt'):
        shutil.copyfile(compiler/'x86_64-w64-mingw32/share/mingw32'/name,artifacts/'licenses'/name)
    for source in (engine/'third_party').rglob('*'):
        if source.is_file() and source.name.lower().startswith(('license','copying','notice')):
            data=source.read_bytes()
            if b'\0' in data:raise RuntimeError('Unexpected binary third-party notice')
            target=artifacts/'licenses/engine-third-party'/source.relative_to(engine/'third_party')
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    report=dict(passed=True,scope='Asset-free CI Windows runtime build only; not a portable package or gameplay acceptance.',
        source_commit=head,source_inventory_sha256=digest(ROOT/'SOURCE-SHA256.json'),source_files=len(json.loads((ROOT/'SOURCE-SHA256.json').read_text())),
        engine_tree=tree,engine_patches=native['engine_patches'],native_source_files=len(native['native_file_sha256']),
        compiler_archive_sha256=TOOLCHAIN_SHA256,public_sdk_files=len(files),compiler_jobs=1,
        machine=pe['machine'],gamelib_id=re.search(r'^GAMELIB_ID \?= (\w+)',(engine/'Makefile').read_text(),re.M)[1],
        developer_counter=dict(symbol='sst_dev_native_stats',rva=rva,bytes=64,derivation='llvm-nm and PE ImageBase; independently verify before memory observation'),
        artifacts={n:dict(sha256=digest(artifacts/n),bytes=(artifacts/n).stat().st_size) for n in ('skyrecomp.exe','skyrecomp.dll.a')},
        game_library_built=False,game_files_used=False,game_launched=False,installed=False,release_feed_updated=False)
    (artifacts/'BUILD-RECEIPT.json').write_text(json.dumps(report,indent=2)+'\n')
    (artifacts/'README-ci.txt').write_text('SST Windows runtime validation artifact only.\nNot a standalone portable package; runtime DLLs, compiler, game files and saves are absent.\nSource commit: '+head+'\nReview BUILD-RECEIPT.json. Native gameplay remains unverified. Derive exact observer metadata before isolated testing. No installation or release-feed update performed.\n')
    inventory={path.relative_to(artifacts).as_posix():digest(path) for path in sorted(artifacts.rglob('*')) if path.is_file()}
    (artifacts/'ARTIFACT-SHA256.json').write_text(json.dumps(inventory,indent=2)+'\n')
    verify_sdk()
    for name,expected in native['native_file_sha256'].items():
        if digest(ROOT/name)!=expected:raise RuntimeError('Build modified native source: '+name)
    print(json.dumps(report),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validate-inputs',action='store_true',help='verify frozen public source SDK without a compiler or network')
    args=parser.parse_args()
    if args.validate_inputs:
        print(f'Verified {len(verify_sdk())} public build-source files; no compiler, Windows process or game.')
    else:build()


if __name__=='__main__':main()
