"""Image-candidate gates reject stale/wrong/native-dialog observations."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('native_capture_gate',Path(__file__).resolve().parents[1]/'validation/native_capture_gate.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
readiness,SCENE=gate.readiness,gate.SCENE


def observation():
    target=dict(placed_id=117,profile=496,params=4294967280,room=0,actor_token=102693920,
                instance=2078,state=1,held_by_native=True,pos=[3380,128,-3000])
    return dict(frame=1200,finished=False,state=dict(preceding_renderer_frame=1140,value=dict(
        readable=True,actor_identity_matches=True,event_state=0,loader_state=703,pause_flags=[0,0,0],held=True)),
        props=dict(preceding_renderer_frame=1140,value=dict(complete=True,count=1,player_instance=1955,items=[target])),
        peers=dict(scene=SCENE,source=1,source_context=4,observed_place_sent=0,descriptor=dict(native_source=1,
            captured=20,retained_settled_carries=1,cold_import=dict(cli_opt_in=False,active=False,count=0))))


IDENTITY=dict(actor_token=102693920,instance=2078,player_instance=1955)


class CaptureGateTests(unittest.TestCase):
    def test_correlated_native_pickup_is_a_candidate_only(self):
        self.assertTrue(readiness(observation(),IDENTITY,'pickup'))

    def test_dialog_and_nonsteady_carry_are_rejected(self):
        for field,value in [('event_state',2),('held',False),('pause_flags',[1,0,0]),('readable',False)]:
            row=observation();row['state']['value'][field]=value
            self.assertFalse(readiness(row,IDENTITY,'pickup'))
        row=observation();row['peers']['source_context']=28
        self.assertFalse(readiness(row,IDENTITY,'pickup'))

    def test_held_linkage_must_belong_to_exact_target(self):
        for field,value in [('held_by_native',False),('instance',2079),('actor_token',1),('state',0),('placed_id',118)]:
            row=observation();row['props']['value']['items'][0][field]=value
            self.assertFalse(readiness(row,IDENTITY,'pickup'))

    def test_incomplete_duplicate_or_changed_player_census_is_rejected(self):
        for field,value in [('complete',False),('count',0),('player_instance',1956)]:
            row=observation();row['props']['value'][field]=value
            self.assertFalse(readiness(row,IDENTITY,'pickup'))
        row=observation();row['props']['value']['items']*=2;row['props']['value']['count']=2
        self.assertFalse(readiness(row,IDENTITY,'pickup'))

    def test_stale_future_unfinished_and_malformed_observations_are_rejected(self):
        for frame in (500,1700,True):
            row=observation();row['frame']=frame
            self.assertFalse(readiness(row,IDENTITY,'pickup'))
        row=observation();row['finished']=True
        self.assertFalse(readiness(row,IDENTITY,'pickup'))
        for row in ({},dict(state=None),None):
            self.assertFalse(readiness(row,IDENTITY,'pickup'))

    def test_scene_source_capture_and_injection_gates(self):
        for field,value in [('scene','054630303172010105'),('source',0),('observed_place_sent',1)]:
            row=observation();row['peers'][field]=value
            self.assertFalse(readiness(row,IDENTITY,'pickup'))
        for field,value in [('native_source',3),('captured',0),('retained_settled_carries',0)]:
            row=observation();row['peers']['descriptor'][field]=value
            self.assertFalse(readiness(row,IDENTITY,'pickup'))
        for field,value in [('active',True),('cli_opt_in',True),('count',1)]:
            row=observation();row['peers']['descriptor']['cold_import'][field]=value
            self.assertFalse(readiness(row,IDENTITY,'pickup'))

    def test_putdown_requires_previously_observed_exact_carry(self):
        row=observation();row['state']['value']['held']=False
        row['props']['value']['items'][0].update(held_by_native=False,state=0)
        row['peers']['source_context']=0
        self.assertFalse(readiness(row,IDENTITY,'putdown'))
        self.assertTrue(readiness(row,IDENTITY,'putdown',carry_observed=True))
        self.assertFalse(readiness(row,IDENTITY,'pickup'))


if __name__=='__main__':
    unittest.main()
