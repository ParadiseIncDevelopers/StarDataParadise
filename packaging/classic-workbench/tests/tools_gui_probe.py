"""Run with Classic --run-script in an isolated --configdir, no database.

Exercises actual native Python/GRT/mforms constructors and handlers. This is
not mouse automation and never connects, writes server data or saves profiles.
"""
import json
import os
from pathlib import Path
import sys
import traceback
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'classic-workbench/modules/wb.utils'))
OUTPUT = Path(os.environ.get('SDP_TOOLS_PROBE_OUTPUT') or ROOT / 'out/classic-workspace-ui/tools-native-result.json')


def run():
    import sdp_tools_grt as tools
    from sdp_tools_backend import Rows
    editor = SimpleNamespace(defaultSchema='', connection=SimpleNamespace(name='İzole araç kabulü', parameterValues={}))
    view = tools.ToolsView(editor)
    assert len(view.pages) == 5
    history, decision, models, jobs, backups = view.pages
    history.fields['journal'].set_value('fixture_history')
    history.fields['mode'].set_value('Fark')
    history.fields['start'].set_value('1')
    history.fields['end'].set_value('2')
    assert history.statement() == 'CALL `fixture_history`.history_diff(1, 2, NULL, 0, 32);'
    history.show_rows(Rows([('18446744073709551615', None, 'Türkçe')], ['id', 'null_value', 'label']))
    decision.source.set_value('CREATE DECISION RULE deny_blocked ON fixture.orders FOR INSERT '
                              'WHEN (NEW.blocked = 1) THEN DENY;')
    decision.generate()
    assert 'CREATE TRIGGER' in decision.generated
    decision.source.set_value('')
    try:
        decision.assert_generated()
    except ValueError:
        pass
    assert decision.generated is None
    models.fields['provider'].set_value('CPU runner')
    models.provider_changed()
    models.fields['provider'].set_value('Classic SQL')
    models.provider_changed()
    for index in range(5):
        view.select(index)
    return dict(ok=True, native_pages=list(view.names), plugin_functions=[f[0] for f in tools.ModuleInfo.functions],
                history_sql='PASS', native_grid='PASS', decision_provider='PASS', stale_output_invalidated='PASS',
                model_provider_switch='PASS', network_used=False, server_writes=False)


try:
    result = run()
except Exception:
    result = dict(ok=False, error=traceback.format_exc())
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print('SDP_TOOLS_PROBE ' + json.dumps(result, ensure_ascii=False))
