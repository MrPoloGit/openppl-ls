#pragma once

#include <cstdio>
#include <optional>
#include <string>

// Minimal JSON-RPC-over-stdio framing for LSP, identical in shape to
// phh-ls/src/jsonrpc.h and ohh-ls/src/jsonrpc.h, kept as a separate copy
// per package rather than a shared library, deliberately, see each
// package's own README for why.
namespace openppl {

// Puts stdin/stdout into binary mode on Windows, a no-op everywhere else.
// Without this, the C runtime's default text mode rewrites every "\n" this
// protocol writes (including inside the literal "\r\n\r\n" header
// terminator) into "\r\n", corrupting the framing for every client. Call
// once, before any other stdio I/O.
void ensure_binary_stdio();

std::optional<std::string> read_message(FILE *in);
void write_message(FILE *out, const std::string &body);

} // namespace openppl
