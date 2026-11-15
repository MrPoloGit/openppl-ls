// openppl-ls: a standard-LSP-only server for OpenPPL (.ohf), over stdio.
// Mirrors phh-ls/ohh-ls's shape. This is this server's SECOND release;
// its first shipped with no registry at all. It now loads
// openppl-data/registry/symbols.json, same shape as phh-ls/ohh-ls's registry
// argument. Hover/completion still fall back to the old shape/naming-
// convention-based behavior for anything the registry doesn't cover
// (native OpenHoldem engine symbols this registry never scraped, etc.);
// see on_hover below for exactly where that fallback kicks in.

#include "document.h"
#include "jsonrpc.h"
#include "registry.h"
#include "validate.h"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>

using namespace openppl;

namespace {

int severity_to_lsp(Severity s) {
  switch (s) {
  case Severity::Error: return 1;
  case Severity::Warning: return 2;
  case Severity::Information: return 3;
  case Severity::Hint: return 4;
  }
  return 1;
}

yyjson_mut_val *position_to_json(yyjson_mut_doc *doc, const Position &p) {
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_uint(doc, obj, "line", p.line);
  yyjson_mut_obj_add_uint(doc, obj, "character", p.character);
  return obj;
}

yyjson_mut_val *range_to_json(yyjson_mut_doc *doc, const Range &r) {
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, obj, "start", position_to_json(doc, r.start));
  yyjson_mut_obj_add_val(doc, obj, "end", position_to_json(doc, r.end));
  return obj;
}

struct OpenDocument {
  std::string text;
};

