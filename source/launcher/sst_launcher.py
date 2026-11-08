#!/usr/bin/env python3
"""SST Update & Play. Standard library only; game/save data never enters managed updates."""
from __future__ import annotations
import argparse, ast, contextlib, ctypes, hashlib, hmac, http.server, json, os, platform, re, secrets, shutil
import socket, ssl, stat, subprocess, sys, tempfile, threading, time, urllib.error, urllib.parse, urllib.request, webbrowser, zipfile
from pathlib import Path, PurePosixPath

APP_ID = 'skyward-sword-together'
LAUNCHER_VERSION = 5
MAX_JSON = 8 * 1024 * 1024
MAX_ARCHIVE = 512 * 1024 * 1024
MAX_EXPANDED = 3 * 1024 * 1024 * 1024
RESERVED = {'save','sst-backups','user-data','exefs','mods','cache','user','.git','launcher-runtime'}
PRIVATE = {'prod.keys','title.keys','romfs.bin','skygame.dll','skygame.so','skygame.dylib','wiiking2.sav',
           'unsaved-world.bin','sst-scenes-v1.bin','skyrecomp.ini','launcher-config.json','launcher-state.json','launcher-session.json','installed-manifest.json'}
DEFAULT_FEED = 'https://github.com/EpicCargo/skyward-sword-together-updates/releases/latest/download/sst-updates.json'
DEFAULT_ROOM_SERVICE = 'https://sst-opaque-rooms.sst-epiccargo.workers.dev'

class UpdateError(Exception): pass

def setup_jobs(value):
    if type(value) is int and value in (1,2,4,8):return value
    if isinstance(value,str) and value in {'1','2','4','8'}:return int(value)
    raise UpdateError('Choose 1, 2, 4 or 8 compilers for first-time setup.')

def digest(path: Path) -> str:
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()

def runtime_file_mode(name, entrypoint):
    # Only these two root-level runtime programs may be executable on POSIX.
    return 0o755 if name in {entrypoint,'sst-session-host'} else 0o644

def atomic_json(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name+'.', dir=path.parent)
    try:
        with os.fdopen(fd,'w',encoding='utf-8') as f:
            json.dump(data,f,indent=2);f.write('\n');f.flush();os.fsync(f.fileno())
        os.replace(name,path)
    finally:
        if os.path.exists(name): os.unlink(name)

def read_json(path: Path, default):
    if not path.exists(): return default
    try:
        if path.stat().st_size > MAX_JSON: raise UpdateError('Configuration is too large.')
        return json.loads(path.read_text(encoding='utf-8'))
    except (ValueError,OSError) as exc: raise UpdateError(f'Could not read {path.name}: {exc}') from exc

def safe_name(name: str) -> str:
    if not isinstance(name,str) or not name or '\\' in name or '\0' in name:
        raise UpdateError('Invalid update filename.')
    p=PurePosixPath(name)
    if p.is_absolute() or any(x in ('','..','.') or ':' in x or x.rstrip(' .')!=x for x in name.split('/')):
        raise UpdateError('An update contains an unsafe path.')
    for segment in p.parts:
        stem=segment.split('.')[0].upper()
        if stem in {'CON','PRN','AUX','NUL'} or re.fullmatch(r'(COM|LPT)[1-9]',stem):
            raise UpdateError('An update contains a reserved filename.')
    if p.parts[0].casefold() in RESERVED or p.name.casefold() in PRIVATE or p.suffix.casefold() in {'.sav','.nsp','.xci','.nso','.screenshot'}:
        raise UpdateError('An update tried to include game files, saves or user configuration.')
    return name

def platform_key():
    if platform.machine().lower() not in {'amd64','x86_64'}: raise UpdateError('This build supports x64 PCs.')
    return ('windows' if os.name=='nt' else 'linux')+'-x86_64'

def location(value: str, base: str|None=None) -> str:
    # Resolve only relative references. An absolute HTTPS URL must never become
    # a child path of the launcher's local configuration directory.
    parsed=urllib.parse.urlparse(value)
    drive=os.name=='nt' and re.match(r'^[A-Za-z]:[\\/]',value)
    if not parsed.scheme and not Path(value).is_absolute():
        if base and urllib.parse.urlparse(base).scheme in {'https','http','file'}:
            value=urllib.parse.urljoin(base,value)
        elif base:value=str(Path(base).parent/value)
    u=urllib.parse.urlparse(value)
    if base and urllib.parse.urlparse(base).scheme in {'https','http'} and u.scheme not in {'https','http'}:
        raise UpdateError('Online channels cannot read local update files.')
    if os.name=='nt' and len(value)>2 and value[0].isalpha() and value[1]==':' and value[2] in '/\\':return str(Path(value).absolute())
    if u.scheme in {'https','file'}:return value
    if u.scheme=='http' and u.hostname in {'127.0.0.1','localhost','::1'}:return value
    if u.scheme:raise UpdateError('Online updates require HTTPS. Local folders are also supported.')
    return str(Path(value).absolute())

def open_source(value: str, ca_file: Path|None=None, timeout=45):
    u=urllib.parse.urlparse(value)
    if not u.scheme or (os.name=='nt' and re.match(r'^[A-Za-z]:[\\/]',value)):
        return Path(value).open('rb')
    ctx=ssl.create_default_context(cafile=str(ca_file) if ca_file and ca_file.exists() else None)
    request=urllib.request.Request(value,headers={'User-Agent':'SST-Update-Play/1','Cache-Control':'no-cache'})
    response=urllib.request.urlopen(request,context=ctx,timeout=timeout)
    final=urllib.parse.urlparse(response.geturl())
    if u.scheme=='https' and final.scheme!='https': response.close();raise UpdateError('An update redirected away from HTTPS.')
    return response

