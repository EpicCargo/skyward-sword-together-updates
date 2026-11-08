"""Delayed host acknowledgements against the real threaded C network consumer."""
import struct
import time
from queue_e2e import Rig, frame, flag

rig=Rig()
try:
    rig.welcome()
    token=rig.command('request')['token'];assert token
    rig.wait(lambda:any(k==0x21 and b==struct.pack('<I',token) for k,b in rig.frames))
    # Ping/traffic and the old 1.5s delay are not an acknowledgement.
    time.sleep(1.65);assert rig.command(f'refresh {token}')['state']==1
    rig.send(frame(0x24,struct.pack('<I',token+1))+frame(0x24,b'\x01'))
    rig.marker('wrong-token');rig.command('wait wrong-token')
    assert rig.command(f'refresh {token}')['state']==1
    rig.send(flag(50)+frame(0x23,struct.pack('<HHH',0,2,10))+frame(0x24,struct.pack('<I',token)))
    rig.marker('acked');rig.command('wait acked')
    assert rig.command(f'refresh {token}')['state']==1
    assert rig.command('flags')['value50']==1
    assert rig.command(f'refresh {token}')['state']==1 # receipt consumer has not run yet
    assert rig.command('chests')['chests']==1
    assert rig.command(f'refresh {token}')['state']==2
    newer=rig.command('request')['token'];assert newer and newer!=token
    assert rig.command(f'refresh {token}')['state']==0
    rig.send(frame(0x24,struct.pack('<I',token)));rig.marker('old-ack');rig.command('wait old-ack')
    assert rig.command(f'refresh {newer}')['state']==1
    # An acknowledged but truncated cache must never authorize a reload.
    rig.send(b''.join(flag(i) for i in range(5000))+frame(0x24,struct.pack('<I',newer)))
    rig.marker('overflow-acked');rig.command('wait overflow-acked')
    assert rig.command(f'refresh {newer}')['state']==0
    rig.command('flags');rig.command('enemies')
    newest=rig.command('request')['token'];assert newest
    rig.send(frame(0x24,struct.pack('<I',newest)));rig.marker('fresh-ack');rig.command('wait fresh-ack')
    assert rig.command(f'refresh {newest}')['state']==2
    rig.sock.shutdown(2)
    end=time.monotonic()+3
    while time.monotonic()<end and rig.command(f'refresh {newest}')['state']!=0:time.sleep(.02)
    assert rig.command(f'refresh {newest}')['state']==0
    assert rig.command('request')['token']==0
    print('Snapshot recovery PASS: delayed/malformed/stale acknowledgements, both consumer queues, supersession, overflow and disconnect')
finally:rig.close()
