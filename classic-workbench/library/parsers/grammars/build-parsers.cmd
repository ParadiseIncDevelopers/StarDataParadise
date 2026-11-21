echo off

if [%1] == [] goto Usage

IF %1 == mysql (
  echo Selected MySQL parser
  if defined SDP_JAVA_EXECUTABLE (
    "%SDP_JAVA_EXECUTABLE%" -Xmx1024m -jar "%WB_3DPARTY_PATH%\bin\antlr-4.13.2-complete.jar" -Dlanguage=Cpp -listener -visitor -o ../mysql -package parsers MySQLLexer.g4 MySQLParser.g4
  ) else (
    java -Xmx1024m -jar "%WB_3DPARTY_PATH%\bin\antlr-4.13.2-complete.jar" -Dlanguage=Cpp -listener -visitor -o ../mysql -package parsers MySQLLexer.g4 MySQLParser.g4
  )
  if errorlevel 1 exit /b 1
) ELSE (
  echo Unknown parser type %1
  exit /b 2
)

goto EndOfScript

:Usage
echo "Usage: $0 [mysql]"

:EndOfScript