bool point_in_range(TSPoint p, TSNode node) {
  TSPoint start = ts_node_start_point(node);
  TSPoint end = ts_node_end_point(node);
  auto before_or_eq = [](TSPoint a, TSPoint b) {
    return a.row < b.row || (a.row == b.row && a.column <= b.column);
  };
  return before_or_eq(start, p) && before_or_eq(p, end);
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

const char RANKS[] = "23456789TJQKA";
bool is_rank(char c) { return std::string(RANKS).find(std::toupper(static_cast<unsigned char>(c))) != std::string::npos; }
bool is_suit(char c) {
  char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return l == 'c' || l == 'd' || l == 'h' || l == 's';
}

std::string suit_name(char c) {
  switch (std::tolower(static_cast<unsigned char>(c))) {
  case 'c': return "clubs";
  case 'd': return "diamonds";
  case 'h': return "hearts";
  case 's': return "spades";
  default: return std::string(1, c);
  }
}

// Decodes the spec after "hand$"/"board$" using only the forms this
// session confirmed directly from the manual, not a registry lookup, just pattern shape.
std::string describe_card_spec(const std::string &spec) {
  std::string low = lower(spec);
  if (low.size() >= 6 && low.substr(low.size() - 6) == "suited") {
    std::string ranks = spec.substr(0, spec.size() - 6);
    if (ranks.size() == 2 && is_rank(ranks[0]) && is_rank(ranks[1])) {
      return "Suited: " + ranks + ", either suit, as long as both hole cards share a suit.";
    }
  }
  if (spec.size() == 2 && is_rank(spec[0]) && is_rank(spec[1])) {
    if (std::toupper(spec[0]) == std::toupper(spec[1])) {
      return "Pocket pair: " + spec + ".";
    }
    return "Two specific ranks, suits unconstrained (not suited, not offsuit, just \"either of "
           "these two ranks\"): " + spec + ".";
  }
  if (spec.size() == 3 && is_rank(spec[0]) && is_rank(spec[1]) && is_suit(spec[2])) {
    std::string suit = suit_name(spec[2]);
    return "**Not \"suited\"**, two SPECIFIC cards sharing one named suit: " +
           spec.substr(0, 1) + " of " + suit + ", and " + spec.substr(1, 1) +
           " of " + suit + ". Confirmed trap : this is NOT the same as `" + spec.substr(0, 2) + "Suited` (suited, either "
           "suit), the trailing letter here names ONE specific suit both cards must be.";
  }
  if (spec.size() == 4 && is_rank(spec[0]) && is_suit(spec[1]) && is_rank(spec[2]) && is_suit(spec[3])) {
    return "Two fully-specified cards: " + spec.substr(0, 1) + " of " + suit_name(spec[1]) +
           ", and " + spec.substr(2, 1) + " of " + suit_name(spec[3]) + ".";
  }
  return "Shape not recognized by this hover, not necessarily invalid, just unconfirmed.";
}

std::string describe_section_name(const std::string &name) {
  static const std::map<std::string, std::string> reserved = {
      {"f$preflop", "Reserved section: preflop rules. At least this one must be present for "
                    "OpenHoldem to enter OpenPPL mode (Structure_Of_A_PPL_File.html)."},
      {"f$flop", "Reserved section: flop rules."},
      {"f$turn", "Reserved section: turn rules."},
      {"f$river", "Reserved section: river rules."},
      {"f$sitin", "Hopper function: called to decide whether to sit back in."},
      {"f$sitout", "Hopper function: called to decide whether to sit out "
                   "(differences to shanky.html, replaces the removed SitOut action)."},
      {"f$autopost", "Hopper function: controls automatic blind/ante posting."},
      {"f$leave", "Hopper function: called to decide whether to leave the table."},
      {"f$close", "Hopper function: called to decide whether to close the table."},
      {"f$delay", "Special function: returns the delay (in ms) before an action executes "
                  "(differences to shanky.html, replaces Shanky's per-action Delay keyword)."},
  };
  auto it = reserved.find(name);
  if (it != reserved.end()) return it->second;
  if (lower(name).substr(0, 4) == "list") {
    return "Handlist (Handlists.html: names starting with \"list\" hold hand-notation entries "
           "like `AA`, `AKs`, `72o`, used by name as a boolean symbol in a WHEN condition).";
  }
  if (name.substr(0, 2) == "f$") {
    return "User-defined function. Call it elsewhere as `" + name + "` "
           "(Building_Symbols_On_Your_Own.html).";
  }
  return "Section header (name doesn't match any confirmed reserved/handlist/f$ shape).";
}

std::string describe_identifier(const std::string &name) {
  if (name.substr(0, 4) == "user") {
    return "Looks like a PPL-level user-defined variable (User_Defined_Variables.html: must "
           "start with \"user\"). Starts false; `Set` makes it true for the rest of the hand.";
  }
  if (name.substr(0, 6) == "me_st_") return "OpenHoldem memory-store symbol (User_Defined_Variables.html).";
  if (name.substr(0, 6) == "me_re_") return "OpenHoldem memory-recall symbol (User_Defined_Variables.html).";
  if (name.substr(0, 2) == "f$") return "User-defined function reference.";
  bool all_lower = !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return !std::isalpha(c) || std::islower(c);
  });
  if (all_lower) {
    return "Not in openppl-data/registry/symbols.json, but all-lowercase, per the confirmed "
           "convention (differences to shanky.html), likely a native OpenHoldem engine symbol "
           "this registry doesn't cover (it only scrapes OpenPPL's own symbol-library pages).";
  }
  return "Not in openppl-data/registry/symbols.json. Could be a real OpenPPL symbol this registry "
         "missed, or a typo, there is no second implementation to cross-check against "
         ".";
}

