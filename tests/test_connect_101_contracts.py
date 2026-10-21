"""Connect 1.0.1 structural QA. Not a substitute for Qt compile or live acceptance."""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(path): return (ROOT/path).read_text(encoding='utf-8')
def js(path): return json.loads(read(path))

class Connect101ContractTests(unittest.TestCase):
    def test_manifest(self):
        m=js('manifest.json')
        self.assertEqual((m['schema'],m['name'],m['version']), (4,'neebles-connect','1.0.1'))
        self.assertEqual(m['entrypoint'],'runtime/neebles-connect')

    def test_install_vault_and_rootfs(self):
        contract=js('lifecycle.json')
        self.assertEqual(contract['transitions']['install']['operations']['ensure-secrets-vault']['munition']['step'],'install-vault')
        self.assertEqual(contract['transitions']['open']['operations']['launch-runtime']['munition']['step'],'open-runtime')

    def test_no_legacy_profiles(self):
        for f in ['ui/desktop/MainWindow.cpp','ui/desktop/MainWindow.h','runtime/src/main.rs']:
            self.assertNotIn('profiles.dev.json',read(f))

    def test_ipc_and_my_quick_independence(self):
        code=read('ui/desktop/MainWindow.cpp')
        for needle in ['secret.put','secret.get','secret.update','secret.delete','vault.list',
                       'closeSavedSessions()', 'QStringLiteral("quick:")', 'refreshSecretsAccess()',
                       'profiles_.clear()', 'setCredentialProvider']:
            self.assertIn(needle,code)
        self.assertIn('!tabs_->tabBar()->tabData(i).toString().startsWith(QStringLiteral("quick:"))',code)

    def test_controlled_qt_world(self):
        cmake=read('ui/desktop/CMakeLists.txt')
        for needle in ['Qt6 6.8.2 EXACT','NEEBLES_CONNECT_CONTROLLED_SYSROOT','NEEBLES_CONNECT_DEP_OVERLAY','libvncclient.so.1','libssh2.so.1','libvterm.so.0']:
            self.assertIn(needle,cmake)

    def test_single_valid_qa(self):
        self.assertFalse((ROOT/'prototype').exists())
        self.assertFalse((ROOT/'tooling/esbirro/f3-source-lock.json').exists())

if __name__=='__main__': unittest.main()
