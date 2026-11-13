#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Loads openppl/registry/symbols.json plus two small, closed,
// hardcoded tables that were never scraped from any manual table and
// therefore never needed the licensing-posture treatment symbols.json
// did: the confirmed action list (Actions.html) and the three confirmed
// "false friends" case-collision pairs (differences to shanky.html).
namespace openppl {

struct SymbolInfo {
  std::string type; // "boolean" | "integer" | "decimal" | "const" | ...
  std::string meaning;
  bool low_confidence = false;
};

struct ActionInfo {
  bool takes_argument = false; // RaiseTo/RaiseBy take one; fixed actions don't
};

class Registry {
public:
  // Loads symbols.json from `directory`. Returns false (and leaves
  // `error` set) if missing or malformed, same "never approximate
  // silently" startup discipline as phh-ls/ohh-ls. Unlike those two,
  // this is the server's SECOND release, not its first: openppl-ls
  // shipped once already with no registry at all, so a caller that
  // still wants that old no-registry behavior can simply not call
  // load(); every other method here degrades gracefully on an empty
  // Registry.
  bool load(const std::string &directory, std::string &error);

  // Case-sensitive, by design: OpenPPL library symbols are
  // case-sensitive (differences to shanky.html, confirmed). Every
  // concrete name from symbols.json's direct entries AND its two
  // expanded template categories (Technical's ranges, PokerTracker's
  // prefix/postfix product) ends up in this one map.
  std::map<std::string, SymbolInfo> symbols;

  std::optional<SymbolInfo> find_symbol(const std::string &name) const;

  // The confirmed, exhaustive action list (Actions.html), never scraped
  // from a manual table, always hand-confirmed in
  //  so it carries no licensing
  // question the way symbols.json's entries did.
  std::map<std::string, ActionInfo> actions;
  bool is_known_action(const std::string &name) const { return actions.count(name) > 0; }

  // differences to shanky.html's three confirmed case-collision pairs:
  // lower-case key is the native OH-script symbol, value is what the
  // capitalized OpenPPL action/function actually means, used to give a
  // specific hint when an unknown-symbol/unknown-action diagnostic's
  // name happens to case-insensitively match one of these.
  std::map<std::string, std::string> false_friends;
};

} // namespace openppl
