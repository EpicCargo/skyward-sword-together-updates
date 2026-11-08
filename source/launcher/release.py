#!/usr/bin/env python3
"""One-command publication of an already verified SST build; no project git writes."""
import argparse,datetime,hashlib,json,re,subprocess,sys,urllib.request,zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from sst_launcher import digest,fetch_json,location,UpdateError
from publish import github_publish
ROOT=Path(__file__).resolve().parents[1]
ONLINE_COMPANIONS={
 'online/__init__.py':'launcher/sst_online_init.py',
 'online/sst_online.py':'launcher/sst_online.py',
 'online/sst_online_transport.py':'launcher/sst_online_transport.py',
}
ONLINE_VERIFIED=('client/build/sst-session-host',*ONLINE_COMPANIONS.values(),'launcher/sst_launcher.py')
def verify_online_inputs(windows,binaries,project=None):
 """Attest new online release inputs; legacy packages retain their old contract."""
 project=Path(project) if project is not None else ROOT
 prefix='Skyward Sword Together/'
 try:
  with zipfile.ZipFile(windows) as archive:
   members={entry.filename:entry for entry in archive.infolist() if not entry.is_dir()}
   online=any(name==prefix+'sst-session-host.exe' or name.startswith(prefix+'online/') for name in members)
   if not online:return False
   required={prefix+'sst-session-host.exe',*(prefix+name for name in ONLINE_COMPANIONS)}
   if not required<=members.keys():raise UpdateError('The verified Windows package has incomplete online support.')
   for relative in ONLINE_VERIFIED:
    path=project/relative;expected=binaries.get(relative)
    if not isinstance(expected,str) or not re.fullmatch(r'[a-f0-9]{64}',expected) or path.is_symlink() or not path.is_file() or digest(path)!=expected:
     raise UpdateError('Online release inputs differ from validation; verify the helper, companion modules and launcher before publishing.')
   for member,relative in ONLINE_COMPANIONS.items():
    with archive.open(prefix+member) as source:
     if hashlib.file_digest(source,'sha256').hexdigest()!=binaries[relative]:
      raise UpdateError('Windows online companions differ from the verified launcher sources.')
 except (OSError,zipfile.BadZipFile,KeyError):
  raise UpdateError('Online release provenance could not be verified.') from None
 return True
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--verification-dir',type=Path,required=True);p.add_argument('--id',required=True);p.add_argument('--sequence',type=int,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--repo',default='EpicCargo/skyward-sword-together-updates');p.add_argument('--gh',type=Path,default=ROOT/'.deps/sst-launcher/gh_2.103.3_linux_amd64/bin/gh');p.add_argument('--previous',type=Path);p.add_argument('--publish',action='store_true');a=p.parse_args()
 e=a.verification_dir;done=json.loads((e/'finalized.json').read_text());package=json.loads((e/'package.json').read_text());bins=json.loads((e/'tested-binaries.json').read_text())
 if not done['complete'] or not package['privacy_checked']:raise UpdateError('A complete, privacy-checked native verification is required.')
 windows=Path(package['path']);linux=ROOT/'engine/build/skyrecomp'
 if digest(windows)!=package['sha256'] or digest(linux)!=bins['engine/build/skyrecomp']:raise UpdateError('Release inputs differ from the verified build; do not publish.')
 verify_online_inputs(windows,bins)
 previous=a.previous
 if previous is None:
  feed_url=f'https://github.com/{a.repo}/releases/latest/download/sst-updates.json'
  try:
   feed=fetch_json(feed_url);entry=feed['release']['platforms']['windows-x86_64'];old=fetch_json(location(entry['manifest'],feed_url),expected=entry['manifest_sha256'])
   previous=ROOT/'build'/('previous-update-'+feed['release']['id']+'.json');previous.write_text(json.dumps(old,indent=2)+'\n')
  except urllib.error.HTTPError as exc:
   if exc.code!=404:raise
 subprocess.run([sys.executable,str(ROOT/'launcher/build_bundle.py'),'--windows',str(windows),'--linux',str(linux),'--out',str(a.out),'--id',a.id,'--sequence',str(a.sequence),'--protocol',str(done['protocol']),*(['--previous',str(previous)] if previous else [])],check=True)
 if a.publish:github_publish(a.gh,a.repo,a.out,'sst-'+a.id,'Skyward Sword Together '+a.id)
 print(a.out/'sst-updates.json')
if __name__=='__main__':main()
