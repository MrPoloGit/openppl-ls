# openppl-ls

A standard-LSP-only server for OpenPPL (`.ohf`).

## Registry-backed, this server's second release

Unlike this server's **first** release, it now takes a **registry
directory argument**, same shape as `phh-ls`/`ohh-ls`, and loads
`openppl-data/registry/symbols.json`. That registry didn't exist at first:
the OpenPPL manual is GPLv3, and the project owner initially chose to
build the grammar and this server *without* any registry rather than
resolve that licensing question first. The question is now
resolved, the project is going open source, and the registry was built
in a "restate independently" posture (every `meaning` composed fresh
from the symbol's name, not copied from the manual, see
`openppl-data/registry/README.md`), and this server was updated to actually
use it.

## Build

```sh
cmake -S . -B build
cmake --build build -j4
```

Works the same way on macOS, Linux and Windows: the only prerequisites
are a C/C++ compiler and CMake 3.16+, same as `phh-ls`'s own README
explains in full, tree-sitter (the runtime library) and yyjson are
fetched and built from source via CMake's `FetchContent`, not found
through a system package manager.

Produces `openppl-ls` and `openppl-check` (a standalone CLI, mirrors
`phh-check`/`ohh-check`). Both build against `../tree-sitter-openppl`'s
generated parser directly. Usage:

```sh
openppl-ls <registry-dir>                       # LSP server, stdio
openppl-check <registry-dir> <file.ohf> [...]    # standalone CLI
```
