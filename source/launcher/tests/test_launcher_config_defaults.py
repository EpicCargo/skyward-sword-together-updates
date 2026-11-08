"""Partial saved preferences preserve defaults during portable bundle upgrades."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import sst_launcher as launcher

class ConfigurationDefaultsTests(unittest.TestCase):
    def test_partial_saved_config_keeps_platform_defaults_and_preferences(self):
        for platform in ['windows-x86_64','linux-x86_64']:
            with self.subTest(platform=platform),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);path=root/'launcher-config.json'
                raw=json.dumps({'feed':'updates/local.json','setup_jobs':2,'custom_preference':'keep'}).encode()
                path.write_bytes(raw);app=launcher.Launcher(root,target_platform=platform)
                expected=root/'user-data/skyrecomp' if platform.startswith('windows') else Path.home()/'.local/share/skyward-sword-together'
                self.assertEqual(app.config['data_dir'],str(expected))
                self.assertEqual(app.config['game_dir'],'')
                self.assertEqual(app.config['fallback_feed'],'updates/sst-updates.json')
                self.assertEqual(app.config['room_service'],launcher.DEFAULT_ROOM_SERVICE)
                self.assertEqual(app.config['setup_jobs'],2)
                self.assertEqual(app.config['feed'],'updates/local.json')
                self.assertEqual(app.config['custom_preference'],'keep')
                self.assertEqual(path.read_bytes(),raw)

    def test_non_object_saved_config_has_a_clear_error_without_rewriting_it(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);path=root/'launcher-config.json';path.write_text('[]\n')
            with self.assertRaisesRegex(launcher.UpdateError,'configuration must be an object'):
                launcher.Launcher(root)
            self.assertEqual(path.read_text(),'[]\n')

if __name__=='__main__':unittest.main()