def fetch_json(value: str, ca_file=None, expected=None, timeout=45):
    with open_source(value,ca_file,timeout) as f: raw=f.read(MAX_JSON+1)
    if len(raw)>MAX_JSON: raise UpdateError('Update metadata is too large.')
    if expected and hashlib.sha256(raw).hexdigest()!=expected: raise UpdateError('Update metadata checksum mismatch.')
    try: return json.loads(raw)
    except ValueError as exc: raise UpdateError('The update channel did not return valid metadata.') from exc

def validate_manifest(data):
    if data.get('schema')!=1 or data.get('app')!=APP_ID or not isinstance(data.get('files'),dict):
        raise UpdateError('This is not an SST runtime manifest.')
    files=data['files'];seen=set();total=0
    if not 1<=len(files)<=30000: raise UpdateError('Unexpected update file count.')
    for name,meta in files.items():
        safe_name(name)
        if name.casefold() in seen: raise UpdateError('Duplicate filenames in update.')
        seen.add(name.casefold())
        if not isinstance(meta,dict) or not re.fullmatch(r'[a-f0-9]{64}',str(meta.get('sha256',''))): raise UpdateError('Invalid file checksum.')
        n=meta.get('size')
        if type(n)!=int or not 0<=n<=MAX_ARCHIVE: raise UpdateError('Invalid update file size.')
        total+=n
    if total>MAX_EXPANDED: raise UpdateError('Expanded update is too large.')
    entry=data.get('entrypoint');safe_name(entry)
    if entry not in files: raise UpdateError('The update has no game executable.')
    return data

def upgrade_launcher(root: Path, feed, source, ca_file=None):
    meta=feed.get('launcher',{})
    if type(meta.get('version'))!=int or meta['version']<=LAUNCHER_VERSION:return False
    if not re.fullmatch(r'[a-f0-9]{64}',str(meta.get('sha256',''))) or type(meta.get('size'))!=int or not 0<meta['size']<=512*1024:
        raise UpdateError('Invalid launcher update metadata.')
    with open_source(location(meta['url'],source),ca_file,timeout=10) as f:raw=f.read(512*1024+1)
    if len(raw)!=meta['size'] or hashlib.sha256(raw).hexdigest()!=meta['sha256']:raise UpdateError('Launcher update checksum mismatch.')
    tree=ast.parse(raw);constants={}
    for node in tree.body:
        if isinstance(node,ast.Assign) and isinstance(node.value,ast.Constant):
            for name in node.targets:
                if isinstance(name,ast.Name):constants[name.id]=node.value.value
    if constants.get('LAUNCHER_VERSION')!=meta['version'] or constants.get('APP_ID')!=APP_ID:raise UpdateError('Launcher update identity/version mismatch.')
    target=root/'sst_launcher.py';backup=root/'launcher-backups';backup.mkdir(exist_ok=True)
    if target.exists():
        saved=backup/(digest(target)+'.py')
        if not saved.exists():shutil.copy2(target,saved)
    fd,name=tempfile.mkstemp(prefix='.launcher-update-',dir=root)
    try:
        with os.fdopen(fd,'wb') as f:f.write(raw);f.flush();os.fsync(f.fileno())
        os.replace(name,target)
    finally:
        if os.path.exists(name):os.unlink(name)
    return True

