#pragma once
#include <rapidjson/document.h>
#include "SymbolTable.h"

namespace parsers {
// Consume only public parameter/result metadata from a native, ACL-checked
// DESCRIBE document. Never retain its SQL body or infer a signature from text.
inline bool sdpLoadKnowledgeMetadata(SdpCatalogSymbol &symbol,const std::string &json) {
  if (symbol.kind != "knowledge") return false;
  symbol.typedMetadata=false;symbol.parameters.clear();symbol.resultColumns.clear();
  if (json.size()>1024*1024) return false;
  rapidjson::Document doc;
  if (doc.Parse(json.data(),json.size()).HasParseError() || !doc.IsObject()) return false;
  std::vector<std::pair<std::string,std::string>> parameters,columns;
  auto read=[&](const char *name,auto &output) {
    if (!doc.HasMember(name) || !doc[name].IsArray() || doc[name].Size()>256) return false;
    for (const auto &entry:doc[name].GetArray()) {
      if (!entry.IsObject() || !entry.HasMember("name") || !entry["name"].IsString() ||
          !entry.HasMember("type") || !entry["type"].IsString()) return false;
      const auto &n=entry["name"],&t=entry["type"];
      if (n.GetStringLength()==0 || n.GetStringLength()>256 || t.GetStringLength()==0 || t.GetStringLength()>256) return false;
      output.emplace_back(std::string(n.GetString(),n.GetStringLength()),std::string(t.GetString(),t.GetStringLength()));
    }
    return true;
  };
  if (!read("parameters",parameters) || !read("result_columns",columns)) return false;
  symbol.parameters=std::move(parameters);symbol.resultColumns=std::move(columns);symbol.typedMetadata=true;
  return true;
}
}
