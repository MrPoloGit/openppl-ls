#include "document.h"

extern "C" const TSLanguage *tree_sitter_openppl(void);

#include <cstring>

namespace openppl {

Position point_to_position(TSPoint p) {
  Position pos;
  pos.line = p.row;
  pos.character = p.column;
  return pos;
}

Range node_range(TSNode node) {
  Range r;
  r.start = point_to_position(ts_node_start_point(node));
  r.end = point_to_position(ts_node_end_point(node));
  return r;
}

std::string node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (end <= start || end > source.size()) return {};
  return source.substr(start, end - start);
}

namespace {

void collect_error_ranges(TSNode node, std::vector<Range> &out) {
  if (ts_node_is_error(node) || ts_node_is_missing(node)) {
    out.push_back(node_range(node));
    return;
  }
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_error_ranges(ts_node_child(node, i), out);
  }
}

// One pass over document's direct children, grouping everything after a
// section_header (up to the next one) into that section, the
// document.cpp-side walk tree-sitter-openppl's own README says a future
// server should do, since the grammar itself stays flat.
void collect_sections(TSNode root, const std::string &source, std::vector<Section> &out) {
  uint32_t count = ts_node_named_child_count(root);
  Section *current = nullptr;
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(root, i);
    const char *type = ts_node_type(child);
    if (!std::strcmp(type, "section_header")) {
      Section s;
      TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
      s.name = node_text(name_node, source);
      s.name_range = node_range(name_node);
      s.header_range = node_range(child);
      out.push_back(std::move(s));
      current = &out.back();
    } else if (!std::strcmp(type, "hand_range")) {
      if (current) current->has_hand_range_items = true;
    } else if (!std::strcmp(type, "when_statement")) {
      if (current) current->has_when_statement_items = true;
    }
  }
}

} // namespace

DocumentParser::DocumentParser() {
  parser_ = ts_parser_new();
  ts_parser_set_language(parser_, tree_sitter_openppl());
}

DocumentParser::~DocumentParser() { ts_parser_delete(parser_); }

ParsedDocument DocumentParser::parse(const std::string &source) const {
  ParsedDocument result;
  result.source = source;

  TSTree *tree = ts_parser_parse_string(parser_, nullptr, source.c_str(),
                                         static_cast<uint32_t>(source.size()));
  result.tree = std::unique_ptr<TSTree, void (*)(TSTree *)>(tree, ts_tree_delete);

  TSNode root = result.root();

  if (ts_node_has_error(root)) {
    result.has_syntax_error = true;
    collect_error_ranges(root, result.syntax_error_ranges);
  }

  collect_sections(root, result.source, result.sections);

  return result;
}

} // namespace openppl