class Launcher:
    def __init__(self, root: Path, target_platform=None):
        self.root=root.absolute();self.root.mkdir(parents=True,exist_ok=True)
        self.platform=target_platform or platform_key();self.ca=self.root/'ca-bundle.pem'
        default_data=str(Path.home()/'.local/share/skyward-sword-together') if self.platform.startswith('linux') else str(self.root/'user-data/skyrecomp')
        saved=read_json(self.root/'launcher-config.json',{})
        if not isinstance(saved,dict):raise UpdateError('Launcher configuration must be an object.')
        self.config={'feed':DEFAULT_FEED,'fallback_feed':'updates/sst-updates.json','game_dir':'','data_dir':default_data,**saved}
        self.config.setdefault('setup_jobs',1)
        self.state=read_json(self.root/'launcher-state.json',{'active':None,'previous':None})
        self.status={'busy':False,'message':'Ready to check for updates.','progress':0,'error':None}
        self.mutex=threading.Lock();self.operation=threading.Lock();self.game=None;self.feed=None
        self.online=None
        self._online_lock=threading.RLock();self._online_epoch=0;self._closing=False
        self.config.setdefault('room_service',DEFAULT_ROOM_SERVICE)

    def report(self,message,progress=None):
        with self.mutex:
            self.status['message']=message
            if progress is not None:self.status['progress']=progress

    def snapshot(self):
        with self.mutex: result=dict(self.status)
        result.update({'app':APP_ID,'launcher_version':LAUNCHER_VERSION,'platform':self.platform,'active':self.state.get('active'),
                       'previous':self.state.get('previous'),'config':dict(self.config),'running':bool(self.game and self.game.poll() is None),
                       'latest':self.feed.get('release') if self.feed else None})
        online=self.online
        result['online']=online.snapshot() if online else {'state':'offline','code':'','role':''}
        return result

    @contextlib.contextmanager
    def install_lock(self):
        lock=self.root/'operation.lock';f=lock.open('a+b')
        try:
            if os.name=='nt':
                import msvcrt
                f.seek(0);f.write(b'0');f.flush();f.seek(0)
                try: msvcrt.locking(f.fileno(),msvcrt.LK_NBLCK,1)
                except OSError as exc: raise UpdateError('Another SST launcher is updating. Wait for it to finish.') from exc
            else:
                import fcntl
                try:fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
                except BlockingIOError as exc:raise UpdateError('Another SST launcher is updating. Wait for it to finish.') from exc
            yield
        finally:f.close()

    def save_config(self, values):
        if self.status['busy'] or self.running_processes(): raise UpdateError('Close the game and finish the current update before changing settings.')
        candidate=dict(self.config)
        if 'setup_jobs' in values:candidate['setup_jobs']=setup_jobs(values['setup_jobs'])
        for name in ['feed','game_dir','data_dir','room_service']:
            if name in values:
                value=values[name]
                if not isinstance(value,str) or len(value)>2048 or '\0' in value:raise UpdateError('Invalid setting.')
                if name=='room_service':
                    if value:
                        parsed=urllib.parse.urlsplit(value)
                        if parsed.scheme not in {'https','http'} or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or (parsed.scheme=='http' and parsed.hostname not in {'127.0.0.1','localhost','::1'}):raise UpdateError('Use an HTTPS room service. Development HTTP is loopback only.')
                elif name=='feed':location(value)
                elif value:value=str(Path(value).expanduser().absolute())
                candidate[name]=value
        if not candidate.get('data_dir'):raise UpdateError('Choose a save/settings folder.')
        for key in ['data_dir','game_dir']:
            if candidate.get(key):
                chosen=Path(candidate[key]).resolve()
                if any(chosen.is_relative_to((self.root/folder).resolve()) for folder in ['versions','cache','launcher-runtime']):raise UpdateError('Game files and saves must stay outside managed runtime folders.')
        atomic_json(self.root/'launcher-config.json',candidate)
        self.config=candidate

    def check(self, allow_fallback=False):
        source=location(self.config['feed'],str(self.root/'launcher-config.json'));using_fallback=False
        try:data=fetch_json(source,self.ca)
        except (OSError,ValueError,urllib.error.URLError) as exc:
            fallback=location(self.config.get('fallback_feed','updates/sst-updates.json'),str(self.root/'launcher-config.json'))
            if not allow_fallback:raise UpdateError(f'Could not check the online channel. Your installed build is unchanged. {exc}') from exc
            try:data=fetch_json(fallback,self.ca);source=fallback;using_fallback=True
            except OSError:raise UpdateError(f'No update channel is reachable. Your installed build is unchanged. {exc}') from exc
            self.report('Online channel unavailable; using the included verified release.')
        if data.get('schema')!=1 or data.get('app')!=APP_ID or not isinstance(data.get('release'),dict):raise UpdateError('This channel is not for Skyward Sword Together.')
        release=data['release'];rid=release.get('id','')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}',rid):raise UpdateError('Invalid release identifier.')
        if type(release.get('sequence'))!=int or type(release.get('protocol'))!=int:raise UpdateError('Invalid release version.')
        active=self.state.get('active')
        if active and release['sequence']<active['sequence']:
            if using_fallback:data['release']=dict(active);release=data['release']
            else:raise UpdateError('The channel offers an older build. Use Roll back explicitly to downgrade.')
        self.feed=data;self.feed['_source']=source;self.feed['_fallback']=using_fallback
        return data

    def running_processes(self):
        if self.game and self.game.poll() is None:return True
        if os.name=='nt':
            TH32CS_SNAPPROCESS=2
            class Entry(ctypes.Structure):
                _fields_=[('size',ctypes.c_uint32),('usage',ctypes.c_uint32),('pid',ctypes.c_uint32),('heap',ctypes.c_size_t),('module',ctypes.c_uint32),('threads',ctypes.c_uint32),('parent',ctypes.c_uint32),('priority',ctypes.c_long),('flags',ctypes.c_uint32),('exe',ctypes.c_wchar*260)]
            k=ctypes.WinDLL('kernel32',use_last_error=True);k.CreateToolhelp32Snapshot.restype=ctypes.c_void_p
            h=k.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)
            if h in (None,ctypes.c_void_p(-1).value):raise UpdateError('Could not check whether SST is running.')
            try:
                e=Entry();e.size=ctypes.sizeof(e);ok=k.Process32FirstW(ctypes.c_void_p(h),ctypes.byref(e))
                while ok:
                    if e.exe.lower()=='skyrecomp.exe':return True
                    ok=k.Process32NextW(ctypes.c_void_p(h),ctypes.byref(e))
            finally:k.CloseHandle(ctypes.c_void_p(h))
        else:
            for p in Path('/proc').glob('[0-9]*'):
                try:
                    if (p/'comm').read_text().strip()=='skyrecomp':return True
                except OSError:pass
        return False

    def active_dir(self):
        active=self.state.get('active')
        if not active:raise UpdateError('Install an SST release first with Update & Play.')
        rid=active['id']
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}',rid):raise UpdateError('Invalid installed version record.')
        return self.root/'versions'/rid

    def update(self):
        if self.running_processes():raise UpdateError('Close Skyward Sword Together before updating. Saves and settings stay in place.')
        data=self.check(allow_fallback=True);release=data['release'];active=self.state.get('active')
        if active and active['id']==release['id']:
            self.verify_entrypoint();self.report('Using your installed build; the online channel could not be checked.' if data['_fallback'] else 'You have the latest available SST build.',100);return False
        package=release.get('platforms',{}).get(self.platform)
        if not package:raise UpdateError('This release has no build for your operating system.')
        if active and active['id'] in package.get('patches',{}):package={**package,**package['patches'][active['id']],'kind':'patch'}
        for key in ['manifest_sha256','sha256']:
            if not re.fullmatch(r'[0-9a-f]{64}',str(package.get(key,''))):raise UpdateError('Missing update checksum.')
        size=package.get('size')
        if type(size)!=int or not 0<size<=MAX_ARCHIVE:raise UpdateError('Invalid download size.')
        manifest=validate_manifest(fetch_json(location(package['manifest'],data['_source']),self.ca,package['manifest_sha256']))
        if not isinstance(package.get('prefix',''),str) or '\\' in package.get('prefix',''):raise UpdateError('Invalid archive prefix.')
        if manifest.get('release_id')!=release['id'] or manifest.get('platform')!=self.platform:raise UpdateError('Update manifest version mismatch.')
        cache=self.root/'cache';cache.mkdir(exist_ok=True);archive=cache/(package['sha256']+'.zip')
        if not archive.exists() or archive.stat().st_size!=size or digest(archive)!=package['sha256']:
            temp=archive.with_suffix('.part');h=hashlib.sha256();total=0
            self.report('Downloading SST update…',2)
            try:
                with open_source(location(package['url'],data['_source']),self.ca) as src,temp.open('wb') as dst:
                    while chunk:=src.read(1024*1024):
                        total+=len(chunk)
                        if total>size:raise UpdateError('Download exceeds its declared size.')
                        dst.write(chunk);h.update(chunk);self.report('Downloading SST update…',2+int(65*total/size))
                if total!=size or h.hexdigest()!=package['sha256']:raise UpdateError('Download checksum failed. Your installed build is unchanged.')
                os.replace(temp,archive)
            finally:temp.unlink(missing_ok=True)
        versions=self.root/'versions';versions.mkdir(exist_ok=True);stage=Path(tempfile.mkdtemp(prefix='.staging-',dir=versions))
        final=versions/release['id']
        try:
            files=manifest['files'];self.report('Verifying and preparing the new version…',70)
            with zipfile.ZipFile(archive) as z:
                prefix=package.get('prefix','');members={};seen=set()
                for item in z.infolist():
                    if item.is_dir():continue
                    if prefix and not item.filename.startswith(prefix):raise UpdateError('Unexpected archive root.')
                    name=item.filename[len(prefix):] if prefix else item.filename;safe_name(name)
                    mode=(item.external_attr>>16)&0xffff
                    if stat.S_ISLNK(mode) or name.casefold() in seen:raise UpdateError('Unsafe or duplicate archive entry.')
                    seen.add(name.casefold());members[name]=item
                if package.get('kind','full')=='full' and set(members)!=set(files):raise UpdateError('Update archive and manifest disagree.')
                if not set(members)<=set(files):raise UpdateError('Unexpected update file.')
                base=self.active_dir() if active else None
                for index,(name,meta) in enumerate(files.items()):
                    target=stage/Path(name);target.parent.mkdir(parents=True,exist_ok=True)
                    if name in members:
                        info=members[name]
                        if info.file_size!=meta['size']:raise UpdateError('Expanded file size mismatch.')
                        with z.open(info) as src,target.open('xb') as dst:shutil.copyfileobj(src,dst,1024*1024)
                    elif base and (base/name).is_file() and (base/name).stat().st_size==meta['size'] and digest(base/name)==meta['sha256']:
                        try:os.link(base/name,target)
                        except OSError:shutil.copy2(base/name,target)
                    else:raise UpdateError('This patch needs a full package for your installed build.')
                    if target.stat().st_size!=meta['size'] or digest(target)!=meta['sha256']:raise UpdateError('Installed file checksum mismatch.')
                    if os.name!='nt':target.chmod(runtime_file_mode(name,manifest['entrypoint']))
                    if index%100==0:self.report('Verifying the new version…',70+int(28*(index+1)/len(files)))
            atomic_json(stage/'installed-manifest.json',manifest)
            if final.exists():
                # An already prepared immutable version can be activated after validating it.
                for name,meta in files.items():
                    if not (final/name).is_file() or digest(final/name)!=meta['sha256']:raise UpdateError('An existing version folder differs; it was preserved for inspection.')
            else:os.rename(stage,final)
            next_state={'active':{**{k:release[k] for k in ['id','sequence','protocol']},'entrypoint':manifest['entrypoint']},'previous':active}
            atomic_json(self.root/'launcher-state.json',next_state);self.state=next_state
            self.report('SST is updated. Your game files, saves and settings were preserved.',100);return True
        finally:
            if stage.exists():shutil.rmtree(stage)

    def verify_entrypoint(self):
        folder=self.active_dir();manifest=read_json(folder/'installed-manifest.json',None)
        if not manifest:raise UpdateError('Installed manifest is missing. Reinstall this release.')
        validate_manifest(manifest);entry=manifest['entrypoint'];target=folder/entry
        if not target.is_file() or digest(target)!=manifest['files'][entry]['sha256']:raise UpdateError('The installed executable changed. Reinstall before playing.')
        return target

    def rollback(self):
        if self.running_processes():raise UpdateError('Close SST before rolling back.')
        previous=self.state.get('previous')
        if not previous:raise UpdateError('There is no previous version yet.')
        old=self.state;self.state={'active':previous,'previous':old.get('active')}
        try:self.verify_entrypoint();atomic_json(self.root/'launcher-state.json',self.state)
        except Exception:self.state=old;raise
        self.report('Previous runtime restored. Saves and settings were not changed.',100)

    def play(self, setup=False):
        with self._online_lock:
            if self._closing:raise UpdateError('The launcher is closing.')
        if self.running_processes():raise UpdateError('SST is already running.')
        executable=self.verify_entrypoint();data=Path(self.config['data_dir']).expanduser().absolute();data.mkdir(parents=True,exist_ok=True)
        env=dict(os.environ,LARECOMP_DATA_ROOT=str(data),SST_BUILD_JOBS=str(setup_jobs(self.config.get('setup_jobs',1))));game=self.config.get('game_dir','');args=[str(executable)]
        online=self.online
        if online:
            session_env=online.environment()
            if not session_env:raise UpdateError('The online room closed before SST could start. Create or join a room again.')
            env.update(session_env)
        if game:
            game_path=Path(game)
            if not (game_path/'exefs/main').is_file() or not (game_path/'romfs.bin').is_file():raise UpdateError('Choose your prepared SSHD1.0.0 folder containing exefs and romfs.bin.')
            library='skygame.dll' if self.platform.startswith('windows') else 'skygame.so'
            if not setup and (game_path/library).is_file():args.append('--play')
            args.append(str(game_path))
        if self.platform.startswith('linux') and shutil.which('clang'):env.setdefault('LARECOMP_CC',shutil.which('clang'))
        with self._online_lock:
            if self._closing:raise UpdateError('The launcher is closing.')
            with (self.root/'game-launch.log').open('ab') as log:self.game=subprocess.Popen(args,cwd=executable.parent,env=env,stdout=log,stderr=subprocess.STDOUT)
        self.report(f'SST started. First-time setup uses {env["SST_BUILD_JOBS"]} compiler(s); completed game libraries are reused.',100)

    def begin_shutdown(self):
        with self._online_lock:
            self._closing=True;self._online_epoch+=1

    def close_online(self, *, shutdown=False):
        with self._online_lock:
            if shutdown:self._closing=True
            self._online_epoch+=1
            online,self.online=self.online,None
        if online:online.close()

    def online_runtime(self):
        # Online code is shipped as checksum-verified runtime files, so existing
        # single-file launcher upgrades also receive all companion modules.
        import importlib.util
        folder=self.active_dir()/'online'
        manifest=read_json(self.active_dir()/'installed-manifest.json',{})
        for name in ['__init__.py','sst_online.py','sst_online_transport.py']:
            path=folder/name;expected=manifest.get('files',{}).get('online/'+name,{})
            if not path.is_file() or path.is_symlink() or digest(path)!=expected.get('sha256'):raise UpdateError('Update to an online-enabled SST runtime before joining.')
        for name in list(sys.modules):
            if name=='_sst_online_runtime' or name.startswith('_sst_online_runtime.'):del sys.modules[name]
        spec=importlib.util.spec_from_file_location('_sst_online_runtime',folder/'__init__.py',submodule_search_locations=[str(folder)])
        module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
        return module

    def online_action(self, action, code=''):
        if not self.operation.acquire(False):raise UpdateError('Another launcher action is already running.')
        with self.mutex:self.status.update(busy=True,error=None,progress=0)
        try:
            with self._online_lock:
                if self._closing:raise UpdateError('The launcher is closing.')
                epoch=self._online_epoch
            if action=='leave':
                self.close_online();self.report('Online room closed. Your game can keep running offline.',100);return
            if action not in {'host','join'}:raise UpdateError('Unknown online action.')
            if self.running_processes():raise UpdateError('Close SST before starting a new online room, then use Host & Play or Join & Play.')
            if self.online:raise UpdateError('Leave the current online room first.')
            service=self.config.get('room_service','')
            if not service:raise UpdateError('The SST online service has not been configured yet.')
            self.verify_entrypoint()
            helper=self.active_dir()/('sst-session-host.exe' if self.platform.startswith('windows') else 'sst-session-host')
            if action=='host':
                helper=self.verify_online_helper()
            module=self.online_runtime()
            lease=self.root/'online-sessions'/('sst-'+secrets.token_hex(16)+'.lease')
            online=module.OnlineSession(service,helper=helper,ca=self.ca if self.ca.is_file() else None,lease=lease)
            try:
                online.host() if action=='host' else online.join(code)
                with self._online_lock:
                    if self._closing or epoch!=self._online_epoch:
                        raise UpdateError('The online connection was cancelled before SST could start.')
                    # Commit the room and game launch together with respect to
                    # shutdown. A pending join cannot publish after cleanup.
                    self.online=online;self.play()
            except Exception:
                try:online.close()
                finally:
                    with self._online_lock:
                        if self.online is online:self.online=None
                raise
            self.report('Room created. Share the code with your friends.' if action=='host' else 'SST started. Connecting to the room…',100)
        except Exception as exc:
            with self.mutex:self.status.update(error=str(exc),message=str(exc))
            raise
        finally:
            with self.mutex:self.status['busy']=False
            self.operation.release()

    def verify_online_helper(self):
        helper=self.active_dir()/('sst-session-host.exe' if self.platform.startswith('windows') else 'sst-session-host')
        manifest=read_json(self.active_dir()/'installed-manifest.json',{})
        expected=manifest.get('files',{}).get(helper.name,{})
        if helper.is_symlink() or not helper.is_file() or helper.stat().st_size!=expected.get('size') or digest(helper)!=expected.get('sha256'):
            raise UpdateError('Update to an online-enabled SST runtime before hosting.')
        # Launcher4 installed every companion as0644. Repair this exact verified
        # helper when a Launcher5 upgrade reuses an already installed runtime.
        if self.platform.startswith('linux') and os.name!='nt':helper.chmod(0o755)
        return helper

    def run(self, action):
        if not self.operation.acquire(False):raise UpdateError('Another launcher action is already running.')
        with self.mutex:self.status.update(busy=True,error=None,progress=0)
        try:
            with self.install_lock():
                if self.online and action in {'update','update-play','rollback','setup'}:raise UpdateError('Leave the online room before updating or changing the runtime.')
                self.state=read_json(self.root/'launcher-state.json',self.state)
                if action=='check':self.check();self.report('Update channel checked.',100)
                elif action in {'update','update-play'}:
                    self.update()
                    if action=='update-play':self.play()
                elif action=='play':self.play()
                elif action=='setup':self.play(True)
                elif action=='rollback':self.rollback()
                else:raise UpdateError('Unknown action.')
        except Exception as exc:
            with self.mutex:self.status['error']=str(exc);self.status['message']=str(exc)
            raise
        finally:
            with self.mutex:self.status['busy']=False
            self.operation.release()

