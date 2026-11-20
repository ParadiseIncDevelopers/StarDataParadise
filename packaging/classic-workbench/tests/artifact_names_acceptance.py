"""Exercise bundled client lookup and connection parsing after artifact renames."""
import ast
import json
import os
from pathlib import Path
import re
import shlex
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]


def function(relative, name, namespace):
    path = ROOT / 'classic-workbench' / relative
    tree = ast.parse(path.read_text(encoding='utf-8-sig'))
    node = next(item for item in tree.body if isinstance(item, ast.FunctionDef) and item.name == name)
    node.decorator_list = []
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(path), 'exec'), namespace)
    return namespace[name]


class ArtifactNamesAcceptance(unittest.TestCase):
    def test_artifact_names_are_unique(self):
        names = json.loads((ROOT / 'packaging/classic-workbench/artifact-names.json').read_text())
        values = list(names['libraries'].values()) + list(names['files'].values())
        self.assertEqual(len(values), len(set(value.casefold() for value in values)))
        self.assertTrue(all('mysql' not in value.casefold() for value in values))

    def test_windows_bundled_client_uses_sdp_name(self):
        requested = []
        app = SimpleNamespace(get_executable_path=lambda name: requested.append(name) or 'fixture/' + name)
        settings = {'mysqlclient': ''}
        namespace = {'grt': SimpleNamespace(root=SimpleNamespace(wb=SimpleNamespace(options=SimpleNamespace(options=settings)))),
                     'mforms': SimpleNamespace(App=SimpleNamespace(get=lambda: app)), 'sys': SimpleNamespace(platform='win32'),
                     'os': os}
        lookup = function('library/python/workbench/client_utils.py', 'get_path_to_mysql', namespace)
        self.assertEqual(lookup(), 'fixture/sdp.exe')
        self.assertEqual(requested, ['sdp.exe'])
        settings['mysqlclient'] = 'custom-client.exe'
        with patch('os.path.exists', return_value=True):
            self.assertEqual(lookup(), 'custom-client.exe')
        self.assertEqual(requested, ['sdp.exe'])

    def test_connection_parser_accepts_sdp_and_existing_client_commands(self):
        driver = SimpleNamespace(name='MysqlNative', hostIdentifierTemplate='Mysql@%hostName%:%port%')
        namespace = {'re': re, 'shlex': shlex, 'log_warning': lambda *args: None,
                     'grt': SimpleNamespace(
                         classes=SimpleNamespace(db_mgmt_Connection=lambda: SimpleNamespace(parameterValues={})),
                         root=SimpleNamespace(wb=SimpleNamespace(rdbmsMgmt=SimpleNamespace(rdbms=[SimpleNamespace(drivers=[driver])]))))}
        parse = function('modules/wb.utils/wb_utils_grt.py', 'connectionFromString', namespace)
        for command in ('sdp', 'sdp.exe', 'mysql', 'mysql.exe', 'C:/fixture/sdp.exe', 'C:/fixture/SDP.EXE'):
            with self.subTest(command=command):
                result = parse(command + ' -u qa -h 127.0.0.1 -P 3197')
                self.assertIsNotNone(result)
                self.assertEqual(result.parameterValues['port'], 3197)
                self.assertEqual(result.parameterValues['userName'], 'qa')


if __name__ == '__main__':
    unittest.main()
