#include "validate.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace openppl {

namespace {

bool starts_with(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool is_all_lowercase_identifier(const std::string &s) {
  for (char c : s) {
    if (std::isalpha(static_cast<unsigned char>(c)) && !std::islower(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

bool starts_with_list_ci(const std::string &name) {
  if (name.size() < 4) return false;
  return lower(name.substr(0, 4)) == "list";
}

// The confirmed, closed set of reserved/hopper section names. An
// f$... reference to one of these is always valid, even with no
// matching ##...## section in THIS file (f$sitout etc. are called by
// OpenHoldem itself, not
// necessarily defined visibly next to where they're referenced).
bool is_reserved_section_name(const std::string &name) {
  static const std::set<std::string> reserved = {
      "f$preflop", "f$flop",    "f$turn",      "f$river",  "f$sitin",
      "f$sitout",  "f$autopost", "f$leave",     "f$close",  "f$delay",
  };
  return reserved.count(name) > 0;
}

void collect_identifiers(TSNode node, std::vector<TSNode> &out) {
  if (ts_node_is_null(node)) return;
  if (!std::strcmp(ts_node_type(node), "identifier")) {
    out.push_back(node);
    return;
  }
  uint32_t count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_identifiers(ts_node_named_child(node, i), out);
  }
}

// Returns a human hint if `name` case-insensitively matches one of the
// three confirmed "false friends" pairs, else empty.
std::string false_friend_hint(const std::string &name, const Registry &registry) {
  auto it = registry.false_friends.find(lower(name));
  if (it == registry.false_friends.end()) return {};
  return " (possible case mix-up: " + it->second + ")";
}

void check_symbol_reference(TSNode id_node, const std::string &source, const ParsedDocument &doc,
                             const Registry &registry, std::vector<Diagnostic> &out) {
  std::string name = node_text(id_node, source);
  if (name.empty()) return;

  if (starts_with(name, "user")) return;    // PPL-level user variable
  if (starts_with(name, "me_st_")) return;  // memory store
  if (starts_with(name, "me_re_")) return;  // memory recall

  if (starts_with(name, "f$")) {
    if (is_reserved_section_name(name)) return;
    for (const auto &s : doc.sections) {
      if (s.name == name) return;
    }
    Diagnostic d;
    d.range = node_range(id_node);
    d.severity = Severity::Warning;
    d.code = "unknown-function-reference";
    d.message = "\"" + name + "\" is not one of the reserved/hopper function names and no "
                "\"##" + name + "##\" section is defined in this file.";
    out.push_back(std::move(d));
    return;
  }

  if (starts_with_list_ci(name)) {
    for (const auto &s : doc.sections) {
      if (s.name == name) return;
    }
    Diagnostic d;
    d.range = node_range(id_node);
    d.severity = Severity::Warning;
    d.code = "unknown-handlist-reference";
    d.message = "\"" + name + "\" looks like a handlist reference (Handlists.html: names "
                "starting with \"list\") but no \"##" + name + "##\" section is defined in "
                "this file.";
    out.push_back(std::move(d));
    return;
  }

  if (registry.find_symbol(name)) return;

  // Confirmed convention (differences to shanky.html): native
  // OpenHoldem engine symbols (balance0, ac_dealpos0, betround, ...)
  // are always lowercase, and this registry does not cover them at all
  // (it only covers OpenPPL's own symbol-library pages, never the
  // separate native-OpenHoldem-symbol manual this project hasn't
  // fetched). Skipping all-lowercase names here is a deliberate,
  // confirmed-convention-based way to avoid a flood of false positives
  // on legitimate symbols this registry simply doesn't claim to know
  // about, not a guess.
  if (is_all_lowercase_identifier(name)) return;

  Diagnostic d;
  d.range = node_range(id_node);
  d.severity = Severity::Warning;
  d.code = "unknown-symbol";
  d.message = "\"" + name + "\" is not in the OpenPPL symbol registry" +
              false_friend_hint(name, registry) +
              ". This registry covers OpenPPL's own symbol library only, not every native "
              "OpenHoldem engine symbol, and has no second implementation to cross-check "
              "against, so treat this as a lead, not "
              "a certainty.";
  out.push_back(std::move(d));
}

void check_expression(TSNode expr, const std::string &source, const ParsedDocument &doc,
                       const Registry &registry, std::vector<Diagnostic> &out) {
  if (ts_node_is_null(expr)) return;
  std::vector<TSNode> ids;
  collect_identifiers(expr, ids);
  for (TSNode id : ids) {
    check_symbol_reference(id, source, doc, registry, out);
  }
}

void check_action(TSNode action_stmt, const std::string &source, const ParsedDocument &doc,
                   const Registry &registry, std::vector<Diagnostic> &out) {
  TSNode action_id = ts_node_child_by_field_name(action_stmt, "action", 6);
  if (ts_node_is_null(action_id)) return;
  std::string name = node_text(action_id, source);

  if (name == "SitOut") {
    Diagnostic d;
    d.range = node_range(action_id);
    d.severity = Severity::Warning;
    d.code = "removed-action";
    d.message = "\"SitOut\" is no longer a supported action (Actions.html), use the "
                "\"f$sitout\" function instead.";
    out.push_back(std::move(d));
  } else {
    auto it = registry.actions.find(name);
    if (it == registry.actions.end()) {
      Diagnostic d;
      d.range = node_range(action_id);
      d.severity = Severity::Warning;
      d.code = "unknown-action";
      d.message = "\"" + name + "\" is not one of OpenPPL's confirmed actions" +
                  false_friend_hint(name, registry) + ".";
      out.push_back(std::move(d));
    } else {
      TSNode arg = ts_node_child_by_field_name(action_stmt, "argument", 8);
      bool has_arg = !ts_node_is_null(arg);
      if (it->second.takes_argument && !has_arg) {
        Diagnostic d;
        d.range = node_range(action_id);
        d.severity = Severity::Warning;
        d.code = "action-missing-argument";
        d.message = "\"" + name + "\" needs a betsize argument (Actions.html).";
        out.push_back(std::move(d));
      } else if (!it->second.takes_argument && has_arg) {
        Diagnostic d;
        d.range = node_range(action_id);
        d.severity = Severity::Warning;
        d.code = "action-unexpected-argument";
        d.message = "\"" + name + "\" does not take a betsize argument (Actions.html).";
        out.push_back(std::move(d));
      }
      if (has_arg) check_expression(arg, source, doc, registry, out);
    }
  }
}

void check_set_statement(TSNode set_stmt, const std::string &source, std::vector<Diagnostic> &out) {
  TSNode var = ts_node_child_by_field_name(set_stmt, "variable", 8);
  if (ts_node_is_null(var)) return;
  std::string name = node_text(var, source);
  if (starts_with(name, "user") || starts_with(name, "me_st_") || starts_with(name, "me_re_")) return;
  Diagnostic d;
  d.range = node_range(var);
  d.severity = Severity::Warning;
  d.code = "set-target-naming-convention";
  d.message = "\"" + name + "\" doesn't start with \"user\" (or \"me_st_\"/\"me_re_\" for a "
              "memory symbol), User_Defined_Variables.html: user-defined variables "
              "\"need to be prefixed with the word 'user'\".";
  out.push_back(std::move(d));
}

void check_when_statements(TSNode node, const std::string &source, const ParsedDocument &doc,
                            const Registry &registry, std::vector<Diagnostic> &out) {
  uint32_t count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(node, i);
    if (std::strcmp(ts_node_type(child), "when_statement") != 0) continue;

    TSNode condition = ts_node_child_by_field_name(child, "condition", 9);
    check_expression(condition, source, doc, registry, out);

    TSNode consequence = ts_node_child_by_field_name(child, "consequence", 11);
    if (ts_node_is_null(consequence)) continue;
    const char *ctype = ts_node_type(consequence);
    if (!std::strcmp(ctype, "action_statement")) {
      check_action(consequence, source, doc, registry, out);
    } else if (!std::strcmp(ctype, "return_statement")) {
      TSNode value = ts_node_child_by_field_name(consequence, "value", 5);
      check_expression(value, source, doc, registry, out);
    } else if (!std::strcmp(ctype, "set_statement")) {
      check_set_statement(consequence, source, out);
    }
  }
}

bool starts_with_list(const std::string &name) { return starts_with_list_ci(name); }

} // namespace

std::vector<Diagnostic> validate(const ParsedDocument &doc, const Registry &registry) {
  std::vector<Diagnostic> out;

  for (const auto &section : doc.sections) {
    bool named_like_handlist = starts_with_list(section.name);

    if (named_like_handlist && section.has_when_statement_items && !section.has_hand_range_items) {
      Diagnostic d;
      d.range = section.name_range;
      d.severity = Severity::Warning;
      d.code = "handlist-name-but-rule-body";
      d.message = "Section \"" + section.name + "\" is named like a handlist (starts with "
                  "\"list\", per Handlists.html) but its body looks like WHEN-rules, not "
                  "hand-notation entries (e.g. \"AA\", \"AKs\"). If this is meant to be a "
                  "function, choose a name that doesn't start with \"list\".";
      out.push_back(std::move(d));
    }

    if (!named_like_handlist && section.has_hand_range_items) {
      Diagnostic d;
      d.range = section.name_range;
      d.severity = Severity::Warning;
      d.code = "handlist-body-but-wrong-name";
      d.message = "Section \"" + section.name + "\" has a body that looks like a handlist "
                  "(hand-notation entries such as \"AA\", \"AKs\") but its name doesn't start "
                  "with \"list\", Handlists.html: \"handlist names need to start with the "
                  "word 'list'\".";
      out.push_back(std::move(d));
    }
  }

  check_when_statements(doc.root(), doc.source, doc, registry, out);

  return out;
}

} // namespace openppl
