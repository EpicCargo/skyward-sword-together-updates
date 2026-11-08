#!/usr/bin/env python3
"""Build public SST update assets and publish them with GitHub CLI; never stage project files."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys,zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from sst_launcher import APP_ID,UpdateError,atomic_json,safe_name,digest,validate_manifest

def windows_release(source: Path,out:Path,rid:str,platform='windows-x86_64',previous:Path|None=None):
    out.mkdir(parents=True,exist_ok=True);prefix='Skyward Sword Together/'
    name=f'SST-runtime-{platform}-{rid}.zip';archive=out/name
    if archive.exists():raise UpdateError('Output already exists; choose a new release ID/directory.')
    files={}
    with zipfile.ZipFile(source) as z:
        for info in z.infolist():
            if info.is_dir():continue
            if not info.filename.startswith(prefix):raise UpdateError('Unexpected source package root.')
            relative=safe_name(info.filename[len(prefix):])
            with z.open(info) as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
            files[relative]={'sha256':sha,'size':info.file_size}
    manifest={'schema':1,'app':APP_ID,'release_id':rid,'platform':platform,'entrypoint':'skyrecomp.exe','files':files}
    validate_manifest(manifest);shutil.copyfile(source,archive)
    manifest_name=f'sst-manifest-{platform}-{rid}.json';atomic_json(out/manifest_name,manifest)
    entry={'url':archive.name,'size':archive.stat().st_size,'sha256':digest(archive),'prefix':prefix,'kind':'full',
           'manifest':manifest_name,'manifest_sha256':digest(out/manifest_name)}
    if previous:
        old=json.loads(previous.read_text());validate_manifest(old)
        changed={n for n,m in files.items() if old['files'].get(n)!=m};patch=out/f'SST-patch-{platform}-{old["release_id"]}-to-{rid}.zip'
        with zipfile.ZipFile(source) as src,zipfile.ZipFile(patch,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as dst:
            for n in sorted(changed):dst.writestr(prefix+n,src.read(prefix+n))
        entry['patches']={old['release_id']:{'url':patch.name,'size':patch.stat().st_size,'sha256':digest(patch),'prefix':prefix}}
    return entry,manifest

def linux_release(binary:Path,out:Path,rid:str,project:Path):
    platform='linux-x86_64';archive=out/f'SST-runtime-{platform}-{rid}.zip'
    mn=f'sst-manifest-{platform}-{rid}.json'
    if archive.exists() or archive.is_symlink() or (out/mn).exists() or (out/mn).is_symlink():
        raise UpdateError('Output already exists; choose a new release ID/directory.')
    online={'sst-session-host':project/'client/build/sst-session-host'}
    for source,name in [('sst_online_init.py','__init__.py'),('sst_online.py','sst_online.py'),('sst_online_transport.py','sst_online_transport.py')]:
        online['online/'+name]=project/'launcher'/source
    if any(path.is_symlink() or not path.is_file() for path in online.values()):
        raise UpdateError('The Linux online helper and all three companion modules are required regular files.')
    mapping={'skyrecomp':binary,**online}
    font=project/'engine/fonts/Baloo2-SemiBold.ttf'
    if font.exists():mapping[font.name]=font
    for n in ['cpu.h','simd.h','simd_fp.h','gamelib.h']:mapping['gamelib-src/'+n]=project/'engine/runtime'/n
    mapping['gamelib-src/gamelib_entry.c']=project/'engine/tools/gamelib_entry.c';mapping['gamelib-src/hle_symbols.txt']=project/'engine/runtime/hle/hle_symbols.txt'
    mapping['README-SST.txt']=project/'docs/windows-package-readme.txt'
    manifest={'schema':1,'app':APP_ID,'release_id':rid,'platform':platform,'entrypoint':'skyrecomp','files':{n:{'sha256':digest(p),'size':p.stat().st_size} for n,p in mapping.items()}}
    validate_manifest(manifest)
    out.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for n,p in mapping.items():z.write(p,n)
    atomic_json(out/mn,manifest)
    return {'url':archive.name,'size':archive.stat().st_size,'sha256':digest(archive),'prefix':'','kind':'full','manifest':mn,'manifest_sha256':digest(out/mn)},manifest

def github_publish(gh:Path,repo:str,folder:Path,tag:str,title:str,notes:str|None=None):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',repo):raise UpdateError('Invalid GitHub repository.')
    owner=repo.split('/')[0];actual=subprocess.check_output([str(gh),'api','user','--jq','.login'],text=True).strip()
    if actual.casefold()!=owner.casefold():raise UpdateError(f'GitHub CLI is signed in as {actual}; expected {owner}.')
    exists=subprocess.run([str(gh),'repo','view',repo,'--json','isPrivate'],capture_output=True,text=True)
    if not exists.returncode and json.loads(exists.stdout)['isPrivate']:raise UpdateError('The selected repository is private. It was left unchanged; use a dedicated public update repository.')
    if exists.returncode:
        subprocess.run([str(gh),'repo','create',repo,'--public','--description','Verified Skyward Sword Together testing releases. No game files, console keys or saves.','--add-readme'],check=True)
    note=folder/'release-notes.txt';note.write_text(notes or 'Skyward Sword Together testing build.\n\nExtract SST-Launcher-Windows.zip once and open SST Update and Play.exe.\nGame files and saves are not included. Boss/miniboss sync remains OFF.\nFull campaign, actual Windows hardware and separate-PC internet remain unverified.\n\nFuture builds: click Update & Play. Friends should use the same runtime build/protocol.\n',encoding='utf-8')
    assets=[p for p in folder.iterdir() if p.is_file() and p.name!='release-notes.txt']
    # Draft keeps the latest update feed unchanged until every verified asset is uploaded.
    subprocess.run([str(gh),'release','create',tag,'--repo',repo,'--draft','--title',title,'--notes-file',str(note),*[str(p) for p in assets]],check=True)
    subprocess.run([str(gh),'release','edit',tag,'--repo',repo,'--draft=false','--latest'],check=True)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--windows',type=Path,required=True);p.add_argument('--linux',type=Path);p.add_argument('--out',type=Path,required=True);p.add_argument('--id',required=True);p.add_argument('--sequence',type=int,required=True);p.add_argument('--protocol',type=int,default=45);p.add_argument('--previous',type=Path);p.add_argument('--publish',action='store_true');p.add_argument('--repo',default='EpicCargo/skyward-sword-together-updates');p.add_argument('--gh',type=Path);a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}',a.id):p.error('Invalid release ID')
    a.out.mkdir(parents=True,exist_ok=True)
    entry,manifest=windows_release(a.windows,a.out,a.id,previous=a.previous);platforms={'windows-x86_64':entry}
    if a.linux:platforms['linux-x86_64']=linux_release(a.linux,a.out,a.id,Path(__file__).resolve().parents[1])[0]
    atomic_json(a.out/'sst-updates.json',{'schema':1,'app':APP_ID,'channel':'testing','release':{'id':a.id,'sequence':a.sequence,'protocol':a.protocol,'notes':'Experimental co-op; boss/miniboss sync off.','platforms':platforms}})
    print(a.out/'sst-updates.json')
    if a.publish:
        if not a.gh:p.error('--gh is required for publishing')
        github_publish(a.gh,a.repo,a.out,'sst-'+a.id,'SST '+a.id)

if __name__=='__main__':main()
