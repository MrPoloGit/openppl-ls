#pragma once

#include <tree_sitter/api.h>

#include <memory>
#include <string>
#include <vector>

// Parses OpenPPL source text with tree-sitter-openppl. Unlike phh-ls's
// flat Hand/Field model or ohh-ls's recursive JsonValue tree, this keeps
// the real TSTree around and lets callers (validate.cpp, main.cpp's
// hover handler) walk it directly with tree-sitter's own node API.
// OpenPPL's document is flat (grammar.js's own design choice, see that
// file's header) and genuinely simple enough that building a second,
// parallel tree structure on top of it would be pure overhead, not
// clarity.
namespace openppl {

struct Position {
  uint32_t line = 0;
  uint32_t character = 0;
};

struct Range {
  Position start;
  Position end;
};

Position point_to_position(TSPoint p);
Range node_range(TSNode node);
std::string node_text(TSNode node, const std::string &source);

// One `##name##` header and the shape of what immediately follows it,
// up to the next header or end of document, enough to check the
// handlist-naming convention (Handlists.html: "handlist names need to
// start with the word 'list'") purely structurally, with no registry.
struct Section {
  std::string name;
  Range name_range;
  Range header_range;
  bool has_hand_range_items = false;   // looks like a handlist body
  bool has_when_statement_items = false; // looks like a function body
};

struct ParsedDocument {
  std::string source;
  std::unique_ptr<TSTree, void (*)(TSTree *)> tree{nullptr, nullptr};

  bool has_syntax_error = false;
  std::vector<Range> syntax_error_ranges;
  std::vector<Section> sections;

  TSNode root() const { return ts_tree_root_node(tree.get()); }
};

class DocumentParser {
public:
  DocumentParser();
  ~DocumentParser();
  DocumentParser(const DocumentParser &) = delete;
  DocumentParser &operator=(const DocumentParser &) = delete;

  ParsedDocument parse(const std::string &source) const;

private:
  TSParser *parser_;
};

} // namespace openppl
