#pragma once
#include <limits>
#include "antlr4-runtime.h"
#include "mysql/MySQLLexer.h"
#include "sdp-language-catalog.h"

namespace parsers {
inline SdpCompletionContext sdpEditorTokens(antlr4::BufferedTokenStream *stream, size_t line, size_t column) {
  SdpCompletionContext context;
  stream->fill();
  size_t covered = 0;
  size_t previousStop = std::numeric_limits<size_t>::max();
  for (antlr4::Token *token : stream->getTokens()) {
    if (token->getLine() > line || (token->getLine() == line && token->getCharPositionInLine() >= column)) break;
    auto *input = stream->getTokenSource()->getInputStream();
    const auto start = std::min(token->getStartIndex(), input->size());
    if (start > covered) {
      auto gap = input->getText(antlr4::misc::Interval(covered, start - 1));
      if (gap.find_first_of("'\"`") != std::string::npos) context.blocked = true;
    }
    if (token->getType() == antlr4::Token::EOF) break;
    covered = token->getStopIndex() + 1;
    const auto text = token->getText();
    // A closed block comment is one hidden token. The legacy lexer emits two
    // ordinary operator tokens for an unclosed opening, which remains an error.
    if (text == "*" && !context.tokens.empty() && context.tokens.back() == "/" &&
        token->getStartIndex() == previousStop + 1) context.blocked = true;
    size_t endLine = token->getLine(), endColumn = token->getCharPositionInLine();
    for (unsigned char c : text) {
      if (c == '\n') { ++endLine; endColumn = 0; }
      else if ((c & 0xc0) != 0x80) ++endColumn;
    }
    const bool beforeEnd = line < endLine || (line == endLine && column < endColumn);
    const bool atEnd = line == endLine && column == endColumn;
    if (token->getChannel() != antlr4::Token::DEFAULT_CHANNEL) {
      if (token->getType() != MySQLLexer::WHITESPACE && (beforeEnd ||
          (atEnd && (token->getType() == MySQLLexer::POUND_COMMENT || token->getType() == MySQLLexer::DASHDASH_COMMENT))))
        context.blocked = true;
      continue;
    }
    const bool identifier = token->getType() == MySQLLexer::BACK_TICK_QUOTED_ID;
    const bool quoted = !identifier && text.find_first_of("'\"`") != std::string::npos;
    if (quoted) {
      if (beforeEnd) context.blocked = true;
      context.tokens.push_back("?");
    } else if (identifier) {
      // Quoted identifiers remain distinct from keyword and string tokens.
      if (beforeEnd) context.blocked = true;
      context.tokens.push_back(text);
    } else if ((beforeEnd || atEnd) && token->getLine() == line && !text.empty() &&
               (std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_')) {
      size_t characters = column - token->getCharPositionInLine(), bytes = 0;
      while (bytes < text.size() && characters != 0) {
        ++bytes;
        while (bytes < text.size() && (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) ++bytes;
        --characters;
      }
      context.prefix = text.substr(0, bytes);
    } else if (text == ">" && !context.tokens.empty() && context.tokens.back() == "|" &&
               token->getStartIndex() == previousStop + 1) {
      context.tokens.back() = "|>";
    } else context.tokens.push_back(text);
    previousStop = token->getStopIndex();
  }
  if (auto *lexer = dynamic_cast<MySQLBaseLexer *>(stream->getTokenSource())) {
    if (lexer->inVersionComment) context.blocked = true;
  }
  bool create = false;
  for (const auto &word : context.tokens) {
    const auto upper = sdpUpper(word);
    if (upper == "CREATE") create = true;
    if (create && (upper == "PROCEDURE" || upper == "FUNCTION" || upper == "TRIGGER")) context.procedural = true;
  }
  return context;
}

inline bool sdpUsesServerValidation(const SdpCompletionContext &context, unsigned features) {
  if (context.blocked || context.tokens.empty()) return false;
  auto tokens = context.tokens;
  for (auto &word : tokens) word = sdpUpper(word);
  auto has = [&](const char *word) { return std::find(tokens.begin(), tokens.end(), word) != tokens.end(); };
  const auto &first = tokens.front();
  const auto second = tokens.size() > 1 ? tokens[1] : "";
  if (features & SdpSqlShort) {
    if (first == "FROM" || has("|>") || (first == "SHOW" && second == "EXPANDED")) return true;
    // The bundled MySQL grammar has a FULL token but no FULL JOIN planner.
    // Send this verified native relational form to the server for validation.
    if (first=="SELECT" || first=="WITH" || first=="EXPLAIN") {
      for (size_t i=1;i+1<tokens.size();++i) {
        if (tokens[i]!="FULL") continue;
        size_t join=i+1;
        if (tokens[join]=="OUTER") ++join;
        if (join<tokens.size() && tokens[join]=="JOIN") return true;
      }
    }
    for (size_t i=0;i<tokens.size();++i) {
      const auto &word=tokens[i];
      if ((word == "EXCLUDE" || word == "REPLACE") && i>=2 && tokens[i-1] == "*" &&
          (tokens[i-2] == "SELECT" || tokens[i-2] == "DISTINCT" || tokens[i-2] == "ALL" ||
           tokens[i-2] == "," || tokens[i-2] == ".")) return true;
      if (first == "INSERT" && word == "BY" && i+1<tokens.size() && tokens[i+1] == "NAME") return true;
      if (word == "QUALIFY" && i>0 && i+1<tokens.size() && has("SELECT")) {
        const std::vector<std::string> operandPositions={"SELECT","FROM","JOIN","WHERE","AND","OR","AS","BY","ON","(",",",".","+","-","*","/","=","<",">"};
        const std::vector<std::string> aliasFollowers={"FROM","WHERE","GROUP","ORDER","LIMIT","JOIN","LEFT","RIGHT","INNER","ON",";"};
        if (std::find(operandPositions.begin(),operandPositions.end(),tokens[i-1]) == operandPositions.end() &&
            std::find(aliasFollowers.begin(),aliasFollowers.end(),tokens[i+1]) == aliasFollowers.end()) return true;
      }
      if (context.procedural) {
        if (word == ":") return true;
        if (word == "AS" && std::find(tokens.begin(),tokens.begin()+i,"BEGIN") == tokens.begin()+i) return true;
        if (word == "LET" && i>0 && (tokens[i-1] == ";" || tokens[i-1] == "BEGIN" || tokens[i-1] == "AS" || tokens[i-1] == "DO")) return true;
        if (word == "FOR" && i+2<tokens.size() && tokens[i+2] == "IN") return true;
        if (word == "WHEN") {
          for (size_t j=i+1;j<tokens.size() && tokens[j] != "THEN" && tokens[j] != ";";++j)
            if (tokens[j] == "DO") return true;
        }
      }
    }
  }
  if (!(features & SdpAiSql)) return false;
  // The released tool profile is conservatively available with Actions.
  // A bare WITH FUNCTIONS option remains a core conversation option.
  if (!(features&SdpAiActions) && sdpConversationToolList(tokens,false)) return false;
  if ((features&SdpAiActions) &&
      ((first == "PREPARE" && second == "ACTION") ||
       ((first == "APPROVE" || first == "DENY") && second == "ACTION") ||
       (first == "EXECUTE" && second == "ACTION" && tokens.size()>2) ||
       (first == "DESCRIBE" && second == "ACTION" && tokens.size()>2 &&
        (tokens[2]=="?" || tokens[2]=="@" ||
         (tokens[2].size()>1 && tokens[2][0]=='@' && tokens[2][1]!='@'))) ||
       (first == "REQUIRE" && second == "APPROVAL"))) return true;
  if ((features&SdpAiRoleAcl) && (first == "GRANT" || first == "REVOKE")) {
    for (size_t i=1;i+2<tokens.size();++i) {
      if (tokens[i] == "ON" && (tokens[i+1] == "KNOWLEDGE" || tokens[i+1] == "MODEL" ||
          tokens[i+1] == "BEHAVIOR" || tokens[i+1] == "CONVERSATION" || tokens[i+1] == "RESPONSE") &&
          tokens[i+2] != "." && tokens[i+2] != "TO" && tokens[i+2] != "FROM") return true;
      if ((tokens[i] == "TO" || tokens[i] == "FROM") && tokens[i+1] == "ROLE" && tokens[i+2] != ";") return true;
      if (tokens[i] == "ON" && tokens[i+1] == "PROCEDURE" && has("APPROVE") && (features&SdpAiActions)) return true;
    }
  }
  if ((first == "CREATE" || first == "ALTER" || first == "DROP" || first == "DESCRIBE") &&
      (second == "KNOWLEDGE" || second == "BEHAVIOR" || second == "RESPONSE" || second == "CONVERSATION" ||
       second == "AI" || (second == "MODEL" && (first != "CREATE" || has("GENERATIVE"))))) return true;
  if (first == "SHOW" && (second == "KNOWLEDGES" || second == "BEHAVIORS" || second == "CONVERSATIONS" ||
      second == "MODELS" || second == "RESPONSE" || second == "MEMORY")) return true;
  return (first == "EXECUTE" && second == "KNOWLEDGE") ||
      ((features&SdpAiActions) && first == "EXECUTE" && second == "AI" && tokens.size()>3 && tokens[2]=="TOOL" && tokens[3]=="CALL") ||
      (first == "START" && second == "AI") || first == "APPEND" || first == "FINISH" ||
      (first == "FETCH" && second == "RESPONSE") || (first == "CANCEL" && second == "RUN") ||
      (first == "COMMIT" && (second == "ASYNC" || second == "WITH")) ||
      (first == "IMPORT" && second == "MODEL") || (first == "FLUSH" && second == "MEMORY");
}
}  // namespace parsers
