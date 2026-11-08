"""The embedded host acknowledges only the tail of its incremental cache replay."""
import json,os,struct,time
from udp_e2e import Process, ROOT
from udp_wire_test import Peer

host=Process(json.loads(os.environ.get('SST_TEST_HOST_COMMAND','null')) or [str(ROOT/'client/build/host_fixture')])
peers=[]
try:
    port=int(host.line());source=Peer(port);peers.append(source)
    # More than one 64KiB host catch-up chunk, with no recipient while seeding.
    for i in range(8000):source.send(0x20,struct.pack('<BHB',1,20000+i,1))
    source.send(0x23,struct.pack('<HH',2,10));source.barrier()
    target=Peer(port);peers.append(target);target.barrier()
    token=0x12345678;target.send(0x21,struct.pack('<I',token));got=set();chest=False
    while True:
        kind,body=target.receive()
        if kind==0x20:
            who,k,index,value=struct.unpack('<HBHB',body)
            if k==1 and value==1:got.add(index)
        if kind==0x23:chest|=body==struct.pack('<HHH',0,2,10)
        if kind==0x24:
            assert body==struct.pack('<I',token) and got==set(range(20000,28000)) and chest
            break
    # A peer cannot forward a forged marker to another peer.
    source.send(0x24,struct.pack('<I',999));source.barrier();target.send(6,b'end!')
    while True:
        kind,body=target.receive();assert kind!=0x24
        if kind==7 and body==b'end!':break
    for invalid in [b'\x01',bytes(4),bytes(5)]:
        bad=Peer(port);peers.append(bad);bad.send(0x21,invalid)
        while True:
            kind,body=bad.receive()
            if kind==5:break
    print('Snapshot host PASS: 8000 cached flags plus chest precede marker, spoof blocked, malformed/zero tokens rejected')
finally:
    for peer in peers:
        peer.tcp.close();peer.udp.close()
    host.close()
