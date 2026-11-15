# openppl-ls

A standard-LSP-only server for OpenPPL (`.ohf`), over stdio, same
architectural role `phh-ls`/`ohh-ls` play for their languages.

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

## What's implemented

- **Parsing** (`src/document.{h,cpp}`): parses via `tree-sitter-openppl`
  and keeps the real `TSTree` around rather than building a second,
  parallel tree structure, OpenPPL's grammar is flat by design (see
  `tree-sitter-openppl/grammar.js`'s header). One structural pass
  (`collect_sections`) groups the flat sibling sequence into `Section`s
  (name + whether its body looks like hand-notation entries or
  `WHEN`-rules).
- **Registry loading** (`src/registry.{h,cpp}`): loads
  `openppl-data/registry/symbols.json`, expanding its two parametric
  categories (`Technical`'s numbered-range templates, `PokerTracker`'s
  base-stat × postfix product) into concrete, case-sensitive-keyed
  entries at startup. Also carries two small, closed, hand-confirmed
  tables that were never scraped from any manual page and so never
  needed symbols.json's licensing-posture treatment: the full `Actions.html`
  action list (with which ones take a betsize argument), and the three
  confirmed "false friends" case-collision pairs
  (`differences to shanky.html`: `bet`/`Bet`, `call`/`Call`,
  `random`/`Random`).
- **Diagnostics** (`src/validate.{h,cpp}`):
  - The handlist-naming convention check (`Handlists.html`), checked
    both directions, unchanged from this server's first release, needs
    no registry.
  - **New**: `unknown-symbol`, every identifier referenced in a
    condition or `RETURN` value that isn't a `user`/`me_st_`/`me_re_`/
    `f$`/`list`-prefixed reference, isn't found in the registry, and
    isn't **all-lowercase**, gets a warning. The all-lowercase exemption
    is deliberate, not a loophole: native OpenHoldem engine symbols
    (`balance0`, `ac_dealpos0`, `betround`, ...) are confirmed always
    lowercase and this registry doesn't cover them at all (it only
    scrapes OpenPPL's own symbol-library pages), flagging every one of
    those would be almost pure noise.
  - **New**: `unknown-action`, `action-missing-argument`,
    `action-unexpected-argument`, an action name not in the confirmed
    list, or used with/without a betsize argument inconsistently with
    what `Actions.html` says that action takes. `SitOut` gets its own
    message pointing at `f$sitout` (`Actions.html`: no longer supported).
  - **New**: `unknown-function-reference` / `unknown-handlist-reference`,
    an `f$...`/`list...` reference with no matching `##...##` section
    in the same file and not one of the ~10 reserved/hopper names.
  - **New**: `set-target-naming-convention`, a `Set` target that
    doesn't start with `user`/`me_st_`/`me_re_`
    (`User_Defined_Variables.html`).
  - Every new check above gives a **"false friends" hint** in its
    message when the offending name case-insensitively matches one of
    the three confirmed collision pairs.
  - Syntax errors (missing `FORCE`, the confirmed `In BigBlind`-style
    malformed expressions, etc.) still come from the grammar directly.
- **Hover** (`src/main.cpp`): checks, in order, whether the hovered
  identifier is a section name (reserved/hopper table, or handlist/
  function-reference naming-shape guess), an action name (registry
  lookup, with argument-arity noted), or a real registry symbol
  (`type` + `meaning`, with a "lower confidence" note when
  `symbols.json` flags one); only falls back to the old shape/naming-
  convention guess for names in none of those. `hand$...`/`board$...`
  expressions are still decoded from shape alone (unaffected by the
  registry), including proactively surfacing the confirmed
  `hand$AKs`-is-not-"suited" trap.
- **Completion**: now includes every registry symbol and action name,
  in addition to the small fixed keyword/reserved-section set.

## How this was verified

- **`openppl-check` against the full corpus** (`openppl-data/corpus/`)
  reproduces zero diagnostics on every `valid/` fixture and the expected
  diagnostics on each `invalid/` fixture, including a new one built
  specifically for the registry-backed checks
  (`unknown-symbol.ohf`: an unknown symbol plus a lowercase `bet`
  written where the action `Bet` was meant, confirms the false-friends
  hint fires correctly).
- **`openppl-ls/tests/lsp_smoke_test.py` drives the real server binary**
  over real JSON-RPC, now including: hover on a real registry symbol
  (`Others`) returning its actual registry meaning, and completion
  including a real registry symbol (`HaveFlushDraw`) and a real action
  (`RaiseTo`), not just the old fixed keyword/reserved-section set.
  **All checks pass.**
- **Wiring in the registry surfaced real gaps in the corpus itself, not
  in this server's logic**, `operators.ohf` (built before any registry
  existed, directly from manual examples) referenced `Outs`/`CardsLeft`
  as bare symbols, which turned out to be the manual's own illustrative,
  non-functional placeholder names from a simplified teaching example
  (`Mathematical_Expressions.html`), not real registered symbols; fixed
  by using the real, complete, function-based pattern from
  `OpenPPL_In_Practice.html`'s own "Calling According To Odds And Outs"
  section instead (`f$Outs`/`f$CardsLeft`, each with its own `##...##`
  definition). Separately, the fixture's `Stacksize` didn't match the
  registry's `StackSize`, a genuine spelling discrepancy between the
  manual's own prose (`differences to shanky.html`) and its own
  reference table (`betsizes.html`), the fixture was corrected to the
  reference table's spelling, and `Position != First`/`Position = Last`
  surfaced two real named constants (`First`, `Last`) confirmed directly
  in manual examples but never listed in any scraped reference table, added to the registry with that provenance noted.
- **A real bug found by the hover test, before the registry existed and
  still true after**: hovering the text inside `##f$preflop##` initially
  reported "User-defined function reference" instead of "Reserved
  section", the hover handler's walk-up-the-tree loop matched the
  generic `identifier` case before it ever reached the parent
  `section_header` node. Fixed by checking an `identifier`'s immediate
  parent first.

## Known scope limits (not hidden)

- **Unknown-symbol/unknown-action detection is deliberately approximate,
  not exhaustive.** This registry covers OpenPPL's own symbol-library
  pages only, not the separate native-OpenHoldem-engine-symbol manual
  (never fetched in this project), the all-lowercase exemption reduces
  but doesn't eliminate false negatives/positives either way, and there
  is no second implementation to cross-check against at all.
- **The one-level-only open-ended-`WHEN` nesting limit is still not
  checked.** The grammar's document root is flat by design, so there is
  no surviving tree structure to check nesting depth against.
- **The handlist-naming check's case-sensitivity is still a documented
  guess**, unchanged from this server's first release.
- **A handful of `symbols.json` entries are marked `low_confidence`**
  (surfaced in hover with a note), the bare symbol name was genuinely
  ambiguous enough that the independently-composed description might be
  wrong about what the symbol actually does.
