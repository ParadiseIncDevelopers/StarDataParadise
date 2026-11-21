// Lexical statement boundaries for the native AS ... END routine form.
#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace parsers {
class SdpRoutineBoundary {
  enum Block { Root, Begin, If, For, While, Loop, Repeat, CaseStatement, CaseExpression };
  std::vector<Block> blocks;
  bool create = false, routine = false, pendingAs = false, statementStart = true;
  bool endSuffix = false, waitingDo = false, waitingThen = false, label = false;
  size_t parentheses = 0;

  static bool identifier(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '$' || c >= 0x80;
  }
  void startBody() { blocks.push_back(Root); pendingAs = false; statementStart = true; }
  void word(const std::string &text) {
    if (pendingAs) {
      pendingAs = false;
      if (text == "RETURN") { routine = false; return; }
      startBody();
    }
    if (blocks.empty()) {
      if (text == "CREATE") create = true;
      else if (create && (text == "PROCEDURE" || text == "FUNCTION")) routine = true;
      else if (routine && parentheses == 0 && text == "AS") pendingAs = true;
      return;
    }
    if (endSuffix) {
      endSuffix = false;
      if (text == "IF" || text == "FOR" || text == "WHILE" || text == "LOOP" ||
          text == "REPEAT" || text == "CASE") return;
    }
    const bool beginning = statementStart;
    label = beginning;
    statementStart = false;
    if (text == "END") { blocks.pop_back(); endSuffix = true; return; }
    if (text == "CASE") { blocks.push_back(beginning ? CaseStatement : CaseExpression); return; }
    if (text == "BEGIN" && beginning) { blocks.push_back(Begin); statementStart = true; return; }
    if (text == "IF" && beginning) { blocks.push_back(If); waitingThen = true; return; }
    if (text == "FOR" && beginning) { blocks.push_back(For); waitingDo = true; return; }
    if (text == "WHILE" && beginning) { blocks.push_back(While); waitingDo = true; return; }
    if (text == "LOOP" && beginning) { blocks.push_back(Loop); statementStart = true; return; }
    if (text == "REPEAT" && beginning) { blocks.push_back(Repeat); statementStart = true; return; }
    if (text == "WHEN" && beginning) { waitingDo = true; return; }
    if (text == "DO" && waitingDo && parentheses == 0) { waitingDo = false; statementStart = true; return; }
    if (text == "THEN" && waitingThen && blocks.back() != CaseExpression && parentheses == 0) {
      waitingThen = false; statementStart = true; return;
    }
    if (text == "ELSEIF" && blocks.back() == If) { waitingThen = true; return; }
    if ((text == "ELSE" || text == "THEN") &&
        (blocks.back() == If || blocks.back() == CaseStatement) && parentheses == 0)
      statementStart = true;
  }

public:
  bool protectsDelimiter() const { return !blocks.empty() || pendingAs; }
  void reset() { *this = SdpRoutineBoundary(); }

  // Called only by the existing scanner outside its string/comment loops.
  // It observes source bytes and never rewrites a query or its positions.
  void observe(const unsigned char *at, const unsigned char *start, const unsigned char *end) {
    const unsigned char c = *at;
    if (c == '(') { ++parentheses; return; }
    if (c == ')') { if (parentheses != 0) --parentheses; return; }
    if (c == ';') { statementStart = true; endSuffix = false; label = false; return; }
    if (c == ':' && label && at + 1 < end && at[1] != '=') { statementStart = true; label = false; return; }
    if ((c == '\'' || c == '"') && pendingAs) { pendingAs = false; routine = false; return; }
    if (c == '`' && pendingAs) { startBody(); label = true; return; }
    if (!(std::isalpha(c) || c == '_') || (at != start && identifier(at[-1]))) return;
    const unsigned char *last = at;
    while (last < end && identifier(*last)) ++last;
    std::string text(reinterpret_cast<const char *>(at), last - at);
    for (char &value : text) value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    word(text);
  }
};
}  // namespace parsers
