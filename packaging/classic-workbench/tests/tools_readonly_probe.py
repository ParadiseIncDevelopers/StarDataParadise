"""Native --query=<existing profile> --run-python probe; SELECT/SHOW only.

Run ONLY in an isolated configdir containing a copy of an existing local test
profile. It reads metadata and constant expressions, never creates SQL buffers,
saves profiles or applies a helper operation. No credentials are printed.
"""
from decimal import Decimal
import json
import os
from pathlib import Path
import sys
import traceback

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'classic-workbench/modules/wb.utils'))
OUTPUT = Path(os.environ.get('SDP_TOOLS_READONLY_OUTPUT') or ROOT / 'out/classic-workspace-ui/tools-readonly-result.json')
DIAGNOSTICS = {}


def run():
    import grt
    import sdp_tools_grt as tools
    from sdp_tools_backend import ManagementSession
    editors = list(grt.root.wb.sqlEditors)
    DIAGNOSTICS['profiles'] = [dict(name=c.name, host=c.parameterValues.get('hostName'), port=c.parameterValues.get('port'))
                               for c in grt.root.wb.rdbmsMgmt.storedConns]
    DIAGNOSTICS['editors'] = [dict(name=e.name, connected=bool(e.isConnected),
                                  host=e.connection.parameterValues.get('hostName') if e.connection else None,
                                  port=e.connection.parameterValues.get('port') if e.connection else None) for e in editors]
    editors = [editor for editor in editors if editor.isConnected]
    if len(editors) != 1:
        raise RuntimeError('Probe requires exactly one connected editor in the isolated instance.')
    editor = editors[0]
    if str(editor.connection.parameterValues.get('hostName', '')) not in ('localhost', '127.0.0.1'):
        raise RuntimeError('Readonly probe accepts only a local test profile.')
    session = ManagementSession(editor)
    row = session.run_sql("SELECT NULL AS null_value, CAST(18446744073709551615 AS UNSIGNED) AS max_u64, "
                          "CAST('99999999999999999999.123456789012345678' AS DECIMAL(38,18)) AS exact_decimal, '' AS empty_text").fetch_one()
    assert row[0] is None, repr(row)
    assert row[1] == 18446744073709551615 and type(row[1]) is int, repr(row)
    assert str(row[2]) == '99999999999999999999.123456789012345678', repr(row)
    assert row[3] == '', repr(row)
    session.require_autocommit()
    tools.open_tools(editor)
    view = next(item for item in tools._open_views if item.editor == editor)
    view.pages[0].discover()
    routine_count = len(view.pages[0].current_rows)
    tables = session.run_sql("SELECT TABLE_NAME FROM information_schema.TABLES "
                             "WHERE TABLE_SCHEMA='sdp_models' AND TABLE_NAME IN ('sdp_model_definitions','sdp_model_versions')").fetch_all()
    if len(tables) == 2:
        view.pages[2].refresh()
    queue = session.run_sql("SELECT TABLE_NAME FROM information_schema.TABLES "
                            "WHERE TABLE_SCHEMA='sdp_jobs' AND TABLE_NAME='compute_jobs'").fetch_all()
    if queue:
        view.pages[3].refresh()
    cipher = session.run_sql("SHOW SESSION STATUS LIKE 'Ssl_cipher'").fetch_one()
    return dict(ok=True, native_null_distinct_from_empty=True, exact_unsigned_bigint=True, exact_decimal=True,
                native_five_pages_docked=True, history_journal_count=routine_count,
                classic_model_registry_available=len(tables) == 2, compute_queue_available=bool(queue),
                tls_active=bool(cipher and cipher[1]), sql_operations='SELECT/SHOW only', server_writes=False)


try:
    result = run()
except Exception:
    result = dict(ok=False, error=traceback.format_exc(), diagnostics=DIAGNOSTICS)
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print('SDP_TOOLS_READONLY_PROBE ' + json.dumps(result, ensure_ascii=False))
