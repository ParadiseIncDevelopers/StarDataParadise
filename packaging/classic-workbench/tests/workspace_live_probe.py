"""Native workspace controls against a caller-owned disposable release fixture."""
import json
import os
from pathlib import Path
import traceback
import hashlib

CONFIG=json.loads(Path(os.environ['SDP_WORKSPACE_LIVE_CONFIG']).read_text(encoding='utf-8'))
OUTPUT=Path(CONFIG['result'])
PROGRESS=OUTPUT.with_suffix('.progress')
TRACE=OUTPUT.with_suffix('.trace.jsonl')


def progress(stage):
    with PROGRESS.open('a',encoding='utf-8') as stream:
        stream.write(stage+'\n')
        stream.flush()


progress('entry')


def trace(stage,**values):
    with TRACE.open('a',encoding='utf-8') as stream:
        stream.write(json.dumps(dict(stage=stage,**values),ensure_ascii=False,default=str)+'\n')
        stream.flush()


TRACED_SESSIONS=set()


def trace_backend(stage,backend,editor):
    from sdp_tools_backend import bind
    trace(stage,backend_id=id(backend),main_id=id(backend.main),read_id=id(backend.read),
          main_management=backend.main.management,read_management=backend.read.management,
          actual_editor=backend.editor==editor)
    for label,session in (('main',backend.main),('read',backend.read)):
        if id(session) in TRACED_SESSIONS:
            continue
        TRACED_SESSIONS.add(id(session))
        original=session.run_sql
        def traced(statement,parameters=(),original=original,session=session,label=label):
            source=bind(statement,parameters)
            trace('sql_before',session_id=id(session),session_label=label,
                  management=session.management,source=source)
            try:
                result=original(statement,parameters)
            except Exception as error:
                trace('sql_error',session_id=id(session),source=source,error=str(error))
                raise
            trace('sql_after',session_id=id(session),source=source,
                  columns=result.columns,rowcount=len(result.rows))
            return result
        session.run_sql=traced


def session_snapshot(stage,backend):
    identity=backend.main.run_sql('SELECT CONNECTION_ID(),DATABASE(),CURRENT_ROLE(),STARDATAPARADISE_PRINCIPALS()').fetch_one()
    trace(stage,connection_id=identity[0],database=identity[1],current_role=identity[2],
          principals=identity[3])
    rows=backend.main.run_sql('SELECT id,note FROM fixture.ui_rows ORDER BY id').fetch_all()
    trace(stage+'_ordinary_select',rows=rows)