const char *KEYWORD_DOCS(const std::string &type) {
  static const std::map<std::string, const char *> docs = {
      {"when_kw", "Starts a rule. Case-insensitive (`WHEN`/`When`/`when` all valid)."},
      {"force_kw", "Mandatory on every action/RETURN in OpenPPL (Actions.html, unlike "
                   "Standard PPL, where it was optional)."},
      {"set_kw", "Records a user-defined variable as true. Takes no FORCE "
                 "(User_Defined_Variables.html)."},
      {"return_kw", "Returns a value from a user-defined function. Needs FORCE."},
      {"and_kw", "Logical AND. Precedence: Not > And > XOr > Or."},
      {"or_kw", "Logical OR (lowest precedence of the four logical operators)."},
      {"xor_kw", "Logical exclusive-or: true if exactly one operand is true."},
      {"not_kw", "Logical NOT (highest precedence of the four logical operators)."},
      {"mod_kw", "Arithmetic modulus."},
      {"suited_kw", "Space-separated suited-board form (`board$AT SUITED`), the manual also "
                    "shows a suffix form (`board$ATSuited`) elsewhere; both are accepted here "
                    "since the two aren't reconciled against each other "
                    "."},
  };
  auto it = docs.find(type);
  return it != docs.end() ? it->second : nullptr;
}

class Server {
public:
  explicit Server(Registry registry) : registry_(std::move(registry)) {}

  void run() {
    while (true) {
      auto body = read_message(stdin);
      if (!body) break;

      yyjson_doc *doc = yyjson_read(body->data(), body->size(), 0);
      if (!doc) continue;
      yyjson_val *root = yyjson_doc_get_root(doc);
      yyjson_val *method_val = yyjson_obj_get(root, "method");
      yyjson_val *id_val = yyjson_obj_get(root, "id");
      yyjson_val *params = yyjson_obj_get(root, "params");
      std::string method = method_val ? yyjson_get_str(method_val) : "";

      dispatch(method, id_val, params);

      yyjson_doc_free(doc);
      if (should_exit_) break;
    }
  }

private:
  Registry registry_;
  DocumentParser parser_;
  std::map<std::string, OpenDocument> open_documents_;
  bool should_exit_ = false;

  void dispatch(const std::string &method, yyjson_val *id, yyjson_val *params) {
    if (method == "initialize") {
      respond_initialize(id);
    } else if (method == "initialized") {
      // nothing to do
    } else if (method == "textDocument/didOpen") {
      on_did_open(params);
    } else if (method == "textDocument/didChange") {
      on_did_change(params);
    } else if (method == "textDocument/didClose") {
      on_did_close(params);
    } else if (method == "textDocument/hover") {
      on_hover(id, params);
    } else if (method == "textDocument/completion") {
      on_completion(id, params);
    } else if (method == "shutdown") {
      respond_null(id);
    } else if (method == "exit") {
      should_exit_ = true;
    } else if (id) {
      respond_method_not_found(id, method);
    }
  }

  void send(yyjson_mut_doc *doc) {
    char *json = yyjson_mut_write(doc, 0, nullptr);
    if (json) {
      write_message(stdout, json);
      free(json);
    }
    yyjson_mut_doc_free(doc);
  }