def choose_folder():
    if os.name=='nt':
        class Browse(ctypes.Structure):
            _fields_=[('owner',ctypes.c_void_p),('root',ctypes.c_void_p),('display',ctypes.c_wchar_p),('title',ctypes.c_wchar_p),('flags',ctypes.c_uint),('callback',ctypes.c_void_p),('param',ctypes.c_ssize_t),('image',ctypes.c_int)]
        shell=ctypes.WinDLL('shell32');ole=ctypes.WinDLL('ole32');ole.CoInitialize(None)
        try:
            display=ctypes.create_unicode_buffer(32768);info=Browse(None,None,ctypes.cast(display,ctypes.c_wchar_p),'Choose a folder',0x41,None,0,0)
            shell.SHBrowseForFolderW.restype=ctypes.c_void_p;p=shell.SHBrowseForFolderW(ctypes.byref(info))
            if not p:return ''
            try:
                result=ctypes.create_unicode_buffer(32768)
                if not shell.SHGetPathFromIDListEx(ctypes.c_void_p(p),result,len(result),0):raise UpdateError('Could not read that folder path.')
                return result.value
            finally:ole.CoTaskMemFree(ctypes.c_void_p(p))
        finally:ole.CoUninitialize()
    import tkinter as tk
    from tkinter import filedialog
    root=tk.Tk();root.withdraw()
    try:return filedialog.askdirectory(title='Choose a folder')
    finally:root.destroy()

