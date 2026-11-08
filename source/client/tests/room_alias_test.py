#!/usr/bin/env python3
"""Real TCP joins: the UI's blank/public defaults agree; custom rooms stay exact."""
import json,os,re,socket,struct,subprocess,sys,unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PROTO=int(re.search(r'#define SST_PROTO\s+(\d+)',(ROOT/'src/sst_net.h').read_text()).group(1))
FIXTURE=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'build/host_fixture'
COMMAND=json.loads(os.environ['SST_ROOM_HOST_COMMAND']) if os.environ.get('SST_ROOM_HOST_COMMAND') else [str(FIXTURE)]
sys.argv=sys.argv[:1]

def read_exact(s,n):
    result=b''
    while len(result)<n:
        part=s.recv(n-len(result))
        if not part:raise RuntimeError('Host closed before answering the join')
        result+=part
    return result

def string(value):
    b=value.encode('utf-8');return bytes([len(b)])+b

class RoomAliasTests(unittest.TestCase):
    def test_real_room_handshakes(self):
        cases=[('', '', True),('', 'public', True),('public', '', True),('public', 'public', True),
               ('friends', 'friends', True),('friends', '', False),('friends', 'public', False),
               ('public', 'Public', False),('public', 'public ', False),('friends', 'Friends', False),
               (' public', ' public', True),(' public', 'public', False),('(public)', '(public)', True),
               ('public', '(public)', False)]
        for host_room,guest_room,accepted in cases:
            with self.subTest(host=host_room,guest=guest_room):
                proc=subprocess.Popen(COMMAND+['0',host_room],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                try:
                    port_line=proc.stdout.readline().strip()
                    if not port_line:raise RuntimeError('Host failed to start: '+proc.stderr.read())
                    with socket.create_connection(('127.0.0.1',int(port_line)),timeout=5) as s:
                        hello=bytes([1])+struct.pack('<H',PROTO)+string('Room test')+string(guest_room)+string('')+os.urandom(16)
                        s.sendall(struct.pack('<I',len(hello))+hello)
                        n=struct.unpack('<I',read_exact(s,4))[0];self.assertLessEqual(n,65536)
                        reply=read_exact(s,n)
                        self.assertEqual(reply[0],2 if accepted else 5)
                        if not accepted:self.assertIn(b'Room does not match the host',reply)
                finally:
                    proc.stdin.close();proc.wait(timeout=5);proc.stdout.close();errors=proc.stderr.read();proc.stderr.close()
                    self.assertEqual(proc.returncode,0,errors)

if __name__=='__main__':unittest.main(verbosity=2)
