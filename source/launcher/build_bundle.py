#!/usr/bin/env python3
"""Package verified SST runtimes with the portable updater. No user data is read."""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,sys,tempfile,zipfile,hashlib
sys.path.insert(0,str(Path(__file__).resolve().parent))
import sst_launcher as updater
from publish import windows_release,linux_release
ROOT=Path(__file__).resolve().parents[1]
RELEASE='2026.10.29-scenes48'
SEQUENCE=202610290048

def seed_version(stage,feed,manifest,source_zip):
    updater.validate_manifest(manifest)
    rid=feed['release']['id']
    if not isinstance(rid,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}',rid) or manifest.get('release_id')!=rid:
        raise updater.UpdateError('Portable release identity does not match its runtime manifest.')
    folder=stage/'versions'/rid
    if folder.exists() or folder.is_symlink():
        raise updater.UpdateError('Portable runtime already exists; choose a fresh destination.')
    prefix=feed['release']['platforms'][manifest['platform']].get('prefix','')
    if not isinstance(prefix,str) or (prefix and not prefix.endswith('/')):
        raise updater.UpdateError('Invalid portable runtime prefix.')
    if prefix:updater.safe_name(prefix[:-1])
    with zipfile.ZipFile(source_zip) as z:
        members={}
        for info in z.infolist():
            if info.is_dir():continue
            if not info.filename.startswith(prefix):
                raise updater.UpdateError('Unexpected portable runtime root.')
            name=info.filename[len(prefix):] if prefix else info.filename;updater.safe_name(name)
            if name in members:
                raise updater.UpdateError('Duplicate portable runtime member.')
            members[name]=info
        if members.keys()!=manifest['files'].keys():
            raise updater.UpdateError('Portable runtime does not contain the complete manifest file set.')
        if any(info.file_size!=manifest['files'][name]['size'] for name,info in members.items()):
            raise updater.UpdateError('Portable runtime file size differs from its manifest.')
        folder.parent.mkdir(parents=True,exist_ok=True)
        # Failed copies/checksums leave no partial version or active launcher state.
        with tempfile.TemporaryDirectory(prefix='.seed-',dir=folder.parent) as temporary:
            staged=Path(temporary)/'runtime';staged.mkdir()
            for name,info in members.items():
                path=staged/name;path.parent.mkdir(parents=True,exist_ok=True)
                with z.open(info) as src,path.open('wb') as dst:shutil.copyfileobj(src,dst,1024*1024)
                if updater.digest(path)!=manifest['files'][name]['sha256']:
                    raise updater.UpdateError('Portable runtime checksum differs from its manifest.')
                if manifest['platform'].startswith('linux'):path.chmod(updater.runtime_file_mode(name,manifest['entrypoint']))
            updater.atomic_json(staged/'installed-manifest.json',manifest)
            staged.rename(folder)
    updater.atomic_json(stage/'launcher-state.json',{'active':{'id':rid,'sequence':feed['release']['sequence'],'protocol':feed['release']['protocol'],'entrypoint':manifest['entrypoint']},'previous':None})

