"""Actual native GRT query completion/lifetime regression, no workspace view."""
import json
import os
from pathlib import Path
import traceback

config=json.loads(Path(os.environ['SDP_WORKSPACE_LIVE_CONFIG']).read_text())
output=Path(config['result'])
progress=output.with_suffix('.progress')

def mark(value):
    with progress.open('a',encoding='utf-8') as stream:
        stream.write(value+'\n');stream.flush()

def first(result):
    assert result.goToFirstRow()
    return [result.stringFieldValue(index) for index in range(len(result.columns))]

def probe():
    import grt
    editor,=[value for value in grt.root.wb.sqlEditors if value.isConnected]
    editor.executeScript('SET ROLE ui_catalog_role;')
    query=lambda sql:editor.executeQuery(sql,0)
    initial=first(query('SELECT CONNECTION_ID(),CURRENT_ROLE()'))
    assert initial[1]=='`ui_catalog_role`@`%`'
    def unchanged(stage):
        identity=first(query('SELECT CONNECTION_ID(),CURRENT_ROLE()'))
        assert identity==initial,(stage,identity,initial)
        assert first(query('SELECT id,note FROM fixture.ui_rows'))==['1','open']
        mark(stage)

    ordinary=query("SELECT 18446744073709551615 AS u,CAST('123.4500' AS DECIMAL(9,4)) AS d,NULL AS n,'Türkçe' AS t")
    values=first(ordinary)
    assert values==['18446744073709551615','123.4500',None,'Türkçe'],values
    unchanged('ordinary_select_same_session')
    assert first(ordinary)==values
    empty=query('SELECT id FROM fixture.ui_rows WHERE id=99')
    assert not empty.goToFirstRow()
    unchanged('empty_select_same_session')

    for count in range(3):
        shown=query('SHOW KNOWLEDGES;')
        columns=[column.name for column in shown.columns]
        unchanged('show_same_session_'+str(count))
        row=first(shown)
        assert row[columns.index('name')]=='ui_k' and row[columns.index('schema_name')]=='fixture'
        described=query('DESCRIBE KNOWLEDGE `fixture`.`ui_k`;')
        unchanged('describe_same_session_'+str(count))
        assert json.loads(first(described)[0])['name']=='ui_k'

    multiple=query('CALL fixture.ui_metadata_two();')
    unchanged('two_results_drained_same_session')
    assert [column.name for column in multiple.columns]==['first_value']
    assert first(multiple)==['11']
    assert first(query('SELECT @ui_metadata_calls'))==['1']
    mark('first_buffered_result_readable_after_followup_queries')

    try:
        query('CALL fixture.ui_metadata_error();')
    except grt.DBError as error:
        assert '1644' in str(error) and 'ui metadata late signal' in str(error),str(error)
    else:
        raise AssertionError('Late SIGNAL was swallowed')
    unchanged('late_error_propagated_same_session')
    assert first(query('SELECT @ui_metadata_error_calls'))==['1']

    editor.executeManagementCommand('SET ROLE ui_catalog_role;',0)
    auxiliary=lambda sql:editor.executeManagementQuery(sql,0)
    aux_identity=first(auxiliary('SELECT CONNECTION_ID(),CURRENT_ROLE()'))
    assert aux_identity[0]!=initial[0] and aux_identity[1]==initial[1]
    aux_shown=auxiliary('SHOW KNOWLEDGES;')
    assert first(auxiliary('SELECT CONNECTION_ID(),CURRENT_ROLE()'))==aux_identity
    assert first(auxiliary('SELECT id,note FROM fixture.ui_rows'))==['1','open']
    assert first(aux_shown)[[column.name for column in aux_shown.columns].index('name')]=='ui_k'
    aux_multiple=auxiliary('CALL fixture.ui_metadata_two();')
    assert first(auxiliary('SELECT CONNECTION_ID(),CURRENT_ROLE()'))==aux_identity
    assert first(aux_multiple)==['11'] and first(auxiliary('SELECT @ui_metadata_calls'))==['1']
    try:
        auxiliary('CALL fixture.ui_metadata_error();')
    except grt.DBError as error:
        assert '1644' in str(error) and 'ui metadata late signal' in str(error)
    else:
        raise AssertionError('Auxiliary late SIGNAL was swallowed')
    assert first(auxiliary('SELECT CONNECTION_ID(),CURRENT_ROLE()'))==aux_identity
    assert first(auxiliary('SELECT @ui_metadata_error_calls'))==['1']
    unchanged('main_and_aux_query_paths_complete')
    return dict(ok=True,diagnostic='actual GRT buffered first result and complete trailing results',
        main_connection_id=initial[0],aux_connection_id=aux_identity[0],roles_preserved=True,
        ordinary_values=values,first_result_lifetime=True,show_describe_repeated=3,
        two_result_call_once=True,late_signal_propagated=True,aux_path=True,workspace='NOT_RUN')

mark('query_completion_entry')
try:
    value=probe();mark('pass')
except Exception:
    value=dict(ok=False,error=traceback.format_exc());mark('fail')
output.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
