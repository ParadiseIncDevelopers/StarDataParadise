"""Construct actual native AI/SQL pages in an isolated Classic --configdir.

No connection, model, profile, credential or user file is read or changed.
Synthetic diagram data verifies rendering only, and is labelled as fixture.
"""
import json
import os
from pathlib import Path
import sys
import traceback
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
PACKAGED = os.environ.get('SDP_WORKSPACE_PROBE_PACKAGED') == '1'
if not PACKAGED:
    sys.path.insert(0, str(ROOT / 'classic-workbench/modules/wb.utils'))
OUTPUT = Path(os.environ.get('SDP_WORKSPACE_PROBE_OUTPUT') or ROOT / 'out/classic-ai-hist-ui/workspace-native-result.json')


def run():
    import sdp_workspace_grt as workspace
    if PACKAGED:
        import grt
        assert hasattr(grt.modules, 'SdpWorkspace'), 'Packaged GRT plugin was not discovered'
    from sdp_tools_backend import Rows
    editor = SimpleNamespace(defaultSchema='fixture', connection=SimpleNamespace(name='İzole native kabul', parameterValues={}))
    view = workspace.WorkspaceView(editor)
    assert len(view.pages) == 8
    assert all('HIST' not in name and 'JOB' not in name for name in view.names)
    assert not hasattr(workspace, 'HistoryPage')
    assert 'openHistory' not in [item[0] for item in workspace.ModuleInfo.functions]
    for index in range(len(view.pages)):
        view.select(index)
    knowledge = view.pages[0]
    knowledge.show_rows(Rows([('fixture_knowledge', 'fixture', 2)], ['name', 'schema_name', 'version']))
    knowledge.dependencies.update([dict(kind='Tablo', name='fixture_orders', version=None)], 'Knowledge - fixture_knowledge')
    knowledge.versions.update([dict(version=1, state='Önceki', time='2026-11-20'), dict(version=2, state='Etkin', time='2026-11-20')])
    view.open_ai_sql('fixture', 'fixture_conversation')
    assert view.pages[7].value('conversation') == 'fixture_conversation'
    source = workspace.SourceDialog('Fixture - SQL önizleme', 'SELECT 1;')
    definition = workspace.DefinitionDialog('Fixture - Tanımı düzenle', '{"type":"object"}', workspace.mforms.LanguageJson)
    fields = workspace.FieldsDialog('Fixture - Parametreler', [('p', 'Parametre - BIGINT', '', None)])
    secret = workspace.FieldsDialog('Fixture - Maskeli giriş', [('key', 'API anahtarı', '', None)], secret_fields=('key',))
    # Exercise the actual provider form controls/handlers. The metadata actor
    # is an explicit fixture, and no credential material is written or read.
    models = view.pages[4]
    models.backend.read = SimpleNamespace(run_sql=lambda _sql: Rows([('fixture_owner@localhost',)], ['actor']))
    models.backend.capabilities = dict(features=dict(ai_models_v1=True))
    drafts = []
    models.review_sql = lambda title, source: drafts.append((title, source))
    for provider in ('OPENAI', 'ANTHROPIC'):
        models.fields['provider'].set_value(provider)
        models.fields['name'].set_value('provider_fixture')
        models.fields['remote'].set_value('fixture-model')
        models.fields['credential_ref'].set_value('fixture-reference')
        models.fields['secret_env'].set_value('')
        models.worker_binding()
        details = models.details.get_string_value()
        assert 'Worker erişimi doğrulanmadı' in details and 'fixture_owner@localhost' in details
        assert 'StarDataParadise/ai/fixture-reference' in details
        models.registration()
        assert 'PROVIDER ' + provider in drafts[-1][1] and 'credential_ref' in drafts[-1][1]
    models.fields['endpoint'].set_value('https://proxy.example.com/messages')
    models.worker_binding()
    assert 'endpoints' in models.details.get_string_value()
    count = len(drafts)
    models.fields['endpoint'].set_value('https://proxy.example.com/messages?key=fixture')
    try:
        models.registration()
    except ValueError:
        pass
    else:
        raise AssertionError('Provider form accepted a query-bearing endpoint')
    assert len(drafts) == count
    models.fields['provider'].set_value('LOCAL_GGUF')
    models.fields['artifact'].set_value('fixture/model.gguf')
    models.fields['sha256'].set_value('a' * 64)
    models.worker_binding()
    assert 'import_roots' in models.details.get_string_value()
    models.registration()
    assert 'IMPORT MODEL' in drafts[-1][1] and 'context_tokens = 4096' in drafts[-1][1]
    # Actual action controls use a labelled metadata fixture and only generate
    # reviewable SQL. No approval or business procedure is executed by this probe.
    approval = view.pages[6]
    approval.backend.capabilities = dict(features=dict(ai_models_v1=True, ai_action_v1=True))
    action_id = 'b' * 32
    action_doc = dict(action_id=action_id, state='prepared', target=dict(schema_name='fixture', name='siparis_iptal'),
                      arguments=[9001], definition_hash='c' * 64, resource_scope='all_dependency_tables', resource_count=1)
    action_reads = []
    def read_action(sql):
        action_reads.append(sql)
        return Rows([(json.dumps(action_doc),)], ['document'])
    approval.backend.main = SimpleNamespace(run_sql=read_action)
    approval.review_sql = lambda title, source: drafts.append((title, source))
    for verb in ('APPROVE', 'DENY', 'EXECUTE'):
        approval.inspect_action()
        assert approval.action_document['action_id'] == action_id
        approval.action_sql(verb)
        assert drafts[-1][1] == verb + " ACTION '" + action_id + "';"
        assert approval.action_document is None
    assert all(sql.startswith('DESCRIBE ACTION ') for sql in action_reads)
    action_doc['state']='stale'
    approval.inspect_action()
    assert all(not control.is_enabled() for control in approval.action_buttons)
    action_doc['state']='prepared'
    approval.inspect_action()
    approval.fields['action_id'].set_value('a' * 32)
    count = len(drafts)
    try:
        approval.action_sql('EXECUTE')
    except ValueError:
        pass
    else:
        raise AssertionError('Action button accepted an invalidated preview')
    assert len(drafts) == count
    assert approval.action_document is None
    # Exercise both active native Cairo paint paths offscreen.
    from workbench.graphics.cairo_utils import Context, ImageSurface
    for graph in [knowledge.dependencies, knowledge.versions]:
        surface = ImageSurface(width=1000, height=400); context = Context(surface)
        graph.repaint(context.cr, 0, 0, 1000, 400)
    return dict(ok=True, native_pages=list(view.names), plugins=[item[0] for item in workspace.ModuleInfo.functions],
                diagrams='PASS', native_dialogs='PASS', masked_entry='PASS', separate_ai_sql='PASS',
                packaged_discovery='PASS' if PACKAGED else 'NOT_RUN (source import)',
                provider_form_actions='PASS (native controls; metadata actor fixture)', worker_access='NOT_VERIFIED (no live worker credential)',
                action_form_actions='PASS (main-session metadata fixture; immutable inspected ID; invalidation)',
                deferred_surfaces_absent='PASS (no HIST/remote/SQL JOB page or HIST plugin)',
                network_used=False, credentials_written=False, server_writes=False)


try:
    result = run()
except Exception:
    result = dict(ok=False, error=traceback.format_exc())
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print('SDP_WORKSPACE_PROBE ' + json.dumps(result, ensure_ascii=False))