class Server(http.server.ThreadingHTTPServer):
    daemon_threads=True
    def __init__(self,address,app):
        self.app=app;self.token=secrets.token_urlsafe(32);super().__init__(address,Handler)

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def send(self,status,data,kind='application/json'):
        raw=data if isinstance(data,bytes) else json.dumps(data).encode();self.send_response(status)
        self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(raw)));self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff');self.send_header('X-Frame-Options','DENY');self.end_headers();self.wfile.write(raw)
    def authorized(self):
        host=f'127.0.0.1:{self.server.server_port}';origin=self.headers.get('Origin')
        return self.headers.get('Host')==host and (not origin or origin==f'http://{host}') and hmac.compare_digest(self.headers.get('Authorization',''), 'Bearer '+self.server.token)
    def do_GET(self):
        if self.headers.get('Host')!=f'127.0.0.1:{self.server.server_port}':self.send(403,{'error':'Invalid host'});return
        if self.path=='/':
            self.send(200,UI.encode(),'text/html; charset=utf-8');return
        if not self.authorized():self.send(403,{'error':'Open the launcher shortcut to authorize this local window.'});return
        if self.path=='/api/status':self.send(200,self.server.app.snapshot())
        else:self.send(404,{'error':'Not found'})
    def do_POST(self):
        if not self.authorized():self.send(403,{'error':'Unauthorized'});return
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<=size<=16384:raise UpdateError('Request too large.')
            data=json.loads(self.rfile.read(size) or b'{}')
            if self.path=='/api/config':self.server.app.save_config(data);self.send(200,{'ok':True})
            elif self.path=='/api/browse':self.send(200,{'path':choose_folder()})
            elif self.path=='/api/action':
                action=data.get('action')
                if action not in {'check','update','update-play','play','setup','rollback'}:raise UpdateError('Unknown action.')
                def work():
                    try:self.server.app.run(action)
                    except Exception:pass
                threading.Thread(target=work,daemon=True).start();self.send(202,{'ok':True})
            elif self.path=='/api/online':
                action=data.get('action');code=data.get('code','')
                if action not in {'host','join','leave'} or not isinstance(code,str):raise UpdateError('Invalid online action.')
                def online_work():
                    try:self.server.app.online_action(action,code)
                    except Exception:pass
                threading.Thread(target=online_work,daemon=True).start();self.send(202,{'ok':True})
            elif self.path=='/api/quit':
                self.server.app.begin_shutdown()
                try:self.send(200,{'ok':True})
                finally:threading.Thread(target=self.server.shutdown,daemon=True).start()
            else:self.send(404,{'error':'Not found'})
        except (ValueError,UpdateError,OSError) as exc:
            with self.server.app.mutex:self.server.app.status.update(error=str(exc),message=str(exc))
            self.send(400,{'error':str(exc)})

