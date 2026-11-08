import importlib.util,json,os,sys,tempfile,unittest
from pathlib import Path
from unittest import mock
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'launcher'))
import sst_online as online
import sst_launcher as launcher

class Tunnel:
 def __init__(self,*args,**kwargs):self.closed=False
 def start(self):return self
 def address(self):return ('127.0.0.1',34567)
 def status(self):return {'state':'connected','message':'Connected','connections':1}
 def close(self):self.closed=True

class Sessions(unittest.TestCase):
 def test_guest_environment_and_secret_free_status(self):
  with mock.patch.object(online,'GuestTunnel',Tunnel):
   session=online.OnlineSession('https://example.invalid')
   session.join('abc234');env=session.environment();self.assertEqual(env['SST_SESSION_ROOM'],'ABC234');self.assertEqual(env['SST_SESSION_SERVER'],'127.0.0.1:34567');self.assertEqual(env['SST_AUTOHOST'],'0');self.assertEqual(env['SST_AUTOCONNECT'],'1');self.assertNotIn('token',json.dumps(session.snapshot()));t=session.tunnel;session.close();self.assertTrue(t.closed);self.assertEqual(session.environment(),{})
 def test_invalid_code_does_not_start_transport(self):
  with mock.patch.object(online,'GuestTunnel') as tunnel:
   with self.assertRaises(online.TransportError):online.OnlineSession('https://example.invalid').join('bad')
   tunnel.assert_not_called()
 def test_failed_transport_cannot_supply_launch_environment_before_monitor_cleanup(self):
  session=online.OnlineSession('https://example.invalid')
  session.tunnel=mock.Mock();session.tunnel.address.return_value=('127.0.0.1',34567)
  session.tunnel.status.return_value={'state':'error'};session.code='ABC234';session.role='guest'
  self.assertEqual(session.environment(),{})
  session.tunnel.address.assert_not_called();session.close()
 def test_private_host_missing_before_room_creation(self):
  with mock.patch.object(online,'create_room') as create:
   with self.assertRaises(online.TransportError):online.OnlineSession('https://example.invalid',helper='/nonexistent/sst-session-host').host()
   create.assert_not_called()
 def test_launcher_room_guard_keeps_running_game(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64');app.config['room_service']='https://example.invalid'
   with mock.patch.object(app,'running_processes',return_value=[123]),mock.patch.object(app,'verify_entrypoint') as verify:
    with self.assertRaises(launcher.UpdateError):app.online_action('join','ABC234')
    verify.assert_not_called();self.assertIsNone(app.online)
 def test_quit_room_does_not_terminate_game(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64');session=mock.Mock();app.online=session;game=mock.Mock();app.game=game;app.online_action('leave');session.close.assert_called_once();game.terminate.assert_not_called();game.kill.assert_not_called()
 def test_runtime_companions_verified_before_import(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);app=launcher.Launcher(root,'linux-x86_64');app.state={'active':{'id':'test','entrypoint':'skyrecomp','protocol':45,'sequence':1},'previous':None};folder=root/'versions/test/online';folder.mkdir(parents=True)
   for source,name in [('sst_online_init.py','__init__.py'),('sst_online.py','sst_online.py'),('sst_online_transport.py','sst_online_transport.py')]: (folder/name).write_bytes((ROOT/'launcher'/source).read_bytes())
   files={'online/'+p.name:{'sha256':launcher.digest(p),'size':p.stat().st_size} for p in folder.iterdir()};launcher.atomic_json(folder.parent/'installed-manifest.json',{'files':files});module=app.online_runtime();self.assertTrue(hasattr(module,'OnlineSession'));(folder/'sst_online.py').write_text('raise Exception("not imported")')
   with self.assertRaises(launcher.UpdateError):app.online_runtime()
 def test_insecure_remote_service_rejected_without_saving(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64')
   with mock.patch.object(app,'running_processes',return_value=[]):
    with self.assertRaises(launcher.UpdateError):app.save_config({'room_service':'http://example.invalid'})
   self.assertEqual(app.config['room_service'],launcher.DEFAULT_ROOM_SERVICE)

class LifecycleRegression(unittest.TestCase):
 def test_quit_request_cancels_pending_work_before_acknowledging(self):
  import io
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64')
   handler=object.__new__(launcher.Handler);handler.server=mock.Mock(app=app)
   handler.path='/api/quit';handler.headers={'Content-Length':'2'};handler.rfile=io.BytesIO(b'{}')
   handler.authorized=mock.Mock(return_value=True)
   def acknowledged(status,data):
    self.assertTrue(app._closing);self.assertEqual(status,200)
   handler.send=mock.Mock(side_effect=acknowledged)
   with mock.patch.object(launcher.threading,'Thread') as thread:handler.do_POST()
   thread.assert_called_once_with(target=handler.server.shutdown,daemon=True)
   thread.return_value.start.assert_called_once()
   with self.assertRaisesRegex(launcher.UpdateError,'closing'):app.online_action('join','ABC234')

 def test_quit_still_stops_server_when_response_write_fails(self):
  import io
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64')
   handler=object.__new__(launcher.Handler);handler.server=mock.Mock(app=app)
   handler.path='/api/quit';handler.headers={'Content-Length':'2'};handler.rfile=io.BytesIO(b'{}')
   handler.authorized=mock.Mock(return_value=True)
   handler.send=mock.Mock(side_effect=BrokenPipeError('fixture browser disconnected'))
   with mock.patch.object(launcher.threading,'Thread') as thread:
    with self.assertRaises(BrokenPipeError):handler.do_POST()
   thread.assert_called_once_with(target=handler.server.shutdown,daemon=True)
   thread.return_value.start.assert_called_once();self.assertTrue(app._closing)

 def test_cancel_during_join_cannot_publish_a_room_or_launch_a_game(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64')
   session=mock.Mock()
   # Deterministic ordering: cleanup finishes while join is still in progress.
   session.join.side_effect=lambda code:app.close_online()
   with mock.patch.object(app,'running_processes',return_value=False), \
        mock.patch.object(app,'verify_entrypoint'), \
        mock.patch.object(app,'active_dir',return_value=Path(temp)), \
        mock.patch.object(app,'online_runtime',return_value=mock.Mock(OnlineSession=mock.Mock(return_value=session))), \
        mock.patch.object(app,'play') as play:
    with self.assertRaises(launcher.UpdateError):app.online_action('join','ABC234')
    play.assert_not_called();session.close.assert_called_once()
   self.assertIsNone(app.online);self.assertFalse(app.status['busy'])
   self.assertTrue(app.operation.acquire(False));app.operation.release()

 def test_shutdown_during_join_prevents_launch_and_future_connections(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64');session=mock.Mock()
   session.join.side_effect=lambda code:app.close_online(shutdown=True)
   with mock.patch.object(app,'running_processes',return_value=False), \
        mock.patch.object(app,'verify_entrypoint'), \
        mock.patch.object(app,'active_dir',return_value=Path(temp)), \
        mock.patch.object(app,'online_runtime',return_value=mock.Mock(OnlineSession=mock.Mock(return_value=session))) as runtime, \
        mock.patch.object(app,'play') as play:
    with self.assertRaisesRegex(launcher.UpdateError,'cancelled'):app.online_action('join','ABC234')
    session.close.assert_called_once();play.assert_not_called()
    with self.assertRaisesRegex(launcher.UpdateError,'closing'):app.online_action('join','ABC234')
    runtime.assert_called_once();self.assertIsNone(app.online)

 def test_regular_leave_allows_a_new_online_connection(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64');old=mock.Mock();app.online=old
   app.online_action('leave');old.close.assert_called_once()
   session=mock.Mock()
   with mock.patch.object(app,'running_processes',return_value=False), \
        mock.patch.object(app,'verify_entrypoint'), \
        mock.patch.object(app,'active_dir',return_value=Path(temp)), \
        mock.patch.object(app,'online_runtime',return_value=mock.Mock(OnlineSession=mock.Mock(return_value=session))), \
        mock.patch.object(app,'play') as play:
    app.online_action('join','ABC234');play.assert_called_once()
   self.assertIs(app.online,session);app.close_online();session.close.assert_called_once()

 def test_shutdown_does_not_wait_for_a_pending_join_to_finish(self):
  import threading
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64');session=mock.Mock()
   entered=threading.Event();finish=threading.Event();errors=[]
   def join(code):
    entered.set()
    if not finish.wait(2):raise AssertionError('Test did not release pending join')
   session.join.side_effect=join
   def work():
    try:app.online_action('join','ABC234')
    except Exception as error:errors.append(error)
   with mock.patch.object(app,'running_processes',return_value=False), \
        mock.patch.object(app,'verify_entrypoint'), \
        mock.patch.object(app,'active_dir',return_value=Path(temp)), \
        mock.patch.object(app,'online_runtime',return_value=mock.Mock(OnlineSession=mock.Mock(return_value=session))), \
        mock.patch.object(app,'play') as play:
    worker=threading.Thread(target=work);worker.start()
    try:
     self.assertTrue(entered.wait(1));app.close_online(shutdown=True)
     self.assertTrue(worker.is_alive());self.assertIsNone(app.online)
    finally:finish.set();worker.join(2)
    self.assertFalse(worker.is_alive());play.assert_not_called()
   self.assertEqual(len(errors),1);self.assertIsInstance(errors[0],launcher.UpdateError)
   session.close.assert_called_once();self.assertFalse(app.status['busy'])

 def test_shutdown_rejects_a_pending_offline_game_launch(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64');app.close_online(shutdown=True)
   with mock.patch.object(app,'running_processes',return_value=False), \
        mock.patch.object(app,'verify_entrypoint',return_value=Path(temp)/'skyrecomp'), \
        mock.patch.object(launcher.subprocess,'Popen') as launch:
    with self.assertRaisesRegex(launcher.UpdateError,'closing'):app.play()
    launch.assert_not_called()

 def test_shutdown_during_launch_preparation_cannot_start_a_game(self):
  with tempfile.TemporaryDirectory() as temp:
   app=launcher.Launcher(Path(temp),'linux-x86_64')
   def verify():
    app.close_online(shutdown=True)
    return Path(temp)/'skyrecomp'
   with mock.patch.object(app,'running_processes',return_value=False), \
        mock.patch.object(app,'verify_entrypoint',side_effect=verify), \
        mock.patch.object(launcher.subprocess,'Popen') as launch:
    with self.assertRaisesRegex(launcher.UpdateError,'closing'):app.play()
    launch.assert_not_called()

 def test_main_still_closes_its_listener_when_online_cleanup_fails(self):
  with tempfile.TemporaryDirectory() as temp:
   app=mock.Mock(root=Path(temp));app.close_online.side_effect=RuntimeError('owned cleanup failed')
   server=mock.Mock(server_port=12345,token='fixture');server.serve_forever.side_effect=RuntimeError('server stopped')
   with mock.patch.object(sys,'argv',['sst_launcher.py','--no-browser']), \
        mock.patch.object(launcher,'Launcher',return_value=app), \
        mock.patch.object(launcher,'Server',return_value=server):
    with self.assertRaisesRegex(RuntimeError,'owned cleanup failed'):launcher.main()
   server.server_close.assert_called_once()

 def test_existing_lease_is_not_deleted_when_another_session_cannot_claim_it(self):
  with tempfile.TemporaryDirectory() as temp, mock.patch.object(online,'GuestTunnel',Tunnel):
   lease=Path(temp)/'other-session.lease';lease.write_bytes(b'existing session')
   session=online.OnlineSession('https://example.invalid',lease=lease)
   with self.assertRaises(FileExistsError):session.join('ABC234')
   self.assertTrue(lease.exists());self.assertEqual(lease.read_bytes(),b'existing session')
   self.assertIsNone(session.tunnel);self.assertEqual(session.environment(),{})

 def test_replaced_lease_is_not_removed_by_its_old_owner(self):
  with tempfile.TemporaryDirectory() as temp, mock.patch.object(online,'GuestTunnel',Tunnel):
   lease=Path(temp)/'session.lease'
   session=online.OnlineSession('https://example.invalid',lease=lease)
   # Keep the heartbeat out of this deterministic replacement/close ordering.
   with mock.patch.object(session,'_start_monitor'):session.join('ABC234')
   replacement=lease.with_suffix('.replacement');replacement.write_bytes(b'new owner')
   replacement.replace(lease)
   session.close()
   self.assertEqual(lease.read_bytes(),b'new owner')

 def test_missing_owned_lease_is_not_recreated_or_touched(self):
  with tempfile.TemporaryDirectory() as temp, mock.patch.object(online,'GuestTunnel',Tunnel):
   lease=Path(temp)/'session.lease'
   session=online.OnlineSession('https://example.invalid',lease=lease)
   with mock.patch.object(session,'_start_monitor'):session.join('ABC234')
   lease.unlink()
   session._start_monitor()
   session._monitor.join(2)
   self.assertFalse(session._monitor.is_alive());self.assertFalse(lease.exists())
   self.assertEqual(session.environment(),{});self.assertEqual(session.snapshot()['state'],'error')

 def test_monitor_does_not_refresh_a_replacement_leases_heartbeat(self):
  with tempfile.TemporaryDirectory() as temp, mock.patch.object(online,'GuestTunnel',Tunnel):
   lease=Path(temp)/'session.lease'
   session=online.OnlineSession('https://example.invalid',lease=lease)
   with mock.patch.object(session,'_start_monitor'):session.join('ABC234')
   replacement=lease.with_suffix('.replacement');replacement.write_bytes(b'new owner')
   os.utime(replacement,ns=(2_000_000_000,2_000_000_000));replacement.replace(lease)
   session._start_monitor();session._monitor.join(2)
   self.assertFalse(session._monitor.is_alive());self.assertEqual(session.environment(),{})
   self.assertEqual(lease.read_bytes(),b'new owner');self.assertEqual(lease.stat().st_mtime_ns,2_000_000_000)

 def test_host_relay_loss_closes_owned_backend(self):
  import time
  session=online.OnlineSession('https://example.invalid');tunnel=mock.Mock();tunnel.status.return_value={'state':'running'};process=mock.Mock();process.poll.return_value=None;process.wait.return_value=0
  session.tunnel=tunnel;session.process=process;session.role='host';session.code='ABC234';session._start_monitor();tunnel.status.return_value={'state':'error'}
  end=time.monotonic()+2
  while time.monotonic()<end and session.process is not None:time.sleep(.01)
  self.assertIsNone(session.process);process.stdin.close.assert_called_once();process.wait.assert_called_once();tunnel.close.assert_called_once();session._monitor.join(1);self.assertFalse(session._monitor.is_alive());self.assertEqual(session.snapshot()['state'],'error')
 def test_tunnel_cleanup_error_still_stops_owned_helper(self):
  session=online.OnlineSession('https://example.invalid');tunnel=mock.Mock();tunnel.close.side_effect=RuntimeError('close failure');process=mock.Mock();process.wait.return_value=0;session.tunnel=tunnel;session.process=process
  with self.assertRaises(RuntimeError):session.close()
  process.stdin.close.assert_called_once();process.wait.assert_called_once();process.stdout.close.assert_called_once();self.assertIsNone(session.process)

 def test_lease_removed_on_leave_without_stopping_game(self):
  with tempfile.TemporaryDirectory() as temp, mock.patch.object(online,'GuestTunnel',Tunnel):
   lease=Path(temp)/'online-sessions/test.lease'
   session=online.OnlineSession('https://example.invalid',lease=lease)
   session.join('ABC234')
   self.assertTrue(lease.is_file());self.assertEqual(lease.stat().st_size,0)
   self.assertEqual(session.environment()['SST_SESSION_LEASE_FILE'],str(lease))
   session.close();self.assertFalse(lease.exists());session._monitor.join(1)
   self.assertFalse(session._monitor.is_alive());self.assertEqual(session.environment(),{})

 def test_lease_delete_failure_does_not_skip_helper_cleanup(self):
  with tempfile.TemporaryDirectory() as temp:
   session=online.OnlineSession('https://example.invalid',lease=Path(temp)/'session.lease')
   session._open_lease();process=mock.Mock();session.process=process
   with mock.patch.object(Path,'unlink',side_effect=PermissionError) as unlink:session.close()
   unlink.assert_called_once();process.stdin.close.assert_called_once();process.wait.assert_called_once()

 def test_helper_exit_revokes_lease_and_session(self):
  import time
  with tempfile.TemporaryDirectory() as temp:
   session=online.OnlineSession('https://example.invalid',lease=Path(temp)/'session.lease')
   process=mock.Mock();process.poll.return_value=1
   session.process=process;session.tunnel=Tunnel();session.code='ABC234';session.role='host'
   session._open_lease();session._start_monitor()
   deadline=time.monotonic()+2
   while time.monotonic()<deadline and session.lease.exists():time.sleep(.01)
   session._monitor.join(1)
   self.assertFalse(session.lease.exists());self.assertEqual(session.environment(),{})
   self.assertEqual(session.snapshot()['state'],'error');process.wait.assert_called_once()

 def test_partial_tunnel_start_is_owned_and_cleaned(self):
  tunnel=mock.Mock();tunnel.start.side_effect=online.TransportError('startup failed')
  with mock.patch.object(online,'GuestTunnel',return_value=tunnel):
   session=online.OnlineSession('https://example.invalid')
   with self.assertRaises(online.TransportError):session.join('ABC234')
   tunnel.close.assert_called_once();self.assertEqual(session.environment(),{})

if __name__=='__main__':unittest.main()
