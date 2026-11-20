"""Run in an isolated native Classic process with --run-script and --quit-when-done.

Verify the real GRT module paths, parse SQL in memory and open an existing model
and its native table editor. No connection, SQL execution or model save occurs.
SDP_ARTIFACT_PROBE_OUTPUT and SDP_ARTIFACT_PROBE_MODEL select the QA artifacts.
"""
import json
import os
from pathlib import Path
import traceback

import grt
import mforms

ROOT = Path(__file__).resolve().parents[3]
OUTPUT = Path(os.environ.get('SDP_ARTIFACT_PROBE_OUTPUT') or ROOT / 'out/classic-sdp-names-acceptance/native-result.json')


def run():
    expected = {'DbMySQL': 'db.sdp.grt.dll', 'DbMySQLQuery': 'db.sdp.query.grt.dll',
                'MySQLParserServices': 'db.sdp.parser.grt.dll', 'MysqlSqlFacade': 'db.sdp.sqlparser.grt.dll',
                'DbMySQLFE': 'db_sdp_fe_grt.py', 'DbMySQLRE': 'db_sdp_re_grt.py',
                'DbMySQLMigration': 'db_sdp_migration_grt.py'}
    paths = {}
    for name, filename in expected.items():
        module = getattr(grt.modules, name)
        paths[name] = Path(module.__path__).name
        assert paths[name].lower() == filename.lower(), (name, paths[name])
    for filename in ('sdp.exe', 'sdpdump.exe'):
        path = Path(mforms.App.get().get_executable_path(filename))
        assert path.is_file() and path.name.lower() == filename
    profile_path = Path(mforms.App.get().get_resource_path('sdp.profiles'))
    assert profile_path.is_dir() and list(profile_path.glob('*.xml'))
    model_path = Path(os.environ['SDP_ARTIFACT_PROBE_MODEL'])
    assert model_path.is_file() and not grt.root.wb.doc
    grt.modules.Workbench.openModel(str(model_path))
    document = grt.root.wb.doc
    assert document and document.physicalModels
    physical_model = document.physicalModels[0]
    original_catalog = physical_model.catalog
    tables = [table for schema in original_catalog.schemata for table in schema.tables]
    assert tables
    # The metaclass IDs are the saved model contract; filenames have changed.
    assert isinstance(tables[0], grt.classes.db_mysql_Table)
    grt.modules.Workbench.editObject(tables[0])
    parser = grt.modules.MySQLParserServices
    rdbms = grt.root.wb.rdbmsMgmt.rdbms[0]
    version = grt.classes.GrtVersion()
    version.majorNumber, version.minorNumber, version.releaseNumber = 8, 0, 47
    context = parser.createNewParserContext(rdbms.characterSets, version, '', 0)
    catalog = grt.classes.db_mysql_Catalog()
    result = parser.parseSQLIntoCatalogSql(context, catalog,
        'CREATE SCHEMA artifact_qa; USE artifact_qa; CREATE TABLE probe (id INT PRIMARY KEY);', {})
    assert result == 0 and len(catalog.schemata) == 1 and len(catalog.schemata[0].tables) == 1, result
    return {'ok': True, 'module_paths': paths, 'existing_model_opened': True,
            'table_editor_requested': True, 'in_memory_parser': True,
            'profile_templates': len(list(profile_path.glob('*.xml'))), 'connections': 0,
            'server_writes': False, 'model_saved': False}


try:
    result = run()
except Exception:
    result = {'ok': False, 'error': traceback.format_exc()}
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print('SDP_ARTIFACT_PROBE ' + json.dumps(result, ensure_ascii=False))