UI = r'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Skyward Sword Together · Update & Play</title>
<style>:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101b22;color:#ecf3ee}body{max-width:850px;margin:7vh auto;padding:24px}small,.muted{color:#aebfb7}h1{font-size:clamp(28px,5vw,46px);margin:.35em 0}header{border-left:4px solid #c7b272;padding-left:22px}main{margin-top:32px;background:#1a2a30;border:1px solid #3b4e50;border-radius:18px;padding:26px}button{border:1px solid #61766f;background:#243b41;color:#fff;border-radius:10px;padding:13px 20px;cursor:pointer;font-weight:650}button.primary{background:#c6b67f;color:#172328;border:0;font-size:20px;padding:18px 30px}button:disabled{opacity:.5;cursor:wait}.row{display:flex;flex-wrap:wrap;gap:12px;align-items:center;margin:18px 0}progress{width:100%;height:14px;accent-color:#c6b67f}input,select{width:100%;box-sizing:border-box;background:#101b22;color:#eaf1ed;border:1px solid #4c6263;border-radius:8px;padding:12px;margin:6px 0 14px}label{display:block;color:#cbd8d1}details{margin-top:24px}summary{cursor:pointer}.error{color:#ffb5a7}.pill{border:1px solid #526963;border-radius:18px;padding:5px 11px;font-size:13px}footer{font-size:13px;margin-top:25px;color:#acbbb7}a{color:#dacb97}</style>
<header><small>SKYWARD SWORD TOGETHER</small><h1>Update once. Play together.</h1><p class="muted">Your game files and saves stay with you. The launcher handles SST updates.</p></header>
<main><div class="row"><span class="pill" id="version">No runtime installed</span><span class="pill" id="protocol">Testing build</span><span class="pill" id="launcher">Launcher</span></div><p id="message">Connecting to the local launcher…</p><progress id="progress" max="100" value="0"></progress>
<div class="row"><button class="primary" data-action="update-play">Update & Play</button><button data-action="play">Play installed build</button><button data-action="setup">Open game setup</button></div><p class="muted" id="notes">First time? Choose your prepared SSHD1.0.0 game folder below. Game files are not included.</p>
<section><h2>Play online</h2><p class="muted">Create a room and share its code, or enter your friend's code. No router setup or VPN. Start an online room before launching the game.</p><div class="row"><button data-online="host">Host &amp; Play</button><input id="room-code" maxlength="6" placeholder="Room code" autocomplete="off"><button data-online="join">Join &amp; Play</button><button data-online="leave">Leave room</button></div><p id="online-status" class="muted">Online service is not configured yet.</p><p class="muted">Closing the launcher ends its online room. You can close the browser tab and leave the launcher running.</p></section>
<details id="settings"><summary>Game folder & settings</summary><p class="muted">Choose these once. To keep an older SST save, select its existing folder containing skyrecomp.ini and save/ as the save/settings folder.</p>
<label>Prepared game folder<div class="row"><input id="game_dir" placeholder="Folder containing exefs/ and romfs.bin"><button id="browse-game">Browse</button></div></label>
<label>Save and settings folder<div class="row"><input id="data_dir"><button id="browse-data">Browse</button></div></label>
<label for="setup_jobs">First-time setup speed</label><select id="setup_jobs"><option value="1">Low memory — 1 compiler (default)</option><option value="2">Faster — 2 compilers</option><option value="4">Faster — 4 compilers</option><option value="8">8 compilers (advanced)</option></select><p class="muted">More compilers use more CPU and RAM. Suggested starting points: 8 GB RAM → 1, 16 GB → 2, 32 GB or more → 4. With 32 GB or more, at least 8 logical CPU processors and plenty of free memory, you can try 8. More jobs help only while several files are compiling; reading and final linking may still limit speed. Lower the setting if memory is tight. Save before opening game setup. Changes apply the next time SST starts; finished libraries are reused on normal launches.</p>
<label>Update channel<input id="feed"></label><label>Online room service<input id="room_service" placeholder="https://SST-service.workers.dev"></label><div class="row"><button id="save">Save settings</button><button data-action="check">Check for updates</button><button data-action="update">Update only</button><button data-action="rollback">Roll back runtime</button></div><p class="muted">Rollback changes the runtime only. It never restores or deletes your saves. Friends should use the same build and protocol.</p></details></main>
<footer>This is an experimental co-op build. Boss/miniboss sync stays off. <button id="quit">Close launcher</button></footer>
<script>const token=new URLSearchParams(location.hash.slice(1)).get('token')||'';let populated=false;const $=x=>document.getElementById(x);async function api(path,body){const r=await fetch('/api/'+path,{method:body?'POST':'GET',headers:{'Authorization':'Bearer '+token,...(body?{'Content-Type':'application/json'}:{})},body:body?JSON.stringify(body):undefined});const d=await r.json();if(!r.ok)throw Error(d.error||'Request failed');return d}function error(e){$('message').textContent=e.message;$('message').className='error'}async function refresh(){try{const d=await api('status');$('version').textContent=d.active?d.active.id:'No runtime installed';$('protocol').textContent=d.active?'Protocol '+d.active.protocol:'Testing build';$('launcher').textContent='Launcher '+d.launcher_version;$('message').textContent=d.message;$('message').className=d.error?'error':'';$('progress').value=d.progress;document.querySelectorAll('[data-action]').forEach(b=>b.disabled=d.busy||d.running);const online=d.online||{};$('online-status').textContent=online.state==='error'?online.message:online.code?('Room '+online.code+' · '+(online.message||online.state)): (d.config.room_service?'Ready to create or join a room.':'Online service is not configured yet.');document.querySelectorAll('[data-online]').forEach(b=>b.disabled=d.busy||(b.dataset.online!=='leave'&&(d.running||!d.config.room_service)));if(!populated){for(const k of ['game_dir','data_dir','feed','setup_jobs','room_service'])$(k).value=d.config[k]||'';populated=true;if(!d.config.game_dir)$('settings').open=true}}catch(e){error(e)}}document.querySelectorAll('[data-action]').forEach(b=>b.onclick=async()=>{try{await api('config',Object.fromEntries(['game_dir','data_dir','feed','setup_jobs','room_service'].map(k=>[k,$(k).value])));await api('action',{action:b.dataset.action});setTimeout(refresh,100)}catch(e){error(e)}});$('save').onclick=async()=>{try{await api('config',Object.fromEntries(['game_dir','data_dir','feed','setup_jobs','room_service'].map(k=>[k,$(k).value])));$('message').textContent='Settings saved.'}catch(e){error(e)}};for(const [id,key]of[['browse-game','game_dir'],['browse-data','data_dir']])$(id).onclick=async()=>{try{const d=await api('browse',{});if(d.path){$(key).value=d.path;await api('config',{[key]:d.path})}}catch(e){error(e)}};document.querySelectorAll('[data-online]').forEach(b=>b.onclick=async()=>{try{if(b.dataset.online!=='leave')await api('config',Object.fromEntries(['game_dir','data_dir','feed','setup_jobs','room_service'].map(k=>[k,$(k).value])));await api('online',{action:b.dataset.online,code:$('room-code').value.trim().toUpperCase()});setTimeout(refresh,100)}catch(e){error(e)}});$('quit').onclick=async()=>{await api('quit',{});document.body.innerHTML='<h1>Launcher closed.</h1><p>You can close this tab.</p>'};refresh();setInterval(refresh,1200);</script></html>'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--app-dir',type=Path,default=Path(__file__).resolve().parent);p.add_argument('--no-browser',action='store_true');p.add_argument('--port',type=int,default=0);p.add_argument('--cli',choices=['status','check','update','update-play','play','setup','rollback']);p.add_argument('--choose-folder',action='store_true');p.add_argument('--wait-game',action='store_true');args=p.parse_args()
    if args.choose_folder:print(json.dumps({'path':choose_folder()}));return
    app=Launcher(args.app_dir)
    if args.cli:
        if args.cli!='status':app.run(args.cli)
        if args.wait_game and app.game:app.game.wait()
        print(json.dumps(app.snapshot(),indent=2));return
    if not args.no_browser and Path(__file__).resolve().parent==app.root.resolve():
        try:
            if not app.running_processes():
                source=location(app.config['feed'],str(app.root/'launcher-config.json'))
                candidate=fetch_json(source,app.ca,timeout=4)
                if candidate.get('app')==APP_ID and candidate.get('schema')==1 and upgrade_launcher(app.root,candidate,source,app.ca):
                    os.execv(sys.executable,[sys.executable,'-I',str(app.root/'sst_launcher.py'),'--app-dir',str(app.root)])
        except (OSError,ValueError,UpdateError):pass
    previous=read_json(app.root/'launcher-session.json',{})
    try:
        old_url=previous.get('url','');old=urllib.parse.urlparse(old_url)
        if old.scheme=='http' and old.hostname=='127.0.0.1' and old.port:
            token=urllib.parse.parse_qs(old.fragment).get('token',[''])[0]
            req=urllib.request.Request(f'http://127.0.0.1:{old.port}/api/status',headers={'Authorization':'Bearer '+token})
            with urllib.request.urlopen(req,timeout=.5) as response:existing=json.load(response)
            if existing.get('app')==APP_ID and existing.get('launcher_version',0)<LAUNCHER_VERSION:
                quit_req=urllib.request.Request(f'http://127.0.0.1:{old.port}/api/quit',data=b'{}',headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
                with urllib.request.urlopen(quit_req,timeout=2) as response:response.read()
            elif existing.get('app')==APP_ID:
                print(old_url,flush=True)
                if not args.no_browser:webbrowser.open(old_url)
                return
    except (OSError,ValueError):pass
    server=Server(('127.0.0.1',args.port),app);url=f'http://127.0.0.1:{server.server_port}/#token={server.token}'
    atomic_json(app.root/'launcher-session.json',{'url':url,'pid':os.getpid()});print(url,flush=True)
    if not args.no_browser:webbrowser.open(url)
    try:server.serve_forever()
    finally:
        try:app.close_online(shutdown=True)
        finally:server.server_close()

if __name__=='__main__':
    try:main()
    except Exception as exc:
        log=Path(__file__).resolve().parent/'launcher-error.log'
        with contextlib.suppress(OSError):log.write_text(str(exc)+'\n',encoding='utf-8')
        if os.name=='nt' and '--cli' not in sys.argv and '--no-browser' not in sys.argv:ctypes.windll.user32.MessageBoxW(None,str(exc),'Skyward Sword Together Launcher',0x10)
        else:print(str(exc),file=sys.stderr)
        sys.exit(1)
