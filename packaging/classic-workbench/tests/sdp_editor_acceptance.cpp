#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

#include "antlr4-runtime.h"
#include "mysql/MySQLLexer.h"
#include "mysql/MySQLParser.h"
#include "SymbolTable.h"
#include "base/log.h"
#include "code-completion/mysql-code-completion.h"
#include "sdp-editor-tokens.h"
#include "sdp-language-keywords.h"
#include "sdp-catalog-metadata.h"

using namespace parsers;
using namespace antlr4;

static SymbolTable symbols;
static size_t checks = 0;

static void expect(bool value, const std::string &label) {
  ++checks;
  if (!value) throw std::runtime_error(label);
}

struct Caret {
  std::string sql;
  size_t line = 0, column = 0;
  explicit Caret(std::string text) : sql(std::move(text)) {
    const auto marker = sql.find('^');
    const auto offset = marker == std::string::npos ? sql.size() : marker;
    if (marker != std::string::npos) sql.erase(marker, 1);
    for (size_t i = 0; i < offset; ++i) {
      if (sql[i] == '\n') { ++line; column = 0; }
      else if ((static_cast<unsigned char>(sql[i]) & 0xc0) != 0x80) ++column;
    }
  }
};

static bool candidate(const std::string &sql, unsigned features, const std::string &word) {
  const Caret caret(sql);
  ANTLRInputStream input(caret.sql);
  MySQLLexer lexer(&input);
  lexer.serverVersion = 261000;
  lexer.removeErrorListeners();
  CommonTokenStream tokens(&lexer);
  MySQLParser parser(&tokens);
  parser.serverVersion = lexer.serverVersion;
  parser.sdpLanguageFeatures = features;
  parser.removeErrorListeners();
  tokens.fill();
  const auto values = getCodeCompletionList(caret.line, caret.column, "test", true, &parser, symbols);
  return std::any_of(values.begin(), values.end(), [&](const auto &entry) { return entry.second == word; });
}

static bool authoritative(const std::string &sql, unsigned features) {
  ANTLRInputStream input(sql);
  MySQLLexer lexer(&input);
  lexer.serverVersion = 261000;
  lexer.removeErrorListeners();
  CommonTokenStream tokens(&lexer);
  const auto context = sdpEditorTokens(&tokens, std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());
  return sdpUsesServerValidation(context, features);
}

static std::string localHover(const std::string &sql,unsigned features=SdpSqlShort) {
  const Caret caret(sql);
  return getSdpLocalHover(caret.line,caret.column,caret.sql,"test",features,261000,"",symbols);
}

static std::string knowledgeCalltip(const std::string &sql,unsigned features=SdpAiSql) {
  const Caret caret(sql);
  return getSdpKnowledgeCalltip(caret.line,caret.column,caret.sql,"test",features,261000,"",symbols);
}

