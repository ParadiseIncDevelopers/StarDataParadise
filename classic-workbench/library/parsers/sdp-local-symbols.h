#pragma once
#include <map>
#include "sdp-language-catalog.h"

namespace parsers {
struct SdpLocalSymbol {
  std::string name, declaredType;
  bool row = false, star = false;
  std::vector<std::string> fields;
  std::string schema, table;
  bool knowledge = false;
};

inline std::string sdpUnquoteLocal(std::string text) {
  if (text.size() >= 2 && text.front() == '`' && text.back() == '`') {
    text = text.substr(1,text.size()-2);
    for (size_t at=0;(at=text.find("``",at)) != std::string::npos;++at) text.erase(at,1);
  }
  return text;
}

inline bool sdpLocalIdentifier(const std::string &text) {
  if (text.empty() || text == "?") return false;
  if (text.front() == '`') return text.size() >= 2 && text.back() == '`';
  const auto c=static_cast<unsigned char>(text.front());
  return std::isalpha(c) || c >= 0x80 || c == '_' || c == '$';
}

inline std::string sdpDeclaredType(const std::vector<std::string> &tokens,size_t from,size_t to) {
  std::string result;
  for (size_t i=from;i<to;++i) {
    if (!result.empty() && tokens[i] != ")" && tokens[i] != "," && tokens[i] != "(" && tokens[i-1] != "(" && tokens[i-1] != ",") result += ' ';
    result += sdpUpper(tokens[i]);
  }
  return result;
}

inline void sdpRowProjection(SdpLocalSymbol &row,const std::vector<std::string> &query) {
  // A pipeline can replace/drop/extend the input projection at each stage.
  // Its source table metadata is not its result schema; wait for server result
  // metadata instead of presenting fields that a later stage removed.
  for (const auto &token:query) if (token == "|>") return;
  size_t select=query.size(),end=query.size();
  int depth=0;
  for (size_t i=0;i<query.size();++i) {
    if (query[i] == "(") ++depth;
    if (query[i] == ")") --depth;
    // UNION/INTERSECT/EXCEPT use the first operand's output names.
    if (depth == 0 && sdpUpper(query[i]) == "SELECT" && select == query.size()) {select=i+1;end=query.size();}
    else if (depth == 0 && select < i && (sdpUpper(query[i]) == "FROM" ||
        sdpUpper(query[i]) == "UNION" || sdpUpper(query[i]) == "INTERSECT" ||
        sdpUpper(query[i]) == "EXCEPT")) end=std::min(end,i);
  }
  if (select < query.size()) {
    if (sdpUpper(query[select]) == "ALL" || sdpUpper(query[select]) == "DISTINCT") ++select;
    size_t begin=select;
    auto field=[&](size_t first,size_t last) {
      if (first == last) return;
      if (last-first == 1 && query[first] == "*") {row.star=true;return;}
      if (last-first == 1 && sdpLocalIdentifier(query[first])) row.fields.push_back(sdpUnquoteLocal(query[first]));
      else if (last-first == 3 && query[first+1] == "." && sdpLocalIdentifier(query[first+2])) row.fields.push_back(sdpUnquoteLocal(query[first+2]));
      else if (last-first >= 3 && sdpUpper(query[last-2]) == "AS" && sdpLocalIdentifier(query[last-1])) row.fields.push_back(sdpUnquoteLocal(query[last-1]));
    };
    depth=0;
    for (size_t i=select;i<end;++i) {
      if (query[i] == "(") ++depth;
      if (query[i] == ")") --depth;
      if (depth == 0 && query[i] == ",") {field(begin,i);begin=i+1;}
    }
    field(begin,end);
  } else if (!query.empty() && sdpUpper(query.front()) == "FROM") row.star=true;
  // A star is expanded only from one actual cached source. Do not guess JOIN,
  // modifier or derived schemas from an incomplete query's text.
  for (const auto &token : query) if (sdpUpper(token) == "JOIN" || sdpUpper(token) == "EXCLUDE" || sdpUpper(token) == "REPLACE") row.star=false;
  // Only the outer query's source can back a star. A CTE body/subquery may
  // mention a cached physical table with a different result projection.
  depth=0;
  for (size_t i=0;i+1<query.size();++i) {
    if (query[i] == "(") ++depth;
    if (query[i] == ")") --depth;
    if (depth != 0 || sdpUpper(query[i]) != "FROM") continue;
    if (sdpUpper(query[i+1]) == "KNOWLEDGE") {row.knowledge=true;++i;}
    if (i+1>=query.size()) break;
    if (!sdpLocalIdentifier(query[i+1])) break;
    row.table=sdpUnquoteLocal(query[i+1]);
    if (i+3<query.size() && query[i+2] == "." && sdpLocalIdentifier(query[i+3])) {
      row.schema=row.table;row.table=sdpUnquoteLocal(query[i+3]);
    }
    break;
  }
  // Unsaved CTE metadata has no trusted entry in the live table cache. A CTE
  // can intentionally shadow an existing table of the same name.
  if (!query.empty() && sdpUpper(query.front()) == "WITH") row.star=false;
}

// A lexical symbol view for an unsaved routine. Names/types come from real
// lexer tokens; star row fields are resolved later against the live symbol
// cache. Inferred expression types remain owned by the server compiler.
inline std::vector<SdpLocalSymbol> sdpVisibleLocals(const SdpCompletionContext &context) {
  if (!context.procedural || context.blocked) return {};
  const auto &t=context.tokens;
  struct Scope {std::string kind;std::vector<SdpLocalSymbol> symbols;};
  std::vector<Scope> scopes{{"routine",{}}};
  size_t body=0;
  for (size_t i=0;i+2<t.size();++i) if (sdpUpper(t[i]) == "PROCEDURE" || sdpUpper(t[i]) == "FUNCTION") {
    size_t open=i+1;
    while (open<t.size() && t[open] != "(") ++open;
    if (open == t.size()) return {};
    int depth=1;size_t start=open+1;
    for (size_t j=start;j<t.size();++j) {
      if (t[j] == "(") ++depth;
      if (t[j] == ")") --depth;
      if ((depth == 1 && t[j] == ",") || depth == 0) {
        size_t name=start;
        if (name<j && (sdpUpper(t[name]) == "IN" || sdpUpper(t[name]) == "OUT" || sdpUpper(t[name]) == "INOUT")) ++name;
        if (name+1<j && sdpLocalIdentifier(t[name])) scopes.front().symbols.push_back({sdpUnquoteLocal(t[name]),sdpDeclaredType(t,name+1,j)});
        start=j+1;
      }
      if (depth == 0) {body=j+1;break;}
    }
    break;
  }
  bool statement=true,enteredBody=false;
  for (size_t i=body;i<t.size();++i) {
    const auto word=sdpUpper(t[i]);
    if (word == "CASE") {scopes.push_back({"case",{}});continue;}
    if (word == "END") {
      const auto after=i+1<t.size()?sdpUpper(t[i+1]):"";
      if (after == "FOR") {
        while (scopes.size()>1) {const auto kind=scopes.back().kind;scopes.pop_back();if (kind == "for") break;}
        ++i;
      } else if (after == "IF" || after == "WHILE" || after == "LOOP" || after == "REPEAT") ++i;
      else if (scopes.size()>1) {
        const auto kind=scopes.back().kind;scopes.pop_back();
        if (after == "CASE") ++i;
        if (kind == "routine_begin") return {};
      } else return {};
      statement=false;continue;
    }
    if (word == "BEGIN") {scopes.push_back({enteredBody?"begin":"routine_begin",{}});enteredBody=true;statement=true;continue;}
    if (word == "AS" && !enteredBody) {enteredBody=true;statement=true;continue;}
    if (word == "THEN" || word == "DO" || word == "ELSE" || t[i] == ";") {statement=true;continue;}
    if (statement && word == "FOR" && i+2<t.size() && sdpLocalIdentifier(t[i+1]) && sdpUpper(t[i+2]) == "IN") {
      int depth=0;size_t end=i+3;
      for (;end<t.size();++end) {
        if (t[end] == "(") ++depth;
        if (t[end] == ")") --depth;
        if (depth == 0 && sdpUpper(t[end]) == "DO") break;
      }
      if (end<t.size()) {
        SdpLocalSymbol row;row.name=sdpUnquoteLocal(t[i+1]);row.row=true;row.declaredType="FOR row";
        sdpRowProjection(row,{t.begin()+i+3,t.begin()+end});
        scopes.push_back({"for",{std::move(row)}});i=end;statement=true;continue;
      }
    }
    if (statement && (word == "LET" || word == "DECLARE") && i+1<t.size() && sdpLocalIdentifier(t[i+1])) {
      size_t type=i+2;std::vector<std::string> names{sdpUnquoteLocal(t[i+1])};
      if (word == "DECLARE") while (type+1<t.size() && t[type] == "," && sdpLocalIdentifier(t[type+1])) {names.push_back(sdpUnquoteLocal(t[type+1]));type+=2;}
      size_t end=type;int depth=0;
      for (;end<t.size();++end) {
        if (t[end] == "(") ++depth;
        if (t[end] == ")") --depth;
        if (depth == 0 && (t[end] == "=" || t[end] == ":=" || t[end] == ";" || sdpUpper(t[end]) == "DEFAULT")) break;
      }
      const auto nameWord=sdpUpper(t[i+1]);
      const auto typeWord=type<t.size()?sdpUpper(t[type]):"";
      const bool special=nameWord == "CONTINUE" || nameWord == "EXIT" || typeWord == "CURSOR" || typeWord == "CONDITION";
      if (!special && end<t.size()) for (const auto &name:names) scopes.back().symbols.push_back({name,sdpDeclaredType(t,type,end)});
    }
    statement=false;
  }
  std::map<std::string,SdpLocalSymbol> visible;
  for (const auto &scope : scopes) for (const auto &symbol : scope.symbols) visible[sdpUpper(symbol.name)]=symbol;
  std::vector<SdpLocalSymbol> result;
  for (const auto &entry:visible) result.push_back(entry.second);
  return result;
}

struct SdpLocalRequest {bool active=false;std::string row;};
inline SdpLocalRequest sdpLocalRequest(const SdpCompletionContext &context,unsigned features) {
  if (!(features&SdpSqlShort) || context.blocked || !context.procedural) return {};
  const auto &t=context.tokens;
  if (!t.empty() && t.back() == ":") return {true,{}};
  if (t.size()>=3 && t.back() == "." && t[t.size()-3] == ":") return {true,sdpUnquoteLocal(t[t.size()-2])};
  return {};
}
} // namespace parsers
