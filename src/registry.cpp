#include "registry.h"

#include <yyjson.h>

namespace openppl {

namespace {

yyjson_doc *read_json_file(const std::string &path, std::string &error) {
  yyjson_read_err err;
  yyjson_doc *doc = yyjson_read_file(path.c_str(), YYJSON_READ_NOFLAG, nullptr, &err);
  if (!doc) {
    error = "failed to parse " + path + ": " + (err.msg ? err.msg : "unknown error");
    return nullptr;
  }
  return doc;
}

std::string str_or_empty(yyjson_val *v) {
  const char *s = (v && yyjson_is_str(v)) ? yyjson_get_str(v) : nullptr;
  return s ? std::string(s) : std::string();
}

void load_direct_entry(yyjson_val *entry, Registry &reg) {
  std::string name = str_or_empty(yyjson_obj_get(entry, "name"));
  if (name.empty()) return;
  SymbolInfo info;
  info.type = str_or_empty(yyjson_obj_get(entry, "type"));
  info.meaning = str_or_empty(yyjson_obj_get(entry, "meaning"));
  info.low_confidence = str_or_empty(yyjson_obj_get(entry, "confidence")) == "medium";
  reg.symbols[name] = std::move(info);
}

// Expands a "name_template"/"n_range" or "name_template"/"values" entry
// (openppl/registry/symbols.json's Technical category) into one
// concrete SymbolInfo per real symbol name.
void load_templated_entry(yyjson_val *entry, Registry &reg) {
  std::string tmpl = str_or_empty(yyjson_obj_get(entry, "name_template"));
  if (tmpl.empty()) return;
  std::string type = str_or_empty(yyjson_obj_get(entry, "type"));
  std::string meaning = str_or_empty(yyjson_obj_get(entry, "meaning"));
  bool low_confidence = str_or_empty(yyjson_obj_get(entry, "confidence")) == "medium";

  std::vector<std::string> names;
  yyjson_val *n_range = yyjson_obj_get(entry, "n_range");
  yyjson_val *values = yyjson_obj_get(entry, "values");
  if (yyjson_is_arr(n_range) && yyjson_arr_size(n_range) == 2) {
    int64_t lo = yyjson_get_sint(yyjson_arr_get(n_range, 0));
    int64_t hi = yyjson_get_sint(yyjson_arr_get(n_range, 1));
    size_t placeholder = tmpl.find("{N}");
    for (int64_t n = lo; n <= hi; ++n) {
      std::string name = tmpl;
      if (placeholder != std::string::npos) {
        name = tmpl.substr(0, placeholder) + std::to_string(n) + tmpl.substr(placeholder + 3);
      }
      names.push_back(name);
    }
  } else if (yyjson_is_arr(values)) {
    size_t idx, max;
    yyjson_val *v;
    yyjson_arr_foreach(values, idx, max, v) { names.push_back(str_or_empty(v)); }
  }

  for (const auto &name : names) {
    SymbolInfo info;
    info.type = type;
    info.meaning = meaning;
    info.low_confidence = low_confidence;
    reg.symbols[name] = std::move(info);
  }
}

bool load_symbols(yyjson_val *root, Registry &reg, std::string &error) {
  yyjson_val *categories = yyjson_obj_get(root, "categories");
  if (!yyjson_is_obj(categories)) {
    error = "symbols.json: missing top-level \"categories\" object";
    return false;
  }

  size_t cidx, cmax;
  yyjson_val *ckey, *cval;
  yyjson_obj_foreach(categories, cidx, cmax, ckey, cval) {
    std::string category = str_or_empty(ckey);

    if (category == "PokerTracker") {
      // Fully parametric: "pt_" + base_stat name + postfix, where a
      // postfix is either a word form ("_raischair", already including
      // its own leading underscore) or a bare chair digit ("0".."9",
      // attached directly with no separator), confirmed naming
      // convention, symbols.json's own _comment.
      yyjson_val *postfixes = yyjson_obj_get(cval, "postfixes");
      yyjson_val *base_stats = yyjson_obj_get(cval, "base_stats");
      std::vector<std::string> postfix_list;
      if (yyjson_is_arr(postfixes)) {
        size_t pidx, pmax;
        yyjson_val *p;
        yyjson_arr_foreach(postfixes, pidx, pmax, p) { postfix_list.push_back(str_or_empty(p)); }
      }
      if (yyjson_is_arr(base_stats)) {
        size_t bidx, bmax;
        yyjson_val *b;
        yyjson_arr_foreach(base_stats, bidx, bmax, b) {
          std::string base_name = str_or_empty(yyjson_obj_get(b, "name"));
          std::string meaning = str_or_empty(yyjson_obj_get(b, "meaning"));
          if (base_name.empty()) continue;
          for (const auto &postfix : postfix_list) {
            SymbolInfo info;
            info.type = "decimal";
            info.meaning = meaning;
            reg.symbols["pt_" + base_name + postfix] = info;
          }
        }
      }
      continue;
    }

    if (!yyjson_is_arr(cval)) continue;
    size_t eidx, emax;
    yyjson_val *entry;
    yyjson_arr_foreach(cval, eidx, emax, entry) {
      if (yyjson_obj_get(entry, "name_template")) {
        load_templated_entry(entry, reg);
      } else {
        load_direct_entry(entry, reg);
      }
    }
  }
  return true;
}

} // namespace

bool Registry::load(const std::string &directory, std::string &error) {
  std::string path = directory + "/symbols.json";
  yyjson_doc *doc = read_json_file(path, error);
  if (!doc) return false;
  yyjson_val *root = yyjson_doc_get_root(doc);
  bool ok = load_symbols(root, *this, error);
  yyjson_doc_free(doc);
  if (!ok) return false;

  // The confirmed, exhaustive action list (Actions.html), a small,
  // closed set hand-confirmed against the manual's own page directly,
  // never scraped from a manual table.
  for (const char *name : {"BetMax", "RaiseMax", "Allin", "BetPot", "RaisePot",
                            "BetThreeFourthPot", "RaiseThreeFourthPot", "BetTwoThirdPot",
                            "RaiseTwoThirdPot", "BetHalfPot", "RaiseHalfPot", "BetThirdPot",
                            "RaiseThirdPot", "BetFourthPot", "RaiseFourthPot", "Bet", "Raise",
                            "BetMin", "RaiseMin", "Call", "Check", "Fold", "Beep"}) {
    actions[name] = ActionInfo{false};
  }
  actions["RaiseTo"] = ActionInfo{true};
  actions["RaiseBy"] = ActionInfo{true};

  // Also recognized as values in RETURN/condition position, per
  // Building_Symbols_On_Your_Own.html's own "RETURN True FORCE"/
  // "RETURN False FORCE" examples, small, closed, not scraped.
  symbols["True"] = SymbolInfo{"boolean", "Boolean literal true.", false};
  symbols["False"] = SymbolInfo{"boolean", "Boolean literal false.", false};

  false_friends = {
      {"bet", "the OH-script symbol \"bet\" (lowercase) is the minimum bet for the current "
              "round, in dollars, not the same as the action \"Bet\"."},
      {"call", "the OH-script symbol \"call\" (lowercase) is the amount needed to call, in "
               "dollars, not the same as the action \"Call\"."},
      {"random", "the OH-script symbol \"random\" (lowercase) returns a float in [0..1), not "
                 "the same as the OpenPPL function \"Random\", which returns an integer in "
                 "[0..99]."},
  };

  return true;
}

std::optional<SymbolInfo> Registry::find_symbol(const std::string &name) const {
  auto it = symbols.find(name);
  if (it == symbols.end()) return std::nullopt;
  return it->second;
}

} // namespace openppl
