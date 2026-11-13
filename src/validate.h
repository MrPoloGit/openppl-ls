#pragma once

#include "document.h"
#include "registry.h"

#include <string>
#include <vector>

namespace openppl {

enum class Severity { Error, Warning, Information, Hint };

struct Diagnostic {
  Range range;
  Severity severity = Severity::Error;
  std::string code;
  std::string message;
};

// Runs every L1 check currently possible. `registry` may be an empty,
// never-`load()`-ed Registry, every check below degrades gracefully to
// "nothing to check against" rather than false-flagging everything, so
// a caller that still wants openppl-ls's original no-registry behavior
//  can simply not call
// Registry::load().
//
// With a loaded registry: unknown-symbol and unknown-action warnings,
// both DELIBERATELY approximate, not exhaustive, see validate.cpp's own
// comments for exactly why.
//
// Without a registry, or always regardless of it: syntax errors come
// from the grammar directly; the handlist-naming-convention check
// (Handlists.html) needs no registry at all and always runs.
std::vector<Diagnostic> validate(const ParsedDocument &doc, const Registry &registry);

} // namespace openppl
