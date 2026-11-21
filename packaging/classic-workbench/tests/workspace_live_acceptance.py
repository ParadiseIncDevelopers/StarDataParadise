"""Run native AI workspace controls against a fresh release Server, never user data."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

REPO=Path(__file__).resolve().parents[3]
SERVER=REPO.parent/'StarDataParadiseServer'
sys.path.insert(0,str(SERVER))
from compatibility.tests.fixture import BASEDIR,BIN,SHELL,MIGRATION,Fixture,query,require
from compatibility.tests.ai_catalog_acceptance import execute,install,call
from compatibility.tests.workbench_native import backend_certificate,ENVIRONMENT


def preserve_diagnostics(profile,result,output):
    if result.is_file():
        shutil.copy2(result,output)
    progress=result.with_suffix('.progress')
    if progress.is_file():
        shutil.copy2(progress,output.with_suffix('.progress'))
    trace=result.with_suffix('.trace.jsonl')
    if trace.is_file():
        shutil.copy2(trace,output.with_suffix('.trace.jsonl'))
    for log in sorted((profile/'log').glob('wb*.log')):
        if log.is_file():
            shutil.copy2(log,output.with_name(output.stem+'.'+log.name))


def wait_for_complete_result(process,result,timeout):
    deadline=time.monotonic()+timeout
    while True:
        code=process.poll()
        require(code in (None,0),'Native probe crashed before a complete result: '+str(code))
        if result.is_file():
            try:
                value=json.loads(result.read_text(encoding='utf-8'))
            except (json.JSONDecodeError,OSError):
                pass
            else:
                return value
        require(code is None,'Native probe exited without a complete result')
        require(time.monotonic()<deadline,'Native probe did not produce a complete result before timeout')
        time.sleep(0.1)


def collect_native_probe(process,result,output,timeout):
    value=None
    complete=False
    forced=False
    before_cleanup=None
    try:
        value=wait_for_complete_result(process,result,timeout)
        complete=True
        require(isinstance(value,dict) and value.get('ok') is True,
                'Native workspace probe assertions failed: '+str(value.get('error','invalid result') if isinstance(value,dict) else value))
        try:
            process.wait(timeout=0.25)
        except subprocess.TimeoutExpired:
            pass
        require(process.poll() in (None,0),'Native probe crashed after writing its result: '+str(process.poll()))
        return value
    finally:
        before_cleanup=process.poll()
        if before_cleanup is None:
            forced=True
            process.terminate()
            process.wait(timeout=20)
        lifecycle=dict(owned_pid=process.pid,result_complete=complete,
            result_ok=isinstance(value,dict) and value.get('ok') is True,
            exit_before_cleanup=before_cleanup,exit_code=process.returncode,forced_owned_cleanup=forced,
            cleanup_phase=('after_complete_result' if complete else 'before_complete_result') if forced else 'none',
            normal_gui_shutdown=not forced and process.returncode==0,process_timeout_seconds=timeout)
        output.with_suffix('.lifecycle.json').write_text(json.dumps(lifecycle,indent=2)+'\n',encoding='utf-8')
        if complete and isinstance(value,dict) and value.get('ok') is True:
            require(before_cleanup in (None,0),'Native probe crashed before parent cleanup: '+str(before_cleanup))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--mysqld',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--probe-script',type=Path,help='Diagnostic replacement probe; does not establish workspace acceptance')
    parser.add_argument('--process-timeout',type=int,default=150)
    args=parser.parse_args()
    require(1<=args.process_timeout<=600,'Process timeout must be between 1 and 600 seconds')
    runtime=args.runtime.resolve(strict=True)
    executable=runtime/'StarDataParadise.exe'
    require(executable.is_file(),'Native Workbench executable missing')
    fixture=Fixture(args.mysqld.resolve(strict=True),BIN/'mysql.exe',BASEDIR,SHELL,None)
    def bootstrap(instance):
        install(instance,False)
        for name in ('tool_catalog.sql','action_catalog.sql'):
            execute(instance,(SERVER/'compatibility/ai'/name).read_text(encoding='utf-8'))
    with fixture.scratch('workspace-release') as root:
        cert=backend_certificate(MIGRATION/'toolchain/openssl-firedaemon/x64/bin/openssl.exe',root,'cert')
        with fixture.disposable(root/'server',tls_dir=cert,bootstrap=bootstrap,
          bootstrap_options=('--sdp-ai-sql=ON','--sdp-ai-actions=OFF'),
          server_options=('--sdp-ai-sql=ON','--sdp-ai-actions=ON')) as instance:
            execute(instance,'''CREATE TABLE fixture.ui_rows(id BIGINT PRIMARY KEY,note VARCHAR(40)) ENGINE=InnoDB;
              INSERT INTO fixture.ui_rows VALUES(1,'open');
              CREATE KNOWLEDGE fixture.ui_k(p BIGINT NOT NULL) RETURNS TABLE(id BIGINT NOT NULL,note VARCHAR(40) NULL)
                READ ONLY SQL SECURITY INVOKER AS SELECT id,note FROM fixture.ui_rows WHERE id=:p;
              CREATE BEHAVIOR fixture.ui_b AS 'Türkçe cevap ver.';
              CREATE RESPONSE SCHEMA fixture.ui_schema VERSION 1 AS JSON '{"type":"object","properties":{},"additionalProperties":false}';
              CREATE PROCEDURE fixture.ui_cancel(IN p BIGINT) SQL SECURITY INVOKER UPDATE fixture.ui_rows SET note='cancelled' WHERE id=p;
              REQUIRE APPROVAL ON PROCEDURE fixture.ui_cancel;
            ''')
            execute(instance,'''DELIMITER $$
              CREATE PROCEDURE fixture.ui_metadata_two() SQL SECURITY INVOKER
              BEGIN SET @ui_metadata_calls=COALESCE(@ui_metadata_calls,0)+1;
                SELECT 11 AS `first_value`; SELECT 22 AS trailing_value; END$$
              CREATE PROCEDURE fixture.ui_metadata_error() SQL SECURITY INVOKER
              BEGIN SET @ui_metadata_error_calls=COALESCE(@ui_metadata_error_calls,0)+1;
                SELECT 33 AS `first_value`;
                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='ui metadata late signal'; END$$
              DELIMITER ;
            ''')
            execute(instance,call('create','model','ui_model',dict(provider='OPENAI',remote_model='fixture-model',settings=dict(secret_env='UNUSED_FIXTURE_KEY'))))
            execute(instance,'CREATE AI CONVERSATION fixture.ui_c USING MODEL fixture.ui_model NO FUNCTIONS MEMORY PERSISTENT;')
            execute(instance,'''CREATE USER ui_viewer@localhost IDENTIFIED BY '' REQUIRE SSL;
              CREATE ROLE ui_catalog_role;
              GRANT ui_catalog_role TO ui_viewer@localhost;
              GRANT EXECUTE ON PROCEDURE sdp_ai.dispatch TO ui_viewer@localhost;
              GRANT EXECUTE ON PROCEDURE sdp_ai.object_document TO ui_viewer@localhost;
              GRANT USE ON MODEL fixture.ui_model TO ROLE ui_catalog_role;
              GRANT USE ON BEHAVIOR fixture.ui_b TO ROLE ui_catalog_role;
              GRANT USE ON RESPONSE SCHEMA fixture.ui_schema TO ROLE ui_catalog_role;
              GRANT EXECUTE ON KNOWLEDGE fixture.ui_k TO ROLE ui_catalog_role;
              GRANT SELECT,APPEND ON CONVERSATION fixture.ui_c TO ROLE ui_catalog_role;
              GRANT EXECUTE ON PROCEDURE fixture.ui_cancel TO ROLE ui_catalog_role;
              GRANT EXECUTE ON PROCEDURE fixture.ui_metadata_two TO ROLE ui_catalog_role;
              GRANT EXECUTE ON PROCEDURE fixture.ui_metadata_error TO ROLE ui_catalog_role;
              GRANT SELECT,UPDATE ON fixture.ui_rows TO ui_catalog_role;
              SET DEFAULT ROLE NONE TO ui_viewer@localhost;
            ''')
            uuid,port,datadir=query(instance,'SELECT @@server_uuid,@@port,@@datadir').split('\t')
            profile=root/'profile';profile.mkdir()
            temporary=root/'tmp';temporary.mkdir()
            result=root/'result.json';config=root/'config.json'
            config.write_text(json.dumps(dict(fixture='sdp-workspace-release-v1',uuid=uuid,port=int(port),datadir=datadir,
                result=str(result),profile=str(profile),ca=str(cert/'ca.pem'))),encoding='utf-8')
            environment={key:value for key,value in os.environ.items() if key.upper() in ENVIRONMENT}
            environment.update(TEMP=str(temporary),TMP=str(temporary))
            environment['SDP_WORKSPACE_LIVE_CONFIG']=str(config)
            environment['PATH']=str(runtime)+os.pathsep+environment.get('PATH','')
            profile_command=[str(executable),'--configdir='+str(profile),
                '--run-script='+str(Path(__file__).with_name('workspace_live_profile.py')),
                '--quit-when-done','--log-level=debug3','--log-to-stderr']
            output=args.output.resolve();output.parent.mkdir(parents=True,exist_ok=True)
            try:
                with output.with_suffix('.profile.log').open('wb') as log:
                    preparation=subprocess.run(profile_command,cwd=runtime,env=environment,stdin=subprocess.DEVNULL,
                        stdout=log,stderr=subprocess.STDOUT,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                preparation_state=dict(exit_code=preparation.returncode,
                    connections_xml=(profile/'connections.xml').is_file())
                output.with_suffix('.preparation.json').write_text(json.dumps(preparation_state,indent=2)+'\n',encoding='utf-8')
                require(preparation.returncode==0 and preparation_state['connections_xml'],
                        'Disposable TLS profile preparation failed: '+json.dumps(preparation_state))
                probe=(args.probe_script or Path(__file__).with_name('workspace_live_probe.py')).resolve(strict=True)
                code='exec(compile(open('+repr(str(probe)) +", encoding='utf-8').read(), "+repr(str(probe))+", 'exec'))"
                command=[str(executable),'--configdir='+str(profile),'--query=Workbench release fixture',
                         '--run-python='+code,'--quit-when-done','--log-level=debug3','--log-to-stderr']
                with output.with_suffix('.process.log').open('wb') as log:
                    process=subprocess.Popen(command,cwd=runtime,env=environment,stdin=subprocess.DEVNULL,
                                             stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    value=collect_native_probe(process,result,output,args.process_timeout)
                output.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
                require(query(instance,'SELECT note FROM fixture.ui_rows WHERE id=1')=='open','Preview changed fixture business data')
                if args.probe_script:
                    print('PASS connected-editor diagnostic probe; fixture business data unchanged (workspace acceptance NOT_RUN)',flush=True)
                else:
                    print('PASS native live workspace: real accepted capabilities, eight pages, catalogs, Knowledge diagrams, main-session action preview, pinned SQL, separate AI SQL and TLS',flush=True)
            finally:
                preserve_diagnostics(profile,result,output)


if __name__=='__main__':main()