def zip_stage(stage,target):
    try:
        with zipfile.ZipFile(target,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for p in sorted(stage.rglob('*')):
                if p.is_file():z.write(p,Path('SST')/p.relative_to(stage))
    except FileExistsError:
        raise updater.UpdateError('Portable ZIP already exists; choose a new output path.') from None

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--windows',type=Path,default=ROOT/'build/dist/SST-Windows-coop-protocol45-scenes48.zip');p.add_argument('--linux',type=Path,default=ROOT/'engine/build/skyrecomp');p.add_argument('--id',default=RELEASE);p.add_argument('--sequence',type=int,default=SEQUENCE);p.add_argument('--protocol',type=int,default=45);p.add_argument('--previous',type=Path);a=p.parse_args();out=a.out.absolute();out.mkdir(parents=True,exist_ok=True)
    windows=a.windows;runtime=ROOT/'.deps/sst-launcher/python-windows';assert (runtime/'pythonw.exe').is_file()
    # Preserve source release ZIPs; publish update-friendly names with a separate manifest.
    wentry,wm=windows_release(windows,out,a.id,previous=a.previous)
    lentry,lm=linux_release(a.linux,out,a.id,ROOT)
    feed={'schema':1,'app':updater.APP_ID,'channel':'testing','release':{'id':a.id,'sequence':a.sequence,'protocol':a.protocol,'notes':f'Testing build {a.id}. Full campaign unverified. Boss/miniboss sync OFF.','platforms':{'windows-x86_64':wentry,'linux-x86_64':lentry}}}
    shutil.copy2(ROOT/'launcher/sst_launcher.py',out/'SST-Launcher.py')
    feed['launcher']={'version':updater.LAUNCHER_VERSION,'url':'SST-Launcher.py','size':(out/'SST-Launcher.py').stat().st_size,'sha256':updater.digest(out/'SST-Launcher.py')}
    updater.atomic_json(out/'sst-updates.json',feed)
    with tempfile.TemporaryDirectory(prefix='sst-launcher-package-',dir=ROOT/'build') as tmp:
        for platform,manifest in [('windows-x86_64',wm),('linux-x86_64',lm)]:
            stage=Path(tmp)/platform;stage.mkdir();shutil.copy2(ROOT/'launcher/sst_launcher.py',stage/'sst_launcher.py');shutil.copy2(ROOT/'.deps/sst-launcher/ca-bundle.pem',stage/'ca-bundle.pem')
            (stage/'updates').mkdir();updater.atomic_json(stage/'updates/sst-updates.json',feed)
            seed_version(stage,feed,manifest,out/feed['release']['platforms'][platform]['url'])
            readme='Skyward Sword Together — Update & Play\n\nExtract this ENTIRE ZIP into a writable folder. Open SST Update and Play.exe\non Windows or Start SST.sh on Linux. The launcher opens a local browser window.\nClick Update & Play for future SST releases; no GitHub account is needed.\n\nChoose your own prepared SSHD1.0.0 game folder once. Game assets and precompiled\ngame code are not included. First Windows setup runs in the native SST window.\nTo keep an older SST save/settings, choose its existing folder containing\nskyrecomp.ini and save/ as the save/settings folder before playing. Nothing is\nmoved or deleted. Saves/settings remain outside managed runtime versions.\n\nUpdates are checked at:\n'+updater.DEFAULT_FEED+'\nIf offline, Play installed build works without checking the channel.\nRollback changes runtime only, never your saves. Friends should use the same\nbuild/protocol shown in the launcher. This is an experimental co-op build; full\ncampaign, actual Windows hardware and internet play remain unverified.\nBoss/miniboss synchronization remains OFF.\n'
            readme+='\nFirst-time setup speed (Game folder & settings): default 1 compiler.\nSuggested starting points: 8 GB RAM -> 1, 16 GB -> 2, 32 GB or more -> 4.\nLauncher4 also offers an advanced 8-compiler option for 32 GB+ PCs with\nat least 8 logical processors and enough free memory. Reading, code generation\nand final linking may limit speed; no 8-job speed benchmark is claimed.\nMore jobs use more CPU/RAM. Save before opening game setup; the setting\napplies next time SST starts. Existing compiled game libraries are reused.\n\nFor multiplayer use Settings -> Video -> Frame rate -> 60 FPS on all PCs.\nA real two-PC playtest reported better running/stamina timing and smoother\nremote movement at 60 FPS. Launcher5 adds Host & Play and Join & Play with six-character room codes.\nUse those buttons before starting SST; friends do not need Cloudflare accounts,\nrouter port forwarding or a VPN for this mode. Keep the launcher running while\nplaying. Closing its browser tab is fine; quitting the launcher ends the room.\nThe free testing service has daily quotas and a six-hour room lifetime.\n\nA controlled Linux test verified independent remote body animations and visible\nhands for the tested outfit. Other outfits and per-player sword equipment remain\nincomplete. Supported pickups reward only the collector. Dynamic drops, many\nobject types and the complete campaign still need work; boss/miniboss sync is OFF.\n'
            (stage/'READ-ME-FIRST.txt').write_text(readme,encoding='utf-8')
            if platform.startswith('windows'):
                shutil.copytree(runtime,stage/'launcher-runtime');shutil.copy2(ROOT/'build/SST-Update-and-Play.exe',stage/'SST Update and Play.exe')
                target=out/'SST-Launcher-Windows.zip'
            else:
                start=stage/'Start SST.sh';start.write_text('#!/bin/sh\nset -eu\nSST_LAUNCHER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec python3 "$SST_LAUNCHER_DIR/sst_launcher.py" --app-dir "$SST_LAUNCHER_DIR" "$@"\n');start.chmod(0o755)
                target=out/'SST-Launcher-Linux.zip'
            zip_stage(stage,target)
    # Public source for the launcher and publisher; no credentials or local settings.
    source=out/'SST-Launcher-Source.zip';assert not source.exists()
    with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted((ROOT/'launcher').rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:z.write(p,p.relative_to(ROOT))
    (out/'SHA256SUMS.txt').write_text(''.join(updater.digest(p)+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.is_file()))
    print(json.dumps({'output':str(out),'windows_launcher_bytes':(out/'SST-Launcher-Windows.zip').stat().st_size,'linux_launcher_bytes':(out/'SST-Launcher-Linux.zip').stat().st_size,'runtime':a.id,'protocol':a.protocol},indent=2))

if __name__=='__main__':main()