def probe():
    import grt
    progress('grt_imported')
    import sdp_workspace_grt as workspace
    import sdp_workspace_backend as backend_module
    from sdp_workspace_backend import action_statement
    progress('imports_complete')
    for module in (workspace,backend_module):
        path=Path(module.__file__)
        trace('actual_module',module=module.__name__,path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    assert CONFIG['fixture']=='sdp-workspace-release-v1'
    editors=[value for value in grt.root.wb.sqlEditors if value.isConnected]
    assert len(editors)==1,'Exactly one disposable connected editor is required'
    editor=editors[0]
    progress('editor_found')
    assert str(editor.connection.parameterValues.get('hostName')) in ('localhost','127.0.0.1')
    progress('before_open_workspace')
    workspace.open_workspace(editor)
    progress('after_open_workspace')
    view=next(value for value in workspace._views if value.editor==editor)
    backend=view.backend
    trace_backend('workspace_backend',backend,editor)
    progress('before_identity')
    identity=backend.read.run_sql('SELECT @@server_uuid,@@port,@@datadir').fetch_one()
    progress('after_identity')
    assert identity[0]==CONFIG['uuid'] and int(identity[1])==CONFIG['port']
    assert Path(identity[2]).resolve()==Path(CONFIG['datadir']).resolve()
    progress('before_capabilities')
    capabilities=backend.refresh_capabilities()
    progress('after_capabilities')
    for key in ('stored_knowledge_v1','ai_conversation_v1','behavior_v1','response_schema_v1',
                'ai_models_v1','ai_action_v1','ai_role_acl_v1','short_sql_v1','ai_sync_v1'):
        backend.require(key)
    assert capabilities['native_language']['hist'] is False
    assert capabilities['features']['jobs'] is False
    assert len(view.pages)==8 and not any('HIST' in name for name in view.names)
    progress('before_inactive_role_catalog')
    assert not backend.list_objects('Knowledge').rows,'Inactive assigned role leaked catalog access'
    progress('before_set_role')
    editor.executeScript('SET ROLE ui_catalog_role;')
    assert backend.main.run_sql('SELECT CURRENT_ROLE()').fetch_one()[0]=='`ui_catalog_role`@`%`'
    progress('after_set_role')
    session_snapshot('after_set_role_identity',backend)
    names=('ui_k','ui_c','ui_b','ui_schema')
    counts={}
    for index,name in enumerate(names):
        progress('before_catalog_'+name)
        page=view.pages[index];page.fields['schema'].set_value('fixture');page.refresh()
        trace_backend('page_backend_'+name,page.backend,editor)
        trace('page_show_rows',name=name,backend_is_workspace=page.backend is backend,
              rowcount=len(page.current_rows),rows=page.current_rows)
        session_snapshot('before_load_'+name,page.backend)
        assert 'name' in page.grid_columns,'SHOW must produce selectable named objects'
        page.load_document('fixture',name)
        assert page.object['name']==name
        counts[page.kind]=len(page.current_rows)
        progress('after_catalog_'+name)
    knowledge=view.pages[0]
    assert knowledge.dependencies.data and knowledge.versions.data
    assert len(knowledge.signature_children)==4
    progress('before_model_catalog')
    model=view.pages[4];model.fields['schema'].set_value('fixture');model.fields['name'].set_value('ui_model')
    model.refresh();model.grid.select_node(model.grid.node_at_row(0));model.inspect()
    progress('after_model_catalog')
    # Exercise the main editor session, including an actual session variable.
    progress('before_prepare_action')
    backend.main.run_sql('PREPARE ACTION CALL fixture.ui_cancel(1) RETURNING ID INTO @sdp_action_id;')
    progress('after_prepare_action')
    identity=backend.main.run_sql('SELECT @sdp_action_id').fetch_one()[0]
    approval=view.pages[6];approval.fields['action_id'].set_value(identity)
    progress('before_inspect_action')
    approval.inspect_action()
    progress('after_inspect_action')
    assert approval.action_document['action_id']==identity
    assert approval.action_document['target']['name']=='ui_cancel'
    drafts=[];approval.review_sql=lambda title,source:drafts.append(source)
    for verb in ('APPROVE','DENY','EXECUTE'):
        progress('before_action_preview_'+verb)
        approval.inspect_action();approval.action_sql(verb)
        assert drafts[-1]==action_statement(verb,identity)
        progress('after_action_preview_'+verb)
    progress('before_ai_sql')
    view.open_ai_sql('fixture','ui_c')
    ai=view.pages[7];ai.fields['question'].set_value("Türkçe prompt'; SELECT 2; --")
    ai.prepare();source=ai.source.get_text(False)
    assert 'START AI CONVERSATION `fixture`.`ui_c`' in source and 'COMMIT ASYNC' in source
    assert 'SELECT 2' not in source
    progress('after_ai_sql')
    progress('before_open_sql')
    query=backend.open_sql('SELECT 41 + 1 AS workspace_release_check;')
    assert query is not None
    progress('after_open_sql')
    tls=backend.read.run_sql("SHOW SESSION STATUS LIKE 'Ssl_cipher'").fetch_one()
    assert tls and tls[1],'Native Workbench connection must use TLS'
    return dict(ok=True,native_pages=8,real_server_capabilities=True,catalog_counts=counts,
                knowledge_dependency_diagram=True,knowledge_version_timeline=True,
                action_preview_main_session=True,action_sql_pinned_id=True,separate_ai_sql=True,
                sql_tab_opened=True,active_sql_role_visibility=True,tls=True,provider_call='NOT_RUN',credential_write=False)


try:
    result=probe()
    progress('pass')
except Exception:
    progress('fail')
    result=dict(ok=False,error=traceback.format_exc())
OUTPUT.parent.mkdir(parents=True,exist_ok=True)
OUTPUT.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print('SDP_WORKSPACE_LIVE '+json.dumps(result,ensure_ascii=False))
