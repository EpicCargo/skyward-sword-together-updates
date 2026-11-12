import unittest
import importlib.util
from pathlib import Path
_spec=importlib.util.spec_from_file_location('sst_expected_disconnect',Path(__file__).resolve().parents[1]/'validation/expected_disconnect.py')
_module=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_module)
ExpectedDisconnect=_module.ExpectedDisconnect

def intent(request=7,seconds=10):
    return dict(kind='native-own-disconnect',request=request,seconds=seconds)

class ExpectedDisconnectTests(unittest.TestCase):
    def test_explicit_eof_waits_only_until_fresh_capture(self):
        policy=ExpectedDisconnect();policy.observe(intent(),2);policy.eof(3)
        self.assertTrue(policy.waiting(4,final_capture=False))
        self.assertFalse(policy.waiting(5,final_capture=True))
    def test_repeated_intent_does_not_extend_deadline(self):
        policy=ExpectedDisconnect();policy.observe(intent(),2);policy.observe(intent(),11)
        self.assertEqual(policy.deadline,12)
        for value in (intent(8),intent(seconds=11)):
            with self.assertRaises(ValueError):policy.observe(value,11)
    def test_malformed_or_ambiguous_intent_is_rejected(self):
        for value in ({},True,dict(kind='disconnect',request=7,seconds=10),intent(True),intent(0),intent(seconds=True),intent(seconds=0),intent(seconds=91),dict(intent(),extra=True)):
            with self.subTest(value=value),self.assertRaises(ValueError):ExpectedDisconnect().observe(value,2)
    def test_unarmed_or_expired_eof_is_rejected(self):
        policy=ExpectedDisconnect();policy.observe(None,2)
        with self.assertRaises(RuntimeError):policy.eof(3)
        policy.observe(intent(),4)
        with self.assertRaises(RuntimeError):policy.eof(14)
    def test_capture_cannot_replace_an_observed_eof(self):
        policy=ExpectedDisconnect();policy.observe(intent(),2)
        with self.assertRaises(RuntimeError):policy.waiting(3,final_capture=True)
    def test_duplicate_eof_is_rejected(self):
        policy=ExpectedDisconnect();policy.observe(intent(),2);policy.eof(3)
        with self.assertRaises(RuntimeError):policy.eof(4)
    def test_invalid_times_and_capture_flags_are_rejected(self):
        for value in (True,-1,float('inf'),float('nan'),'2'):
            with self.subTest(value=value),self.assertRaises(ValueError):ExpectedDisconnect().observe(intent(),value)
        policy=ExpectedDisconnect();policy.observe(intent(),2);policy.eof(3)
        with self.assertRaises(RuntimeError):policy.waiting(4,final_capture=1)
    def test_clock_rollback_and_late_capture_cannot_pass(self):
        policy=ExpectedDisconnect();policy.observe(intent(),2)
        with self.assertRaises(RuntimeError):policy.eof(1)
        policy.eof(3)
        for now in (1,12,13):
            with self.subTest(now=now),self.assertRaises(RuntimeError):policy.waiting(now,final_capture=True)
    def test_status_never_grants_gameplay_acceptance(self):
        policy=ExpectedDisconnect();policy.observe(intent(),2);policy.eof(3)
        self.assertEqual(policy.status(),dict(request=7,armed_at=2,deadline=12,host_eof_at=3,gameplay_accepted=False))

if __name__=='__main__':unittest.main(verbosity=2)