int main() {
  try {
    base::Logger logger(false, std::string());
    auto *schema = symbols.addNewSymbol<SchemaSymbol>(nullptr, "test");
    auto *table = symbols.addNewSymbol<TableSymbol>(schema, "film");
    symbols.addNewSymbol<ColumnSymbol>(table, "title", nullptr);
    symbols.addNewSymbol<ColumnSymbol>(table, "description", nullptr);
    auto *knowledge=symbols.addNewSymbol<SdpCatalogSymbol>(schema, "orders_knowledge", "knowledge");
    expect(sdpLoadKnowledgeMetadata(*knowledge,R"json({"parameters":[{"name":"customer","type":"BIGINT UNSIGNED"}],"result_columns":[{"name":"order_id","type":"BIGINT"},{"name":"total","type":"DECIMAL(18,2)"}],"definition":"not retained"})json"),"native DESCRIBE metadata cache");
    symbols.addNewSymbol<SdpCatalogSymbol>(schema, "brief_behavior", "behavior");
    symbols.addNewSymbol<SdpCatalogSymbol>(schema, "sales_model", "model");
    symbols.addNewSymbol<SdpCatalogSymbol>(schema, "assistant_session", "conversation");
    symbols.addNewSymbol<SdpCatalogSymbol>(schema, "0123456789abcdef0123456789abcdef", "hist_revision");
    expect(candidate("SEL^", 0, "SELECT"), "ordinary completion regressed");
    expect(!candidate("CREATE KN^", 0, "KNOWLEDGE"), "AI family leaked without capability");
    expect(candidate("CREATE KN^", SdpAiSql, "KNOWLEDGE"), "CREATE KNOWLEDGE completion");
    expect(candidate("EXECUTE KN^", SdpAiSql, "KNOWLEDGE"), "EXECUTE KNOWLEDGE completion");
    expect(candidate("SHOW KNOW^", SdpAiSql, "KNOWLEDGES"), "SHOW KNOWLEDGES completion");
    expect(candidate("EXECUTE KNOWLEDGE orders^", SdpAiSql, "orders_knowledge"), "cached Knowledge name completion");
    expect(!candidate("EXECUTE KNOWLEDGE ^", 0, "orders_knowledge"), "native object cache leaked without capability");
    expect(!candidate("EXECUTE KNOWLEDGE ^", SdpAiSql, "brief_behavior"), "wrong catalog kind offered for Knowledge");
    expect(candidate("DESCRIBE KNOWLEDGE test.^", SdpAiSql, "orders_knowledge"), "qualified Knowledge name completion");
    expect(candidate("DESCRIBE KNOWLEDGE `test`.^", SdpAiSql, "orders_knowledge"), "quoted schema name completion");
    expect(candidate("CREATE AI CONVERSATION c USING MODEL sa^", SdpAiSql, "sales_model"), "conversation model name completion");
    expect(candidate("START AI CONVERSATION as^", SdpAiSql, "assistant_session"), "conversation name completion");
    expect(!candidate("PREPARE AC^", SdpAiSql, "ACTION"),"core-only AI suggested Action preparation");
    expect(candidate("PREPARE AC^", SdpAiSql|SdpAiActions, "ACTION"),"Action capability preparation completion");
    expect(candidate("PREPARE ACTION ^", SdpAiSql|SdpAiActions, "CALL"),"typed Action CALL completion");
    expect(candidate("PREPARE ACTION ^", SdpAiSql|SdpAiActions, "FOR TOOL CALL"),"Action tool preparation completion");
    expect(candidate("PREPARE ACTION FOR TO^", SdpAiSql|SdpAiActions, "TOOL CALL"),"Action tool preparation token completion");
    expect(candidate("PREPARE ACTION FOR TOOL CALL @tool_id ^", SdpAiSql|SdpAiActions, "RETURNING ID"),"Action tool ID result completion");
    expect(!candidate("PREPARE ACTION FOR TOOL CALL ^", SdpAiSql|SdpAiActions, "RETURNING ID"),"Action result before tool ID expression");
    expect(candidate("PREPARE ACTION CALL p(1) ^", SdpAiSql|SdpAiActions, "RETURNING ID"),"Action ID result completion");
    expect(!candidate("PREPARE ACTION CALL p(1,^", SdpAiSql|SdpAiActions, "RETURNING ID"),"Action completion before call arguments closed");
    expect(candidate("EXECUTE AC^", SdpAiSql|SdpAiActions, "ACTION"),"Action execution completion");
    expect(!candidate("EXECUTE AC^", SdpAiSql, "ACTION"),"core-only AI suggested Action execution");
    expect(candidate("DESCRIBE AC^", SdpAiSql|SdpAiActions, "ACTION"),"Action description completion");
    expect(!candidate("DESCRIBE AC^", SdpAiSql, "ACTION"),"Action description leaked to core-only profile");
    expect(candidate("EXECUTE AI TO^", SdpAiSql|SdpAiActions, "TOOL CALL"),"verified native tool execution completion");
    expect(!candidate("EXECUTE AI TO^", SdpAiSql, "TOOL CALL"),"full-profile tool suggestion leaked to core-only AI");
    expect(!candidate("EXECUTE AI TO^", 0, "TOOL CALL"),"tool execution completion leaked without AI capability");
    expect(candidate("ALTER AI CONVERSATION c WITH ^",SdpAiSql,"FUNCTIONS"),"conversation explicit tool list completion");
    expect(candidate("CREATE AI CONVERSATION c USING MODEL m WITH FUNCTIONS (^",SdpAiSql|SdpAiActions,"KNOWLEDGE"),"Knowledge tool kind completion");
    expect(candidate("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE k,^",SdpAiSql|SdpAiActions,"PROCEDURE"),"subsequent typed tool kind completion");
    expect(candidate("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE ord^",SdpAiSql|SdpAiActions,"orders_knowledge"),"tool list ACL-visible Knowledge name completion");
    expect(candidate("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE test.^",SdpAiSql|SdpAiActions,"orders_knowledge"),"qualified tool list Knowledge metadata completion");
    expect(!candidate("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE ord^",SdpAiSql,"orders_knowledge"),"full-profile tool metadata leaked to core-only AI");
    expect(!candidate("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE ord^",0,"orders_knowledge"),"tool list metadata leaked without AI capability");
    expect(!candidate("SELECT functions( ^",SdpAiSql,"KNOWLEDGE"),"ordinary function became a conversation tool list");
    expect(candidate("REQUIRE AP^", SdpAiSql|SdpAiActions, "APPROVAL"),"approval requirement completion");
    expect(!candidate("REQUIRE AP^", SdpAiSql, "APPROVAL"),"core-only AI suggested approval policy");
    expect(candidate("GRANT US^", SdpAiSql|SdpAiRoleAcl, "USE"),"custom role USE completion");
    expect(candidate("GRANT AP^", SdpAiSql|SdpAiRoleAcl|SdpAiActions, "APPROVE"),"custom role approval privilege completion");
    expect(!candidate("GRANT AP^", SdpAiSql|SdpAiRoleAcl, "APPROVE"),"role-only profile suggested unsupported approval grant");
    expect(!candidate("GRANT US^", SdpAiSql, "USE"),"core-only AI suggested custom role USE grant");
    expect(candidate("GRANT EXECUTE ON KN^", SdpAiSql|SdpAiRoleAcl, "KNOWLEDGE"),"custom Knowledge grant target completion");
    expect(candidate("GRANT EXECUTE ON KNOWLEDGE ord^", SdpAiSql|SdpAiRoleAcl, "orders_knowledge"),"ACL visible grant target metadata completion");
    expect(!candidate("GRANT EXECUTE ON KNOWLEDGE ord^", SdpAiSql, "orders_knowledge"),"custom grant target leaked to core-only connection");
    expect(candidate("GRANT EXECUTE ON FUNCTION f TO RO^", SdpAiSql|SdpAiRoleAcl, "ROLE"),"explicit role recipient completion");
    expect(authoritative("PREPARE ACTION CALL p(1) RETURNING ID INTO @id", SdpAiSql|SdpAiActions),"Action validation dispatch");
    expect(!authoritative("PREPARE ACTION CALL p(1) RETURNING ID INTO @id", SdpAiSql),"Action validation bypass leaked to core-only connection");
    expect(authoritative("GRANT USE ON MODEL m TO ROLE r", SdpAiSql|SdpAiRoleAcl),"custom role grant validation dispatch");
    expect(!authoritative("GRANT USE ON MODEL m TO ROLE r", SdpAiSql),"custom role validation bypass leaked to core-only connection");
    expect(!authoritative("EXECUTE action",SdpAiSql|SdpAiActions),"ordinary prepared statement named action changed validation");
    expect(authoritative("DESCRIBE ACTION @id",SdpAiSql|SdpAiActions),"Action user variable description validation");
    expect(authoritative("DESCRIBE ACTION '0123456789abcdef'",SdpAiSql|SdpAiActions),"Action literal description validation");
    expect(!authoritative("DESCRIBE ACTION @id",SdpAiSql),"Action description bypass leaked to core-only profile");
    expect(!authoritative("DESCRIBE action id",SdpAiSql|SdpAiActions),"ordinary table/column description changed validation");
    expect(authoritative("EXECUTE AI TOOL CALL @id",SdpAiSql|SdpAiActions),"native tool execution validation");
    expect(!authoritative("EXECUTE AI TOOL CALL @id",SdpAiSql),"full-profile tool validation leaked to core-only AI");
    expect(authoritative("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE k)",SdpAiSql|SdpAiActions),"typed tool list validation");
    expect(!authoritative("ALTER AI CONVERSATION c WITH FUNCTIONS (KNOWLEDGE k)",SdpAiSql),"typed tool list bypass leaked to core-only AI");
    expect(authoritative("CREATE AI CONVERSATION c USING MODEL m WITH FUNCTIONS",SdpAiSql),"bare core WITH FUNCTIONS inference option regressed");
    expect(!authoritative("EXECUTE AI TOOL CALL @id",0),"tool execution validation leaked without AI capability");
    expect(!authoritative("GRANT SELECT ON model TO role",SdpAiSql|SdpAiRoleAcl),"ordinary model table or role account changed validation");
    expect(!candidate("CREATE TABLE kn^", SdpAiSql, "KNOWLEDGE"), "AI keyword leaked into object identifier");
    expect(!candidate("HIST ^", SdpSqlShort, "DIFF"), "HIST family leaked under SQL-only capability");
    expect(!candidate("HIST ^", 7, "REMOTE"), "deferred HIST leaked with older capability flags");
    expect(!authoritative("HIST STATUS", 7), "deferred HIST bypassed ordinary validation");
    expect(!candidate("SET ENQUEUE JOB ^", 7, "BACKOFF"), "deferred SQL JOB completion leaked");
    {
      ANTLRInputStream input("HIST HISTORY HISTOGRAM");MySQLLexer lexer(&input);lexer.serverVersion=261000;
      CommonTokenStream stream(&lexer);stream.fill();std::vector<size_t> types;
      for (auto *token:stream.getTokens()) if (token->getChannel()==Token::DEFAULT_CHANNEL && token->getType()!=Token::EOF) types.push_back(token->getType());
      expect(types==std::vector<size_t>{MySQLLexer::IDENTIFIER,MySQLLexer::HISTORY_SYMBOL,MySQLLexer::HISTOGRAM_SYMBOL},"baseline HISTORY/HISTOGRAM or ordinary HIST identifier changed");
    }
    expect(candidate("APPEND RESPONSE AS ^", SdpAiSql, "JSON"), "AI response format completion");
    expect(candidate("APPEND RESPONSE AS JSON ^", SdpAiSql, "INTO"), "AI format-first capture completion");
    expect(candidate("APPEND RESPONSE INTO @answer AS JSON RETURNING ^", SdpAiSql, "METADATA"), "AI capture metadata completion");
    expect(candidate("CREATE MODEL m TYPE GENERATIVE PROVIDER ^", SdpAiSql, "LOCAL_GGUF"), "real local model provider completion");
    expect(!candidate("SELECT 'unterminated ^", SdpAiSql, "CREATE KNOWLEDGE"), "unterminated string completion");
    expect(!candidate("SELECT * ^ FROM film", 0, "EXCLUDE"), "EXCLUDE leaked without capability");
    expect(candidate("SELECT * EX^ FROM film", SdpSqlShort, "EXCLUDE"), "star modifier completion");
    expect(candidate("SELECT * EXCLUDE (ti^) FROM film", SdpSqlShort, "title"), "native source-column cache completion");
    expect(candidate("FROM film |> EX^", SdpSqlShort, "EXTEND"), "pipeline stage completion");
    expect(candidate("SELECT title FROM film ^", SdpSqlShort, "QUALIFY"), "QUALIFY expression boundary");
    expect(!candidate("SELECT title FROM film WHERE ^", SdpSqlShort, "QUALIFY"), "QUALIFY inside unfinished expression");
    expect(candidate("INSERT INTO film BY ^", SdpSqlShort, "NAME"), "INSERT BY NAME completion");
    expect(candidate("CREATE PROCEDURE p() AS LE^", SdpSqlShort, "LET"), "short routine completion");
    expect(candidate("CREATE PROCEDURE p() AS WHEN n>0 ^", SdpSqlShort, "DO"), "WHEN expression completion");
    expect(candidate("CREATE PROCEDURE p() AS FOR r ^", SdpSqlShort, "IN"), "FOR row completion");
    expect(candidate("CREATE PROCEDURE p() AS LET şehir='x';\nWH^", SdpSqlShort, "WHEN"), "multiline UTF8 routine completion");
    expect(candidate("CREATE PROCEDURE p(IN requested_id BIGINT) AS LET answer JSON=NULL;SELECT :an^",SdpSqlShort,"answer"),"typed local capture completion");
    expect(candidate("CREATE PROCEDURE p(IN requested_id BIGINT) AS SELECT :req^",SdpSqlShort,"requested_id"),"native routine parameter completion");
    expect(!candidate("CREATE PROCEDURE p() AS LET answer JSON=NULL;SELECT :an^",0,"answer"),"local symbols leaked without capability");
    expect(!candidate("CREATE PROCEDURE p() AS BEGIN LET hidden INT=1;END;SELECT :hi^",SdpSqlShort,"hidden"),"closed local scope leaked");
    expect(candidate("CREATE PROCEDURE p(IN outer_value INT) AS BEGIN LET hidden INT=1;END;SELECT :out^",SdpSqlShort,"outer_value"),"nested BEGIN removed routine parameters");
    expect(candidate("CREATE PROCEDURE p() AS LET answer JSON=NULL;BEGIN LET answer TEXT='x';SELECT :an^",SdpSqlShort,"answer"),"inner local shadow completion");
    expect(localHover("CREATE PROCEDURE p() AS LET answer JSON=NULL;BEGIN LET answer TEXT='x';SELECT :an^swer") == ":answer\nTür: TEXT","inner declaration type hover");
    expect(localHover("CREATE PROCEDURE p(IN amount DECIMAL(10,2)) AS SELECT :am^ount") == ":amount\nTür: DECIMAL(10,2)","parameter precision/scale hover");
    expect(localHover("CREATE PROCEDURE p() AS LET şehir VARCHAR(40)='x';SELECT :şe^hir") == ":şehir\nTür: VARCHAR(40)","UTF8 local hover position");
    expect(candidate("CREATE PROCEDURE p() AS FOR r IN SELECT title,description AS detail FROM film DO SELECT :r.de^",SdpSqlShort,"detail"),"FOR row projected alias completion");
    expect(candidate("CREATE PROCEDURE p() AS FOR r IN SELECT * FROM film DO SELECT :r.ti^",SdpSqlShort,"title"),"FOR star uses actual cached source fields");
    expect(candidate("CREATE PROCEDURE p() AS FOR r IN SELECT title AS first_name FROM film UNION ALL SELECT title AS second_name FROM film DO SELECT :r.fi^",SdpSqlShort,"first_name"),"FOR UNION preserves first operand output names");
    expect(!candidate("CREATE PROCEDURE p() AS FOR r IN SELECT title AS first_name FROM film UNION ALL SELECT title AS second_name FROM film DO SELECT :r.se^",SdpSqlShort,"second_name"),"FOR UNION exposed a later operand alias");
    expect(candidate("CREATE PROCEDURE p() AS FOR r IN SELECT 1 AS first_name UNION ALL SELECT 2 AS second_name DO SELECT :r.fi^",SdpSqlShort,"first_name"),"FOR table-free UNION first output name missing");
    expect(!candidate("CREATE PROCEDURE p() AS FOR r IN WITH film AS (SELECT 1 AS different) SELECT * FROM film DO SELECT :r.ti^",SdpSqlShort,"title"),"FOR CTE shadow borrowed physical table columns");
    expect(candidate("CREATE PROCEDURE p() AS FOR r IN WITH c AS (SELECT title FROM film) SELECT 1 AS actual_name FROM c DO SELECT :r.ac^",SdpSqlShort,"actual_name"),"FOR CTE outer explicit projection names missing");
    expect(!candidate("CREATE PROCEDURE p() AS FOR r IN FROM film |> SELECT 1 AS only_value DO SELECT :r.ti^",SdpSqlShort,"title"),"FOR pipeline borrowed removed input fields");
    expect(!candidate("CREATE PROCEDURE p() AS FOR r IN SELECT * FROM (SELECT 1 AS different FROM film) d DO SELECT :r.ti^",SdpSqlShort,"title"),"FOR derived source borrowed inner table columns");
    expect(localHover("CREATE PROCEDURE p() AS FOR r IN SELECT * FROM film DO SELECT :r.ti^tle") == ":r.title\nFOR kayıt alanı","FOR row actual metadata hover");
    expect(!candidate("CREATE PROCEDURE p() AS FOR r IN SELECT title FROM film DO SELECT 1;END FOR;SELECT :r^",SdpSqlShort,"r"),"completed FOR row leaked");
    expect(!candidate("CREATE PROCEDURE p() AS SELECT title AS let FROM film;SELECT :FR^",SdpSqlShort,"FROM"),"SELECT alias became a false local declaration");
    expect(!candidate("CREATE PROCEDURE p() AS /* LET hidden INT=1; */ SELECT :hi^",SdpSqlShort,"hidden"),"comment became local declaration");
    expect(localHover("CREATE PROCEDURE p() AS LET answer JSON=NULL;SELECT ':an^swer'").empty(),"local hover inside string");
    expect(localHover("CREATE PROCEDURE p() AS LET answer JSON=NULL;SELECT :an^swer",0).empty(),"local hover leaked without capability");
    expect(candidate("CREATE PROCEDURE p() AS LET answer INT=1;SELECT CASE WHEN 1 THEN 2 END;SELECT :an^",SdpSqlShort,"answer"),"SQL CASE END closed local scope");
    const std::string signature="test.orders_knowledge(customer BIGINT UNSIGNED)\nRETURNS TABLE (order_id BIGINT, total DECIMAL(18,2))";
    expect(localHover("EXECUTE KNOWLEDGE orders_kno^wledge(1)",SdpAiSql)==signature,"actual Knowledge signature hover");
    expect(localHover("APPEND CONTEXT FROM KNOWLEDGE test.orders_kno^wledge(1)",SdpAiSql)==signature,"qualified Knowledge context signature");
    expect(knowledgeCalltip("EXECUTE KNOWLEDGE orders_knowledge(^")==signature,"Knowledge open-call signature");
    expect(knowledgeCalltip("EXECUTE KNOWLEDGE orders_knowledge(JSON_OBJECT('n',1),^")==signature,"Knowledge nested expression signature");
    expect(knowledgeCalltip("EXECUTE KNOWLEDGE orders_knowledge(1)^").empty(),"closed Knowledge call signature persisted");
    expect(knowledgeCalltip("EXECUTE KNOWLEDGE orders_knowledge(^",0).empty(),"Knowledge calltip leaked without capability");
    expect(localHover("SELECT knowledge.orders_kno^wledge",SdpAiSql).empty(),"ordinary qualified column became Knowledge call");
    expect(candidate("CREATE PROCEDURE p() AS FOR r IN SELECT * FROM KNOWLEDGE test.orders_knowledge(1) DO SELECT :r.to^",SdpSqlShort|SdpAiSql,"total"),"typed Knowledge FOR star metadata completion");
    expect(localHover("CREATE PROCEDURE p() AS FOR r IN SELECT * FROM KNOWLEDGE test.orders_knowledge(1) DO SELECT :r.to^tal",SdpSqlShort|SdpAiSql)==":r.total\nTür: DECIMAL(18,2)","typed Knowledge row field hover");
    expect(!candidate("CREATE PROCEDURE p() AS FOR r IN SELECT * FROM KNOWLEDGE test.orders_knowledge(1) DO SELECT :r.to^",SdpSqlShort,"total"),"Knowledge metadata leaked with AI capability disabled");
    SdpCatalogSymbol invalid("invalid","knowledge");
    expect(!sdpLoadKnowledgeMetadata(invalid,R"({"parameters":[{"name":"p","type":42}],"result_columns":[]})"),"invalid metadata type accepted");
    expect(!authoritative("HIST STATUS", 0), "native validation without capability");
    expect(authoritative("SELECT * REPLACE(title AS title) FROM film", SdpSqlShort), "REPLACE validation dispatch");
    expect(authoritative("FROM film |> SELECT title", SdpSqlShort), "pipeline validation dispatch");
    expect(authoritative("SELECT ROW_NUMBER() OVER() AS n QUALIFY n=1", SdpSqlShort), "QUALIFY validation dispatch");
    expect(authoritative("SELECT * FROM film FULL OUTER JOIN other ON film.id=other.id",SdpSqlShort),"native FULL OUTER validation dispatch");
    expect(authoritative("SELECT * FROM film FULL JOIN other ON film.id=other.id",SdpSqlShort),"native FULL JOIN validation dispatch");
    expect(!authoritative("SELECT * FROM film FULL JOIN other ON film.id=other.id",0),"FULL JOIN bypass leaked without SQL capability");
    expect(!authoritative("SELECT full FRM film",SdpSqlShort),"ordinary FULL identifier hid a syntax error");
    expect(!authoritative("SELECT exclude FRM film", SdpSqlShort), "ordinary EXCLUDE identifier hid a syntax error");
    expect(!authoritative("SELECT qualify FRM film", SdpSqlShort), "ordinary QUALIFY identifier hid a syntax error");
    expect(!authoritative("SELECT title FROM film AS qualify WHERE", SdpSqlShort), "ordinary QUALIFY alias hid a syntax error");
    expect(!authoritative("CREATE PROCEDURE p() BEGIN SELECT title AS label FROM film WHERE;END", SdpSqlShort), "ordinary long routine alias hid a syntax error");
    expect(authoritative("EXECUTE KNOWLEDGE test.k(1)", SdpAiSql), "Knowledge execute validation dispatch");
    expect(!authoritative("SELECT 'HIST QUALIFY EXCLUDE'", 7), "quoted words changed validation family");
    expect(!authoritative("SELECT * EXCLUDE ('unterminated", 7), "lexical quote error hidden");
    expect(!authoritative("SELECT * EXCLUDE(description) FROM film /* unfinished", 7), "unfinished comment error hidden");
    const std::vector<std::string> keywords = {
#define SDP_WORD(text) #text,
      SDP_LANGUAGE_KEYWORDS(SDP_WORD)
#undef SDP_WORD
    };
    expect(std::find(keywords.begin(), keywords.end(), "KNOWLEDGE") != keywords.end(), "AI syntax color set");
    expect(std::find(keywords.begin(), keywords.end(), "HIST") == keywords.end(), "deferred HIST syntax color removed");
    expect(std::find(keywords.begin(), keywords.end(), "COMPACT") == keywords.end(), "invented COMPACT keyword");
    std::cout << "PASS actual editor lexer/completion: " << checks << " capability/context/column/quote/UTF8/color checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
