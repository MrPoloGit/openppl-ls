#pragma once

#include <cstdio>
#include <optional>
#include <string>

// Minimal JSON-RPC-over-stdio framing for LSP, identical in shape to
// phh-ls/src/jsonrpc.h and ohh-ls/src/jsonrpc.h, kept as a separate copy
// per package rather than a shared library, deliberately, see each
// package's own README for why.
namespace openppl {

std::optional<std::string> read_message(FILE *in);
void write_message(FILE *out, const std::string &body);

} // namespace openppl
