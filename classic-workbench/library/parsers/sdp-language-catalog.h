#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace parsers {
enum SdpLanguageFeature : unsigned {
  SdpAiSql = 1, SdpSqlShort = 2,
  // Bit 4 belonged to the deferred HIST family and remains unused.
  SdpAiActions = 8, SdpAiRoleAcl = 16
};

inline std::string sdpUpper(std::string text) {
  for (char &c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

struct SdpCompletionContext {
  std::vector<std::string> tokens; // Real lexer tokens, excluding strings/comments.
  std::string prefix;
  bool blocked = false;
  bool procedural = false;
};

struct SdpCatalogRequest {
  std::string kind;
  std::string schema;
};

inline bool sdpConversationToolList(const std::vector<std::string> &tokens, bool onlyOpen=true) {
  if (tokens.size()<6 || (tokens[0]!="CREATE" && tokens[0]!="ALTER") ||
      tokens[1]!="AI" || tokens[2]!="CONVERSATION") return false;
  for (size_t i=4;i+2<tokens.size();++i) {
    if (tokens[i]!="WITH" || tokens[i+1]!="FUNCTIONS" || tokens[i+2]!="(") continue;
    int depth=0;
    for (size_t j=i+2;j<tokens.size();++j) {
      if (tokens[j]=="(") ++depth;
      if (tokens[j]==")") --depth;
    }
    return !onlyOpen || depth>0;
  }
  return false;
}

inline SdpCatalogRequest sdpCatalogRequest(const SdpCompletionContext &context, unsigned features) {
  SdpCatalogRequest result;
  if (context.blocked || context.tokens.empty()) return result;
  auto tokens = context.tokens;
  const auto separator = std::find(tokens.rbegin(), tokens.rend(), ";");
  if (separator != tokens.rend()) tokens.erase(tokens.begin(), separator.base());
  if (tokens.size() >= 2 && tokens.back() == ".") {
    result.schema = tokens[tokens.size()-2];
    if (result.schema.size() >= 2 && result.schema.front() == '`' && result.schema.back() == '`') {
      result.schema = result.schema.substr(1, result.schema.size()-2);
      for (size_t at=0; (at=result.schema.find("``",at)) != std::string::npos; ++at) result.schema.erase(at,1);
    }
    tokens.resize(tokens.size()-2);
  }
  if (tokens.empty()) return {};
  for (auto &word : tokens) word = sdpUpper(word);
  const auto &first = tokens.front();
  const auto &last = tokens.back();
  if (!(features & SdpAiSql)) return result;
  if ((features&SdpAiActions) && last=="KNOWLEDGE" && sdpConversationToolList(tokens)) result.kind="knowledge";
  if ((features&SdpAiRoleAcl) && (first == "GRANT" || first == "REVOKE") &&
      std::find(tokens.begin(),tokens.end(),"ON") != tokens.end()) {
    if (last == "KNOWLEDGE") result.kind="knowledge";
    if (last == "MODEL") result.kind="model";
    if (last == "BEHAVIOR") result.kind="behavior";
    if (last == "CONVERSATION") result.kind="conversation";
    if (last == "SCHEMA" && tokens.size()>1 && tokens[tokens.size()-2] == "RESPONSE") result.kind="response_schema";
  }
  if (tokens.size() == 2 && (first == "ALTER" || first == "DROP" || first == "DESCRIBE" || first == "EXECUTE")) {
    if (last == "KNOWLEDGE") result.kind = "knowledge";
    if (last == "BEHAVIOR") result.kind = "behavior";
    if (last == "MODEL") result.kind = "model";
    if (last == "CONVERSATION") result.kind = "conversation";
  }
  if (tokens.size() == 3 && (first == "DROP" || first == "DESCRIBE") && tokens[1] == "RESPONSE" &&
      (last == "SCHEMA" || last == "DATABASE")) result.kind = "response_schema";
  if (tokens.size() == 3 && first == "START" && tokens[1] == "AI" && last == "CONVERSATION") result.kind = "conversation";
  if (first == "CREATE" && last == "MODEL" && std::find(tokens.begin(), tokens.end(), "CONVERSATION") != tokens.end() &&
      std::find(tokens.begin(), tokens.end(), "USING") != tokens.end()) result.kind = "model";
  return result;
}

inline std::vector<std::string> sdpContextKeywords(const SdpCompletionContext &context, unsigned features) {
  std::vector<std::string> result;
  if (context.blocked || features == 0) return result;
  auto tokens = context.tokens;
  for (auto &token : tokens) token = sdpUpper(token);
  const auto separator = std::find(tokens.rbegin(), tokens.rend(), ";");
  if (separator != tokens.rend()) tokens.erase(tokens.begin(), separator.base());
  const std::string last = tokens.empty() ? "" : tokens.back();
  const std::string first = tokens.empty() ? "" : tokens.front();
  auto has = [&](const char *word) { return std::find(tokens.begin(), tokens.end(), word) != tokens.end(); };
  const std::vector<std::string> unfinished = {"SELECT", "FROM", "WHERE", "JOIN", "ON", "AND", "OR", "BY", "AS", "IN", "LIKE", "NOT", "=", "<", ">", "+", "-", "*", "/", ".", "(", ",", ":", "QUALIFY"};
  const bool expressionEnd = !last.empty() && std::find(unfinished.begin(), unfinished.end(), last) == unfinished.end();
  auto add = [&](std::initializer_list<const char *> words) {
    const auto prefix = sdpUpper(context.prefix);
    for (const char *word : words) if (std::string(word).compare(0, prefix.size(), prefix) == 0) result.emplace_back(word);
  };
  if ((features&SdpAiSql) && (features&SdpAiActions)) {
    if (tokens.empty()) add({"PREPARE ACTION", "APPROVE ACTION", "DENY ACTION", "EXECUTE ACTION", "DESCRIBE ACTION", "REQUIRE APPROVAL", "EXECUTE AI TOOL CALL"});
    if (first == "EXECUTE" && tokens.size()==1) add({"AI TOOL CALL"});
    if (first == "EXECUTE" && last == "AI") add({"TOOL CALL"});
    if (first == "EXECUTE" && last == "TOOL" && tokens.size()>1 && tokens[1]=="AI") add({"CALL"});
    if (first == "PREPARE" && last == "PREPARE") add({"ACTION"});
    if (first == "PREPARE" && last == "ACTION") add({"CALL", "FOR TOOL CALL"});
    if (first == "PREPARE" && last == "FOR" && has("ACTION")) add({"TOOL CALL"});
    if (first == "PREPARE" && last == "TOOL" && has("ACTION")) add({"CALL"});
    if ((first == "APPROVE" || first == "DENY" || first == "EXECUTE" || first == "DESCRIBE") && tokens.size()==1) add({"ACTION"});
    if (first == "REQUIRE" && last == "REQUIRE") add({"APPROVAL"});
    if (first == "REQUIRE" && last == "APPROVAL") add({"ON"});
    if (first == "REQUIRE" && last == "ON" && has("APPROVAL")) add({"PROCEDURE"});
    if (first == "PREPARE" && has("ACTION") && has("CALL")) {
      int depth=0;for (const auto &token:tokens) {if (token == "(") ++depth;if (token == ")") --depth;}
      if (last == ")" && depth==0) add({"RETURNING ID"});
      if (has("FOR") && has("TOOL") && expressionEnd && last!="CALL" && !has("RETURNING")) add({"RETURNING ID"});
      if (last == "RETURNING") add({"ID"});
      if (last == "ID") add({"INTO"});
    }
  }
  if ((features&SdpAiSql) && (features&SdpAiRoleAcl) && (first == "GRANT" || first == "REVOKE")) {
    if (tokens.size()==1) {
      add({"USE", "APPEND"});
      if (features&SdpAiActions) add({"APPROVE"});
    }
    if (last == "ON") add({"KNOWLEDGE", "MODEL", "BEHAVIOR", "CONVERSATION", "RESPONSE SCHEMA"});
    if (last == "RESPONSE" && has("ON")) add({"SCHEMA"});
    if ((first == "GRANT" && last == "TO") || (first == "REVOKE" && last == "FROM")) add({"ROLE"});
    if (last == "," && has("SELECT") && !has("ON")) add({"APPEND"});
  }
  if (features & SdpAiSql) {
    if (tokens.empty()) add({"CREATE KNOWLEDGE", "CREATE BEHAVIOR", "CREATE RESPONSE SCHEMA", "CREATE AI CONVERSATION",
                            "CREATE MODEL", "IMPORT MODEL", "EXECUTE KNOWLEDGE", "START AI CONVERSATION", "APPEND", "FETCH RESPONSE", "CANCEL RUN", "FINISH"});
    if (last == "CREATE" || last == "DROP" || last == "DESCRIBE")
      add({"KNOWLEDGE", "BEHAVIOR", "RESPONSE SCHEMA", "MODEL"});
    if (last == "ALTER") add({"KNOWLEDGE", "BEHAVIOR"});
    if (first == "ALTER" && tokens.size()==1) add({"AI CONVERSATION"});
    if (last == "CREATE" || last == "START") add({"AI CONVERSATION"});
    if (last == "AI") add({"CONVERSATION"});
    if (first == "SHOW" && last == "SHOW") add({"KNOWLEDGES", "BEHAVIORS", "RESPONSE SCHEMAS", "CONVERSATIONS", "MODELS", "MEMORY"});
    if (last == "RESPONSE" && (first == "CREATE" || first == "DROP" || first == "DESCRIBE")) add({"SCHEMA"});
    if (first == "SHOW" && last == "RESPONSE") add({"SCHEMAS"});
    if (last == "IMPORT") add({"MODEL"});
    if (last == "EXECUTE") add({"KNOWLEDGE"});
    if ((first=="CREATE" || first=="ALTER") && tokens.size()>3 && tokens[1]=="AI" && tokens[2]=="CONVERSATION") {
      if (last=="WITH") add({"FUNCTIONS"});
      if ((features&SdpAiActions) && (last=="(" || last==",") && sdpConversationToolList(tokens)) add({"KNOWLEDGE", "FUNCTION", "PROCEDURE"});
    }
    if (first == "IMPORT" && last == "FROM") add({"FILE"});
    if (first == "CREATE" && has("MODEL") && last == "TYPE") add({"GENERATIVE"});
    if (last == "GENERATIVE") add({"PROVIDER"});
    if (last == "PROVIDER") add({"OPENAI", "ANTHROPIC", "LOCAL_GGUF"});
    if (last == "USING" && has("CONVERSATION")) add({"MODEL"});
    if (last == "APPEND") add({"PROMPT", "CONTEXT", "RESPONSE"});
    if (first == "APPEND" && tokens.size() >= 2 && tokens[1] == "RESPONSE") {
      if (last == "RESPONSE") add({"INTO", "AS JSON", "AS TEXT", "WITH", "RETURNING METADATA INTO"});
      if (last == "AS") add({"JSON", "TEXT"});
      if (last == "JSON" || last == "TEXT") add({"INTO", "WITH", "RETURNING METADATA INTO"});
      if (last == "RETURNING") add({"METADATA"});
      if (last == "METADATA") add({"INTO"});
    }
    if (last == "FETCH") add({"RESPONSE"});
    if (first == "FETCH" && last == "FROM") add({"RUN"});
    if (last == "CANCEL") add({"RUN"});
    if (last == "COMMIT") add({"ASYNC", "WITH"});
    if (first == "COMMIT" && last == "ASYNC") add({"WITH", "RETURNING RUN INTO"});
    if (last == "FLUSH") add({"MEMORY"});
  }
  if (features & SdpSqlShort) {
    if (tokens.empty()) add({"SHOW EXPANDED", "FROM"});
    if (context.procedural && (tokens.empty() || last == "AS" || last == "BEGIN" || last == "THEN" || last == "DO"))
      add({"LET", "WHEN", "FOR"});
    if (last == "SHOW") add({"EXPANDED"});
    if (last == "EXPANDED") add({"SELECT", "FROM", "PROCEDURE", "FUNCTION"});
    if (last == "AS" && has("FUNCTION")) add({"RETURN"});
    if (last == "*" && (has("SELECT") || first == "FROM")) add({"EXCLUDE", "REPLACE"});
    if (last == ")" && has("EXCLUDE")) add({"REPLACE"});
    if (has("SELECT") && has("FROM") && expressionEnd && last != "|") add({"QUALIFY"});
    if (first == "INSERT" && tokens.size() > 1 && !has("SELECT") && !has("VALUES") && last != "INTO" &&
        last != "IGNORE" && last != "LOW_PRIORITY" && expressionEnd) add({"BY NAME"});
    if (last == "BY" && first == "INSERT") add({"NAME"});
    if (last == "|>") add({"WHERE", "SELECT", "EXTEND", "AGGREGATE", "JOIN", "LEFT JOIN", "RIGHT JOIN", "ORDER BY", "LIMIT"});
    if (context.procedural && tokens.size() > 1 && tokens[tokens.size()-2] == "FOR" &&
        last != "UPDATE" && last != "SHARE") add({"IN"});
    if (context.procedural && expressionEnd) {
      auto when = std::find(tokens.rbegin(), tokens.rend(), "WHEN");
      auto after = std::find(tokens.rbegin(), tokens.rend(), "DO");
      if (when != tokens.rend() && (after == tokens.rend() || when < after) && !has("CASE")) add({"DO"});
      if (has("FOR") && has("IN") && has("SELECT") && !has("DO")) add({"DO"});
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}
}  // namespace parsers
