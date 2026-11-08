import contextlib,hashlib,importlib.util,json,os,sys,tempfile,threading,time,unittest,urllib.request,urllib.error,zipfile
from pathlib import Path
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import sst_launcher as m

class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.base=Path(self.tmp.name);self.releases=self.base/'releases';self.releases.mkdir();self.app=m.Launcher(self.base/'app',target_platform='linux-x86_64');self.app.running_processes=lambda:False
        self.app.config['feed']=str(self.releases/'feed.json');self.data=self.base/'private';self.data.mkdir();(self.data/'save.sav').write_bytes(b'personal save');self.app.config['data_dir']=str(self.data)
    def tearDown(self):self.tmp.cleanup()
    def release(self,rid='v1',sequence=1,files=None,patch=None,manifest_changes=None,unsafe_entries=None):
        files=files or {'skyrecomp':b'#!/bin/sh\nexit 0\n','compiler/header.h':b'SDK'}
        manifest={'schema':1,'app':m.APP_ID,'platform':'linux-x86_64','release_id':rid,'entrypoint':'skyrecomp','files':{n:{'sha256':hashlib.sha256(b).hexdigest(),'size':len(b)} for n,b in files.items()}}
        if manifest_changes:manifest_changes(manifest)
        mp=self.releases/(rid+'.json');m.atomic_json(mp,manifest);zp=self.releases/(rid+'.zip')
        with zipfile.ZipFile(zp,'w',zipfile.ZIP_DEFLATED) as z:
            for n,b in (unsafe_entries or files).items():
                if patch is None or n in patch:z.writestr(n,b)
        entry={'url':zp.name,'sha256':m.digest(zp),'size':zp.stat().st_size,'manifest':mp.name,'manifest_sha256':m.digest(mp),'kind':'patch' if patch else 'full'}
        m.atomic_json(self.releases/'feed.json',{'schema':1,'app':m.APP_ID,'release':{'id':rid,'sequence':sequence,'protocol':45,'platforms':{'linux-x86_64':entry}}})
        return files,manifest
    def test_full_install_preserves_private_data(self):
        self.release();self.app.run('update');self.assertEqual(self.app.state['active']['id'],'v1');self.assertEqual((self.data/'save.sav').read_bytes(),b'personal save');self.assertEqual((self.app.active_dir()/'compiler/header.h').read_bytes(),b'SDK')

    @unittest.skipIf(os.name=='nt','POSIX runtime modes')
    def test_online_helper_executable_after_update_and_bundle_seed(self):
        import build_bundle
        files={'skyrecomp':b'game','sst-session-host':b'helper','online/sst_online.py':b'code'}
        _,manifest=self.release(files=files);self.app.run('update')
        self.assertEqual((self.app.active_dir()/'sst-session-host').stat().st_mode&0o777,0o755)
        self.assertEqual((self.app.active_dir()/'online/sst_online.py').stat().st_mode&0o777,0o644)
        stage=self.base/'seed';stage.mkdir()
        feed=json.loads((self.releases/'feed.json').read_text())
        build_bundle.seed_version(stage,feed,manifest,self.releases/'v1.zip')
        self.assertEqual((stage/'versions/v1/sst-session-host').stat().st_mode&0o777,0o755)
        self.assertEqual((stage/'versions/v1/online/sst_online.py').stat().st_mode&0o777,0o644)

    @unittest.skipIf(os.name=='nt','POSIX runtime modes')
    def test_verified_helper_repairs_launcher4_mode_but_rejects_changed_content(self):
        self.release(files={'skyrecomp':b'game','sst-session-host':b'helper'});self.app.run('update')
        helper=self.app.active_dir()/'sst-session-host';helper.chmod(0o644)
        self.assertEqual(self.app.verify_online_helper(),helper)
        self.assertEqual(helper.stat().st_mode&0o777,0o755)
        helper.write_bytes(b'changed');helper.chmod(0o644)
        with self.assertRaises(m.UpdateError):self.app.verify_online_helper()
        self.assertEqual(helper.stat().st_mode&0o777,0o644)
    def test_patch_rollback_and_removed_files(self):
        self.release(files={'skyrecomp':b'old','compiler/header.h':b'SDK','removed.txt':b'old'});self.app.run('update');old=self.app.active_dir()
        self.release('v2',2,{'skyrecomp':b'new','compiler/header.h':b'SDK'},patch={'skyrecomp'});self.app.run('update');self.assertEqual((old/'skyrecomp').read_bytes(),b'old');self.assertEqual((self.app.active_dir()/'skyrecomp').read_bytes(),b'new');self.assertFalse((self.app.active_dir()/'removed.txt').exists());self.app.run('rollback');self.assertEqual(self.app.state['active']['id'],'v1');self.assertEqual((self.data/'save.sav').read_bytes(),b'personal save')
    def test_bad_download_checksum_preserves_active(self):
        self.release();self.app.run('update');self.release('v2',2,files={'skyrecomp':b'changed','compiler/header.h':b'SDK'});p=self.releases/'v2.zip';p.write_bytes(p.read_bytes()+b'bad')
        with self.assertRaises(m.UpdateError):self.app.run('update')
        self.assertEqual(self.app.state['active']['id'],'v1')
    def test_bad_expanded_checksum_does_not_activate(self):
        self.release();self.app.run('update');self.release('v2',2,manifest_changes=lambda d:d['files']['skyrecomp'].update(sha256='0'*64))
        with self.assertRaises(m.UpdateError):self.app.run('update')
        self.assertEqual(self.app.state['active']['id'],'v1');self.assertFalse(list((self.app.root/'versions').glob('.staging-*')))
    def test_unsafe_paths_rejected(self):
        for name in ['../escape','/escape','C:/escape','x\\escape','x:stream','save/data','exefs/main','ROMFS.BIN','skyrecomp.ini','a./b','CON.txt','foo/NUL','sst-scenes-v1.bin','installed-manifest.json']:
            with self.subTest(name=name),self.assertRaises(m.UpdateError):m.safe_name(name)
    def test_zip_traversal_is_not_extracted(self):
        self.release(unsafe_entries={'../escape':b'bad','skyrecomp':b'ok','compiler/header.h':b'SDK'})
        with self.assertRaises(m.UpdateError):self.app.run('update')
        self.assertFalse((self.base/'escape').exists());self.assertIsNone(self.app.state['active'])
    def test_zip_symlink_rejected(self):
        self.release();p=self.releases/'v1.zip'
        with zipfile.ZipFile(p,'a') as z:
            i=zipfile.ZipInfo('link');i.create_system=3;i.external_attr=(0o120777<<16);z.writestr(i,'../private')
        feed=json.loads((self.releases/'feed.json').read_text());e=feed['release']['platforms']['linux-x86_64'];e.update(size=p.stat().st_size,sha256=m.digest(p));m.atomic_json(self.releases/'feed.json',feed)
        with self.assertRaises(m.UpdateError):self.app.run('update')
    def test_manifest_case_collision_rejected(self):
        self.release(files={'skyrecomp':b'x','Same':b'a','same':b'b'})
        with self.assertRaises(m.UpdateError):self.app.run('update')
    def test_absolute_https_is_not_joined_to_local_configuration_path(self):
        url='https://github.com/EpicCargo/skyward-sword-together-updates/releases/latest/download/sst-updates.json'
        self.assertEqual(m.location(url,str(self.app.root/'launcher-config.json')),url)
        self.assertEqual(m.location('package.zip',url),'https://github.com/EpicCargo/skyward-sword-together-updates/releases/latest/download/package.zip')
        self.assertEqual(m.location('package.zip',str(self.app.root/'updates/feed.json')),str(self.app.root/'updates/package.zip'))
    def test_online_metadata_cannot_select_local_files(self):
        with self.assertRaises(m.UpdateError):m.location('file:///private/config','https://example.com/feed.json')
    def test_verified_launcher_script_update_keeps_configuration(self):
        root=self.app.root;old=root/'sst_launcher.py';old.write_bytes(b'old launcher');config=root/'launcher-config.json';config.write_bytes(b'private prefs')
        version=m.LAUNCHER_VERSION+1
        raw=f"APP_ID = 'skyward-sword-together'\nLAUNCHER_VERSION = {version}\n".encode()
        update=self.releases/'launcher.py';update.write_bytes(raw)
        feed={'launcher':{'version':version,'url':'launcher.py','size':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}}
        self.assertTrue(m.upgrade_launcher(root,feed,str(self.releases/'feed.json')));self.assertEqual(old.read_bytes(),raw);self.assertEqual(config.read_bytes(),b'private prefs');self.assertEqual(len(list((root/'launcher-backups').glob('*.py'))),1)
    def test_bad_launcher_update_never_replaces_code(self):
        target=self.app.root/'sst_launcher.py';target.write_bytes(b'previous code');raw=f"APP_ID = 'skyward-sword-together'\nLAUNCHER_VERSION = {m.LAUNCHER_VERSION}\n".encode();source=self.releases/'launcher.py';source.write_bytes(raw)
        feed={'launcher':{'version':m.LAUNCHER_VERSION+1,'url':'launcher.py','size':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}}
        with self.assertRaises(m.UpdateError):m.upgrade_launcher(self.app.root,feed,str(self.releases/'feed.json'))
        self.assertEqual(target.read_bytes(),b'previous code')
    def test_non_https_remote_blocked(self):
        for url in ['http://example.com/feed','ftp://example.com/feed']:
            with self.assertRaises(m.UpdateError):m.location(url)
        self.assertTrue(m.location('http://127.0.0.1:1234/feed').startswith('http:'))
    def test_older_online_release_blocked(self):
        self.release('v2',2);self.app.run('update');self.release('v1',1)
        with self.assertRaises(m.UpdateError):self.app.run('update')
        self.assertEqual(self.app.state['active']['id'],'v2')
    def test_offline_older_fallback_keeps_installed_version(self):
        self.release('v1',1);fallback=self.releases/'fallback.json';fallback.write_bytes((self.releases/'feed.json').read_bytes());self.release('v2',2);self.app.run('update');self.app.config.update(feed=str(self.base/'absent.json'),fallback_feed=str(fallback));self.app.run('update');self.assertEqual(self.app.state['active']['id'],'v2');self.assertIn('could not be checked',self.app.status['message'])
    def test_running_game_blocks_update_before_download(self):
        self.release();self.app.running_processes=lambda:True
        with self.assertRaises(m.UpdateError):self.app.run('update')
        self.assertIsNone(self.app.state['active']);self.assertFalse((self.app.root/'cache').exists())
    def test_rejected_config_does_not_mutate_preferences(self):
        before=dict(self.app.config)
        with self.assertRaises(m.UpdateError):self.app.save_config({'data_dir':str(self.app.root/'versions/v1')})
        self.assertEqual(self.app.config,before)
    def test_setup_speed_migrates_to_one_job_and_persists_choice(self):
        legacy=dict(self.app.config);legacy.pop('setup_jobs',None);m.atomic_json(self.app.root/'launcher-config.json',legacy)
        loaded=m.Launcher(self.app.root,target_platform='linux-x86_64');loaded.running_processes=lambda:False
        self.assertEqual(loaded.config['setup_jobs'],1)
        loaded.save_config({'setup_jobs':'4'})
        self.assertEqual(m.Launcher(self.app.root,target_platform='linux-x86_64').config['setup_jobs'],4)
    def test_online_room_lost_before_launch_does_not_start_offline_game(self):
        self.release();self.app.run('update')
        self.app.online=mock.Mock();self.app.online.environment.return_value={}
        with mock.patch.object(m.subprocess,'Popen') as launch:
            with self.assertRaisesRegex(m.UpdateError,'room closed'):
                self.app.play()
        launch.assert_not_called()
        self.assertEqual((self.data/'save.sav').read_bytes(),b'personal save')

    def test_invalid_setup_speed_preserves_preferences(self):
        self.app.save_config({'setup_jobs':'2'});before=dict(self.app.config);on_disk=(self.app.root/'launcher-config.json').read_bytes()
        for value in [0,3,5,64,-1,True,None,[],{},'4.0',' 4','1; echo bad']:
            with self.subTest(value=value),self.assertRaises(m.UpdateError):self.app.save_config({'setup_jobs':value})
            self.assertEqual(self.app.config,before);self.assertEqual((self.app.root/'launcher-config.json').read_bytes(),on_disk)
    def test_running_setup_blocks_speed_changes(self):
        self.app.running_processes=lambda:True
        with self.assertRaises(m.UpdateError):self.app.save_config({'setup_jobs':'4'})
        self.assertEqual(self.app.config['setup_jobs'],1)
    def test_actual_child_receives_selected_setup_jobs(self):
        # Use Python as a harmless stand-in for the game, on Windows and POSIX.
        # It receives the same command-line game directory and environment.
        game=self.base/'prepared game';(game/'exefs').mkdir(parents=True);(game/'exefs/main').write_bytes(b'fixture');(game/'romfs.bin').write_bytes(b'fixture')
        (game/'__main__.py').write_text("import json,os,pathlib\npathlib.Path(os.environ['LARECOMP_DATA_ROOT'],'setup-environment.json').write_text(json.dumps({'jobs':os.environ.get('SST_BUILD_JOBS'),'data':os.environ['LARECOMP_DATA_ROOT']}))\n")
        self.app.config['game_dir']=str(game)
        with mock.patch.object(self.app,'verify_entrypoint',return_value=Path(sys.executable)),mock.patch.dict(os.environ,{'SST_BUILD_JOBS':'64'}):
            for jobs in [1,2,4,8]:
                self.app.save_config({'setup_jobs':str(jobs)});self.app.play(setup=True)
                self.assertEqual(self.app.game.wait(timeout=10),0)
                child=json.loads((self.data/'setup-environment.json').read_text());self.assertEqual(child,{'jobs':str(jobs),'data':str(self.data)})
                self.assertEqual(os.environ['SST_BUILD_JOBS'],'64');self.assertEqual((self.data/'save.sav').read_bytes(),b'personal save')
    def test_setup_speed_preserves_prepared_game_and_resume_cache(self):
        game=self.base/'prepared game';work=game/'.gamebuild';work.mkdir(parents=True)
        files={work/'main_001.o':b'completed object',work/'main_001.stamp':b'valid build stamp',game/'skygame.dll':b'existing game library'}
        for p,data in files.items():p.write_bytes(data)
        self.app.save_config({'game_dir':str(game),'setup_jobs':'8'})
        self.assertEqual(m.Launcher(self.app.root,target_platform='linux-x86_64').config['setup_jobs'],8)
        for p,data in files.items():self.assertEqual(p.read_bytes(),data)
    def test_operation_file_lock(self):
        second=m.Launcher(self.app.root,target_platform='linux-x86_64')
        with self.app.install_lock():
            with self.assertRaises(m.UpdateError):
                with second.install_lock():pass
    @unittest.skipIf(os.name=='nt','POSIX fixture executable')
    def test_actual_game_process_uses_external_data_directory(self):
        body=b'#!/bin/sh\nprintf "%s" "$LARECOMP_DATA_ROOT" > "$LARECOMP_DATA_ROOT/launcher-check.txt"\n'
        self.release(files={'skyrecomp':body});self.app.run('update');self.app.run('play');self.app.game.wait(timeout=5);self.assertEqual((self.data/'launcher-check.txt').read_text(),str(self.data));self.assertEqual((self.data/'save.sav').read_bytes(),b'personal save')
    def test_http_api_requires_token_and_correct_origin(self):
        server=m.Server(('127.0.0.1',0),self.app);t=threading.Thread(target=server.serve_forever,daemon=True);t.start();base=f'http://127.0.0.1:{server.server_port}'
        try:
            with self.assertRaises(urllib.error.HTTPError):urllib.request.urlopen(base+'/api/status')
            req=urllib.request.Request(base+'/api/status',headers={'Authorization':'Bearer '+server.token})
            with urllib.request.urlopen(req) as f:self.assertEqual(json.load(f)['platform'],'linux-x86_64')
            req=urllib.request.Request(base+'/api/config',data=b'{}',headers={'Authorization':'Bearer '+server.token,'Origin':'https://untrusted.example'})
            with self.assertRaises(urllib.error.HTTPError):urllib.request.urlopen(req)
        finally:server.shutdown();server.server_close();t.join()

if __name__=='__main__':unittest.main()
