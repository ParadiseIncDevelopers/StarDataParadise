import importlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'classic-workbench/modules/wb.utils'))
local=importlib.import_module('sdp_local_worker')

class LocalWorkerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.config=self.root/'config.json';self.config.write_text('{"credentials":{},"local_runtime":{"threads":2}}')
        self.value=dict(version=1,owner='a@localhost',server_uuid='uuid',port=41754,config=str(self.config),
                        import_root=str(self.root),status=str(self.root/'status.json'))
        self.path=self.root/'manifest.json';self.path.write_text(json.dumps(self.value))
    def test_identity_is_pinned_to_instance_owner_and_port(self):
        self.assertEqual(local.installation(['uuid','41754','a@localhost'],self.path),self.value)
        for identity in (['other','41754','a@localhost'],['uuid','41754','b@localhost'],['uuid','3306','a@localhost']):
            with self.assertRaises(ValueError):local.installation(identity,self.path)
    def test_binding_preserves_configuration_and_other_owners(self):
        local.bind_credential(self.value,'OPENAI','openai_main')
        local.bind_credential(self.value,'ANTHROPIC','claude_main')
        value=json.loads(self.config.read_text())
        self.assertEqual(value['local_runtime'],dict(threads=2))
        aliases=value['credentials']['a@localhost']
        self.assertEqual(aliases['claude_main']['providers'],['anthropic'])
        self.assertEqual(aliases['openai_main']['target'],'StarDataParadise/ai/openai_main')
        with self.assertRaises(ValueError):local.bind_credential(self.value,'ANTHROPIC','openai_main')
        with self.assertRaises(ValueError):local.bind_credential(self.value,'OPENAI','x',endpoint='https://proxy.example')
        with self.assertRaises(ValueError):local.bind_credential(self.value,'OPENAI','sk-secret')
    def test_local_import_preserves_source_and_hashes_bytes(self):
        source=self.root/'original.gguf';content=struct.pack('<4sIQQ',b'GGUF',3,0,0)+b'payload';source.write_bytes(content)
        progress=[];result=local.stage_gguf(self.value,source,lambda a,b:progress.append((a,b)))
        self.assertEqual(source.read_bytes(),content)
        self.assertEqual((self.root/result['artifact']).read_bytes(),content)
        self.assertEqual(result['sha256'],local.hashlib.sha256(content).hexdigest())
        self.assertEqual(progress[-1],(len(content),len(content)))
    def test_bad_import_is_not_published(self):
        source=self.root/'bad.gguf';source.write_bytes(b'x'*24)
        with self.assertRaises(ValueError):local.stage_gguf(self.value,source)
        self.assertFalse(list(self.root.glob('*.part')))
        self.assertEqual(list(self.root.glob('*.gguf')),[source])
    def test_cancel_removes_partial_copy_and_keeps_source(self):
        source=self.root/'cancel.gguf';content=struct.pack('<4sIQQ',b'GGUF',3,0,0)+b'x'*1024;source.write_bytes(content)
        def cancel(*args):raise ValueError('cancel')
        with self.assertRaisesRegex(ValueError,'cancel'):local.stage_gguf(self.value,source,cancel)
        self.assertEqual(source.read_bytes(),content)
        self.assertFalse(list(self.root.glob('*.part')))
        self.assertEqual(list(self.root.glob('*.gguf')),[source])
    def test_manual_launcher_is_confined_to_installed_directory(self):
        launcher=self.root/'StarDataParadiseAiWorker.exe';launcher.write_text('')
        self.value['launcher_exe']=str(launcher)
        with patch('subprocess.Popen') as launch:
            local.start_worker(self.value)
            self.assertEqual(Path(launch.call_args.args[0][0]),launcher)
            self.assertEqual(launch.call_args.args[0][1:],["--deployment",str(launcher.parent.resolve())])
        other=self.root/'other.exe';other.write_text('');self.value['launcher_exe']=str(other)
        with self.assertRaises(ValueError):local.start_worker(self.value)
    def test_health_checks_freshness_and_state(self):
        status=Path(self.value['status']);status.write_text('{"state":"ready"}')
        self.assertEqual(local.health(self.value)['state'],'ready')
        local.os.utime(status,(1,1))
        with self.assertRaises(ValueError):local.health(self.value)

if __name__=='__main__':unittest.main()