  yyjson_mut_doc *new_response(yyjson_val *id, yyjson_mut_val **out_obj) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    yyjson_mut_obj_add_strcpy(doc, obj, "jsonrpc", "2.0");
    if (id) {
      yyjson_mut_val *id_copy = yyjson_val_mut_copy(doc, id);
      yyjson_mut_obj_add_val(doc, obj, "id", id_copy);
    } else {
      yyjson_mut_obj_add_null(doc, obj, "id");
    }
    *out_obj = obj;
    return doc;
  }

  void respond_null(yyjson_val *id) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_obj_add_null(doc, obj, "result");
    send(doc);
  }

  void respond_method_not_found(yyjson_val *id, const std::string &method) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *err = yyjson_mut_obj_add_obj(doc, obj, "error");
    yyjson_mut_obj_add_int(doc, err, "code", -32601);
    yyjson_mut_obj_add_strcpy(doc, err, "message", ("method not found: " + method).c_str());
    send(doc);
  }

  void respond_initialize(yyjson_val *id) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, obj, "result");
    yyjson_mut_val *caps = yyjson_mut_obj_add_obj(doc, result, "capabilities");
    yyjson_mut_obj_add_int(doc, caps, "textDocumentSync", 1);
    yyjson_mut_obj_add_true(doc, caps, "hoverProvider");
    yyjson_mut_val *completion = yyjson_mut_obj_add_obj(doc, caps, "completionProvider");
    yyjson_mut_val *trigger = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, completion, "triggerCharacters", trigger);

    yyjson_mut_val *info = yyjson_mut_obj_add_obj(doc, result, "serverInfo");
    yyjson_mut_obj_add_strcpy(doc, info, "name", "openppl-ls");
    yyjson_mut_obj_add_strcpy(doc, info, "version", "0.1.0");
    send(doc);
  }

  std::string extract_uri(yyjson_val *params) {
    yyjson_val *td = yyjson_obj_get(params, "textDocument");
    yyjson_val *uri = yyjson_obj_get(td, "uri");
    return uri ? yyjson_get_str(uri) : std::string();
  }

  void on_did_open(yyjson_val *params) {
    yyjson_val *td = yyjson_obj_get(params, "textDocument");
    std::string uri = extract_uri(params);
    yyjson_val *text = yyjson_obj_get(td, "text");
    if (uri.empty() || !text) return;
    open_documents_[uri] = OpenDocument{yyjson_get_str(text)};
    publish_diagnostics(uri);
  }

  void on_did_change(yyjson_val *params) {
    std::string uri = extract_uri(params);
    if (uri.empty()) return;
    yyjson_val *changes = yyjson_obj_get(params, "contentChanges");
    if (!yyjson_is_arr(changes) || yyjson_arr_size(changes) == 0) return;
    yyjson_val *last = yyjson_arr_get_last(changes);
    yyjson_val *text = yyjson_obj_get(last, "text");
    if (!text) return;
    open_documents_[uri] = OpenDocument{yyjson_get_str(text)};
    publish_diagnostics(uri);
  }

  void on_did_close(yyjson_val *params) {
    std::string uri = extract_uri(params);
    open_documents_.erase(uri);
    send_publish_diagnostics(uri, {}, nullptr);
  }

  void publish_diagnostics(const std::string &uri) {
    auto it = open_documents_.find(uri);
    if (it == open_documents_.end()) return;
    ParsedDocument parsed = parser_.parse(it->second.text);
    auto diagnostics = validate(parsed, registry_);
    send_publish_diagnostics(uri, diagnostics, &parsed);
  }

  void send_publish_diagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics,
                                 const ParsedDocument *parsed) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    yyjson_mut_obj_add_strcpy(doc, obj, "jsonrpc", "2.0");
    yyjson_mut_obj_add_strcpy(doc, obj, "method", "textDocument/publishDiagnostics");
    yyjson_mut_val *params = yyjson_mut_obj_add_obj(doc, obj, "params");
    yyjson_mut_obj_add_strcpy(doc, params, "uri", uri.c_str());
    yyjson_mut_val *arr = yyjson_mut_obj_add_arr(doc, params, "diagnostics");

    if (parsed) {
      for (const auto &r : parsed->syntax_error_ranges) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, arr);
        yyjson_mut_obj_add_val(doc, item, "range", range_to_json(doc, r));
        yyjson_mut_obj_add_int(doc, item, "severity", 1);
        yyjson_mut_obj_add_strcpy(doc, item, "source", "openppl-ls");
        yyjson_mut_obj_add_strcpy(doc, item, "code", "syntax-error");
        yyjson_mut_obj_add_strcpy(doc, item, "message", "Invalid OpenPPL syntax.");
      }
    }
    for (const auto &d : diagnostics) {
      yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, arr);
      yyjson_mut_obj_add_val(doc, item, "range", range_to_json(doc, d.range));
      yyjson_mut_obj_add_int(doc, item, "severity", severity_to_lsp(d.severity));
      yyjson_mut_obj_add_strcpy(doc, item, "source", "openppl-ls");
      yyjson_mut_obj_add_strcpy(doc, item, "code", d.code.c_str());
      yyjson_mut_obj_add_strcpy(doc, item, "message", d.message.c_str());
    }
    send(doc);
  }

  // Walks up from the smallest node at `pos` to find the nearest
  // ancestor type this hover implementation knows how to describe.
  TSNode node_at(const ParsedDocument &parsed, Position pos) {
    TSPoint p{pos.line, pos.character};
    return ts_node_descendant_for_point_range(parsed.root(), p, p);
  }

  void on_hover(yyjson_val *id, yyjson_val *params) {
    std::string uri = extract_uri(params);
    yyjson_val *position = yyjson_obj_get(params, "position");
    auto it = open_documents_.find(uri);
    if (it == open_documents_.end() || !position) { respond_null(id); return; }

    Position pos;
    pos.line = static_cast<uint32_t>(yyjson_get_uint(yyjson_obj_get(position, "line")));
    pos.character = static_cast<uint32_t>(yyjson_get_uint(yyjson_obj_get(position, "character")));

    ParsedDocument parsed = parser_.parse(it->second.text);
    TSNode node = node_at(parsed, pos);
    if (ts_node_is_null(node)) { respond_null(id); return; }

    std::string text;
    for (TSNode n = node; !ts_node_is_null(n); n = ts_node_parent(n)) {
      std::string type = ts_node_type(n);
      if (type == "hand_expression") {
        text = "**" + node_text(n, parsed.source) + "**\n\n" +
               describe_card_spec(node_text(n, parsed.source).substr(5));
        break;
      }
      if (type == "board_expression") {
        std::string full = node_text(n, parsed.source);
        bool has_suited_kw = !ts_node_is_null(ts_node_child_by_field_name(n, "suited", 6));
        std::string spec = full.substr(6);
        // strip a trailing " SUITED"-style keyword's own text if present
        size_t space = spec.find_first_of(" \t");
        if (space != std::string::npos) spec = spec.substr(0, space);
        std::string desc = describe_card_spec(has_suited_kw ? spec + "Suited" : spec);
        text = "**" + full + "**\n\n" + desc;
        break;
      }
      if (type == "section_header") {
        TSNode name_node = ts_node_child_by_field_name(n, "name", 4);
        std::string name = node_text(name_node, parsed.source);
        text = "**##" + name + "##**\n\n" + describe_section_name(name);
        break;
      }
      if (const char *kw_doc = KEYWORD_DOCS(type)) {
        text = "**" + node_text(n, parsed.source) + "**\n\n" + kw_doc;
        break;
      }
      if (type == "hand_range") {
        std::string spec = node_text(n, parsed.source);
        std::string low = lower(spec);
        std::string desc;
        if (spec.size() >= 2 && std::toupper(spec[0]) == std::toupper(spec[1])) {
          desc = "Pocket pair.";
        } else if (!low.empty() && low.back() == 's') {
          desc = "Suited (either suit, matching), note: a bare `hand$" + spec.substr(0, 2) +
                 "s` means something different (two SPECIFIC same-suit cards), not this.";
        } else if (!low.empty() && low.back() == 'o') {
          desc = "Offsuit.";
        } else {
          desc = "Two different ranks with no suited/offsuit suffix, every confirmed "
                 "non-pair example in the manual includes one; unconfirmed whether this "
                 "bare form is valid.";
        }
        text = "**" + spec + "** (handlist entry)\n\n" + desc;
        break;
      }
      if (type == "identifier") {
        // An identifier that is itself the "name" field of a
        // section_header needs the section-header description, not the
        // generic symbol-reference one, found by testing, not by
        // inspection: this case used to fall through to
        // describe_identifier() unconditionally, so hovering the text
        // inside "##f$preflop##" reported "User-defined function
        // reference" instead of "Reserved section", since this branch
        // matched and `break`ed before the loop's next iteration ever
        // reached the parent section_header node.
        TSNode parent = ts_node_parent(n);
        std::string name = node_text(n, parsed.source);
        bool parent_is_section_header = !ts_node_is_null(parent) &&
                                         std::string(ts_node_type(parent)) == "section_header";
        bool parent_is_action_field = !ts_node_is_null(parent) &&
                                       std::string(ts_node_type(parent)) == "action_statement" &&
                                       !ts_node_is_null(ts_node_child_by_field_name(parent, "action", 6)) &&
                                       ts_node_eq(ts_node_child_by_field_name(parent, "action", 6), n);

        if (parent_is_section_header) {
          text = "**##" + name + "##**\n\n" + describe_section_name(name);
        } else if (parent_is_action_field) {
          auto action_it = registry_.actions.find(name);
          if (action_it != registry_.actions.end()) {
            text = "**" + name + "** (action)\n\n" +
                   (action_it->second.takes_argument
                        ? "Takes a betsize argument before FORCE."
                        : "Fixed action, takes no argument.");
          } else if (name == "SitOut") {
            text = "**" + name + "**\n\nNo longer supported (Actions.html), use `f$sitout` "
                   "instead.";
          } else {
            text = "**" + name + "**\n\nNot one of OpenPPL's confirmed actions.";
          }
        } else if (auto sym = registry_.find_symbol(name)) {
          text = "**" + name + "** (" + sym->type + ")\n\n" + sym->meaning +
                 (sym->low_confidence
                      ? "\n\n*Lower confidence: this symbol's exact behavior wasn't fully "
                        "certain from its name alone, see openppl-data/registry/README.md.*"
                      : "");
        } else {
          text = "**" + name + "**\n\n" + describe_identifier(name);
        }
        break;
      }
    }

    if (text.empty()) { respond_null(id); return; }

    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, obj, "result");
    yyjson_mut_val *contents = yyjson_mut_obj_add_obj(doc, result, "contents");
    yyjson_mut_obj_add_strcpy(doc, contents, "kind", "markdown");
    yyjson_mut_obj_add_strcpy(doc, contents, "value", text.c_str());
    send(doc);
  }

  void on_completion(yyjson_val *id, yyjson_val * /*params*/) {
    // Now backed by the real registry (openppl-data/registry/symbols.json),
    // unlike this server's first release, plus the small, fixed
    // keyword/reserved-section/action sets, which were never
    // registry-dependent in the first place.
    static const std::vector<std::string> keywords = {
        "WHEN", "FORCE", "SET", "RETURN", "AND", "OR", "XOR", "NOT", "MOD", "SUITED",
    };
    static const std::vector<std::string> reserved_sections = {
        "f$preflop", "f$flop", "f$turn", "f$river",
        "f$sitin", "f$sitout", "f$autopost", "f$leave", "f$close", "f$delay",
    };

    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "result", result);

    auto add_item = [&](const std::string &label, const std::string &detail, int kind) {
      yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, result);
      yyjson_mut_obj_add_strcpy(doc, item, "label", label.c_str());
      if (!detail.empty()) yyjson_mut_obj_add_strcpy(doc, item, "detail", detail.c_str());
      yyjson_mut_obj_add_int(doc, item, "kind", kind);
    };
    for (const auto &k : keywords) add_item(k, "", 14 /* Keyword */);
    for (const auto &s : reserved_sections) add_item(s, "", 3 /* Function */);
    for (const auto &[name, info] : registry_.actions) {
      add_item(name, info.takes_argument ? "action (needs a betsize)" : "action", 2 /* Method */);
    }
    for (const auto &[name, info] : registry_.symbols) {
      add_item(name, info.type, 6 /* Variable */);
    }

    send(doc);
  }
};

} // namespace

int main(int argc, char **argv) {
  ensure_binary_stdio();

  if (argc < 2) {
    std::cerr << "usage: openppl-ls <registry-dir>\n";
    return 2;
  }

  Registry registry;
  std::string error;
  if (!registry.load(argv[1], error)) {
    std::cerr << "openppl-ls: failed to load registry: " << error << "\n";
    return 1;
  }

  Server server(std::move(registry));
  server.run();
  return 0;
}
