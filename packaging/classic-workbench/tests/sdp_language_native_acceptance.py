"""Execute real splitter and lexer/completion code from the native editor build."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
SOURCE=ROOT/'classic-workbench/modules/db.mysql.parser/src/mysql_parser_module.cpp'


def splitter():
    out=ROOT/'out/sdp-language-native-tests'
    out.mkdir(parents=True,exist_ok=True)
    source=SOURCE.read_text(encoding='utf-8-sig')
    start=source.index('static const unsigned char *skipLeadingWhitespace(')
    end=source.index('grt::BaseListRef MySQLParserServicesImpl::getSqlStatementRanges',start)
    utilities=source[start:end]
    start=source.index('size_t MySQLParserServicesImpl::determineStatementRanges(')
    end=source.index('//----------------------------------------------------------------------------------------------------------------------',start)
    splitter=source[start:end].replace('MySQLParserServicesImpl::determineStatementRanges','split',1)
    cases=[]
    def add(body, tail='SELECT 2'):
        cases.append((body+';'+tail+';',[body,tail]))
    add("CREATE PROCEDURE p() AS LET n=0;WHEN n>0 DO INSERT INTO t VALUES(';','END IF');END")
    add("CREATE PROCEDURE p() AS LET n=0;IF n>0 THEN BEGIN LET x='CASE END';END;ELSE FOR r IN SELECT CASE WHEN id>0 THEN 'END' ELSE 'BEGIN' END AS label FROM t DO WHEN :r.id>0 DO SET n=n+1;END FOR;END IF;END")
    add("CREATE FUNCTION f(n INT) RETURNS INT AS RETURN n+1")
    add("CREATE PROCEDURE p() AS LET n=0;WHILE n<1 DO SET n=n+1;END WHILE;REPEAT SET n=n+1;UNTIL n>2 END REPEAT;label: LOOP LEAVE label;END LOOP;END")
    add("CREATE PROCEDURE p() AS LET n=CASE WHEN 1 THEN IF(1,2,3) ELSE 0 END;SELECT `END`,delimiter FROM t;END")
    add("CREATE PROCEDURE p() AS /* END; */ LET şehir='İstanbul; END';-- END IF;\nWHEN şehir<>'' DO SET @x=şehir;END")
    add("CREATE PROCEDURE p() LANGUAGE JAVA AS 'external; body; END'")
    body="CREATE PROCEDURE p() AS LET n=1;BEGIN WHEN n>0 DO BEGIN SET n=2;END;END;END"
    cases.append(('DELIMITER //\n'+body+'//\nDELIMITER ;\nSELECT 3;',[body,'SELECT 3']))
    old="CREATE PROCEDURE old() BEGIN SELECT 1;SELECT 2;END"
    cases.append(('DELIMITER $$\n'+old+'$$\nDELIMITER ;\nSELECT 3;',[old,'SELECT 3']))
    incomplete='CREATE PROCEDURE p() AS LET n=0;INSERT INTO t VALUES(1);SELECT 9;'
    cases.append((incomplete,[incomplete]))
    cases.append(("SELECT 'AS; END; |>','delimiter //';SELECT 1|2;",["SELECT 'AS; END; |>','delimiter //'",'SELECT 1|2']))
    cases.append(('/* AS END; */\nSELECT 1;# END;\nSELECT 2;',['SELECT 1','SELECT 2']))
    header=(ROOT/'classic-workbench/library/parsers/sdp-routine-boundary.h').as_posix()
    prefix='''#include <algorithm>\n#include <iostream>\n#include <string>\n#include <vector>\n#include <utility>\n'''
    prefix+='#include "'+header+'"\nusing namespace parsers;\n'
    prefix+='''struct StatementRange { size_t line,start,length; };
namespace base { std::string trim(const std::string &s) {
 const auto a=s.find_first_not_of(" \\t\\r\\n");
 return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \\t\\r\\n")-a+1);
} }
'''
    rows=[]
    for text, expected in cases:
        rows.append('{'+json.dumps(text,ensure_ascii=False)+',{'+','.join(json.dumps(s,ensure_ascii=False) for s in expected)+'}}')
    tests='''
int main() {
 std::vector<std::pair<std::string,std::vector<std::string>>> cases={'''+','.join(rows)+'''};
 size_t count=0;
 for (const auto &test:cases) {
  std::vector<StatementRange> ranges;
  split(test.first.c_str(),test.first.size(),";",ranges,"\\n");
  if (ranges.size()!=test.second.size()) { std::cerr<<"range count case "<<count<<": "<<ranges.size()<<" != "<<test.second.size()<<"\\n";return 1; }
  size_t search=0;
  for(size_t i=0;i<ranges.size();++i) {
   auto expected=test.second[i];auto at=test.first.find(expected,search);
   if (ranges[i].start!=at || test.first.substr(ranges[i].start,ranges[i].length)!=expected) {
    std::cerr<<"source range case "<<count<<" item "<<i<<"\\n";return 2;
   }
   search=at+expected.size();
  }
  ++count;
 }
 std::cout<<"PASS actual native editor splitter: "<<count<<" scripts, source offsets, nested AS scopes, quotes/comments, AS RETURN and custom delimiters\\n";
}
'''
    cpp=out/'splitter_acceptance.cpp';cpp.write_text(prefix+utilities+splitter+tests,encoding='utf-8')
    vc=Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231')
    kit=Path('C:/Program Files (x86)/Windows Kits/10')
    env=os.environ.copy()
    env['INCLUDE']=';'.join(str(p) for p in (vc/'include',kit/'Include/10.0.26100.0/ucrt',kit/'Include/10.0.26100.0/shared',kit/'Include/10.0.26100.0/um'))
    env['LIB']=';'.join(str(p) for p in (vc/'lib/x64',kit/'Lib/10.0.26100.0/ucrt/x64',kit/'Lib/10.0.26100.0/um/x64'))
    env['PATH']=str(vc/'bin/Hostx64/x64')+os.pathsep+env['PATH']
    hidden=getattr(subprocess,'CREATE_NO_WINDOW',0)
    exe=out/'splitter_acceptance.exe'
    compile=subprocess.run([str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/EHsc','/std:c++17','/utf-8',str(cpp),'/Fe'+str(exe),'/Fo'+str(out/'splitter_acceptance.obj')],env=env,capture_output=True,text=True,creationflags=hidden)
    (out/'compile.log').write_text(compile.stdout+compile.stderr)
    if compile.returncode: raise RuntimeError(compile.stdout+compile.stderr)
    result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,creationflags=hidden)
    (out/'result.log').write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError(result.stdout+result.stderr)
    print(result.stdout.strip())


def completion(runtime):
    out=ROOT/'out/sdp-language-native-tests'
    out.mkdir(parents=True,exist_ok=True)
    vc=Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231')
    kit=Path('C:/Program Files (x86)/Windows Kits/10')
    deps=ROOT/'out/workbench8-deps'
    env=os.environ.copy()
    env['INCLUDE']=';'.join(str(p) for p in (vc/'include',kit/'Include/10.0.26100.0/ucrt',kit/'Include/10.0.26100.0/shared',kit/'Include/10.0.26100.0/um'))
    env['LIB']=';'.join(str(p) for p in (vc/'lib/x64',kit/'Lib/10.0.26100.0/ucrt/x64',kit/'Lib/10.0.26100.0/um/x64',runtime,deps/'lib'))
    env['PATH']=os.pathsep.join((str(runtime),str(deps/'bin'),str(vc/'bin/Hostx64/x64'),env['PATH']))
    hidden=getattr(subprocess,'CREATE_NO_WINDOW',0)
    exe=out/'editor_acceptance.exe'
    includes=[ROOT/'classic-workbench/library/parsers',ROOT/'out/mysql-workbench-native-src/library/parsers',ROOT/'classic-workbench/library/base',ROOT/'classic-workbench/res/server',deps/'include/antlr4-runtime',deps/'include']
    command=[str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/EHsc','/MD','/std:c++17','/utf-8','/D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING',*[f'/I{path}' for path in includes],str(Path(__file__).with_name('sdp_editor_acceptance.cpp')),'/Fe'+str(exe),'/Fo'+str(out/'editor_acceptance.obj'),'/link','parsers.lib','base.lib','antlr4-runtime.lib']
    compile=subprocess.run(command,env=env,capture_output=True,text=True,creationflags=hidden)
    (out/'editor-compile.log').write_text(compile.stdout+compile.stderr)
    if compile.returncode: raise RuntimeError(compile.stdout+compile.stderr)
    result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,creationflags=hidden)
    (out/'editor-result.log').write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError(f'editor acceptance exit {result.returncode}: '+result.stdout+result.stderr)
    print(result.stdout.strip())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,help='Build-Workspace runtime containing the updated parsers.dll')
    args=parser.parse_args()
    # A failed grammar generator must fail the native target. Previously the
    # batch script continued after "java not found" and reused stale C++ files.
    env=os.environ.copy()
    env['SDP_JAVA_EXECUTABLE']='__sdp_missing_java_for_acceptance__.exe'
    result=subprocess.run(['cmd.exe','/d','/c','call build-parsers.cmd mysql'],
                          cwd=ROOT/'classic-workbench/library/parsers/grammars',env=env,
                          capture_output=True,text=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if result.returncode == 0: raise RuntimeError('failed grammar generation was reported as successful')
    print('PASS failed native grammar generation returns a build error')
    splitter()
    if args.runtime: completion(args.runtime.resolve(strict=True))


if __name__=='__main__':
    main()
