#!/usr/bin/env python3
"""End-to-end smoke test for openppl-ls over real LSP stdio framing.

Mirrors phh-ls/ohh-ls's lsp_smoke_test.py shape, now including a registry
directory argument; this server's first release took none, but now
loads openppl/registry/symbols.json.

Usage: python3 openppl-ls/tests/lsp_smoke_test.py <path-to-openppl-ls-binary> <registry-dir> <corpus-dir>
"""
import json
import subprocess
import sys


def encode(msg: dict) -> bytes:
    body = json.dumps(msg).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def read_message(stream) -> dict:
    headers = {}
    while True:
        line = stream.readline().decode("ascii")
        if line in ("\r\n", "\n", ""):
            break
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip()] = v.strip()
    length = int(headers["Content-Length"])
    body = stream.read(length)
    return json.loads(body)


def main():
    openppl_ls, registry_dir, corpus_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    import glob
    import os

    proc = subprocess.Popen(
        [openppl_ls, registry_dir],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    def send(msg):
        proc.stdin.write(encode(msg))
        proc.stdin.flush()

    failures = []

    def check(label, cond):
        status = "ok" if cond else "FAIL"
        print(f"[{status}] {label}")
        if not cond:
            failures.append(label)

    send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
    resp = read_message(proc.stdout)
    check("initialize responds with id=1", resp.get("id") == 1)
    caps = resp.get("result", {}).get("capabilities", {})
    check("capabilities advertise hoverProvider", caps.get("hoverProvider") is True)
    check("capabilities advertise completionProvider", "completionProvider" in caps)
    check("capabilities advertise full-document textDocumentSync", caps.get("textDocumentSync") == 1)

    send({"jsonrpc": "2.0", "method": "initialized", "params": {}})

    expected_zero = sorted(glob.glob(os.path.join(corpus_dir, "valid", "*.ohf")))
    expected_nonzero = sorted(glob.glob(os.path.join(corpus_dir, "invalid", "*.ohf")))

    for path in expected_zero + expected_nonzero:
        uri = "file://" + os.path.abspath(path)
        text = open(path, encoding="utf-8").read()
        send(
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didOpen",
                "params": {"textDocument": {"uri": uri, "languageId": "openppl", "version": 1, "text": text}},
            }
        )
        note = read_message(proc.stdout)
        check(f"{os.path.basename(path)}: publishDiagnostics received",
              note.get("method") == "textDocument/publishDiagnostics")
        diags = note.get("params", {}).get("diagnostics", [])
        if path in expected_zero:
            check(f"{os.path.basename(path)}: zero diagnostics", len(diags) == 0)
        else:
            check(f"{os.path.basename(path)}: at least one diagnostic", len(diags) >= 1)

    # Hover on structure-example.ohf's "hand$AA" (line 3, 0-indexed 2),
    # which should surface the pocket-pair description and, separately,
    # on the reserved "f$preflop" section header.
    sample = os.path.join(corpus_dir, "valid", "structure-example.ohf")
    uri = "file://" + os.path.abspath(sample)
    send(
        {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 12}},
        }
    )
    resp = read_message(proc.stdout)
    check("hover responds with id=2", resp.get("id") == 2)
    result = resp.get("result")
    check("hover on hand$AA returns non-null markdown content",
          result is not None and "markdown" == result.get("contents", {}).get("kind"))
    check("hover on hand$AA mentions pocket pair",
          result is not None and "pair" in result.get("contents", {}).get("value", "").lower())

    send(
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 5}},
        }
    )
    resp = read_message(proc.stdout)
    result = resp.get("result")
    check("hover on ##f$preflop## mentions 'Reserved section'",
          result is not None and "Reserved section" in result.get("contents", {}).get("value", ""))

    # Hover on a real registry symbol (line 4: "WHEN Others Fold FORCE" ->
    # "Others" is a real symbols.json entry, not a naming-convention
    # guess) should now return its actual registry meaning.
    send(
        {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 4, "character": 10}},
        }
    )
    resp = read_message(proc.stdout)
    result = resp.get("result")
    check("hover on a real registry symbol (Others) returns its registry meaning",
          result is not None and "evaluates true" in result.get("contents", {}).get("value", ""))

    # Completion includes the fixed keyword/reserved-section set, NOT a
    # registry-backed symbol list (there is none yet).
    send(
        {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}},
        }
    )
    resp = read_message(proc.stdout)
    items = resp.get("result", [])
    labels = {item["label"] for item in items}
    check("completion includes WHEN", "WHEN" in labels)
    check("completion includes FORCE", "FORCE" in labels)
    check("completion includes the reserved f$preflop section", "f$preflop" in labels)
    check("completion includes a real registry symbol (HaveFlushDraw)", "HaveFlushDraw" in labels)
    check("completion includes a real action (RaiseTo)", "RaiseTo" in labels)

    # didChange: introduce the handlist-naming-convention violation live.
    broken = open(sample, encoding="utf-8").read() + "\n##notAHandlist##\n    AA KK\n"
    send(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": broken}],
            },
        }
    )
    note = read_message(proc.stdout)
    diags = note.get("params", {}).get("diagnostics", [])
    check("didChange with a misnamed handlist section re-validates with a new diagnostic", len(diags) >= 1)
    check("didChange diagnostic is handlist-body-but-wrong-name",
          any(d.get("code") == "handlist-body-but-wrong-name" for d in diags))

    send({"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": uri}}})
    note = read_message(proc.stdout)
    check("didClose publishes an empty diagnostics list", note.get("params", {}).get("diagnostics") == [])

    send({"jsonrpc": "2.0", "id": 5, "method": "shutdown", "params": None})
    resp = read_message(proc.stdout)
    check("shutdown responds with id=5 and null result", resp.get("id") == 5 and resp.get("result") is None)
    send({"jsonrpc": "2.0", "method": "exit", "params": None})
    proc.wait(timeout=5)
    check("process exits cleanly after exit notification", proc.returncode == 0)

    print()
    if failures:
        print(f"{len(failures)} FAILURE(S):")
        for f in failures:
            print(" -", f)
        sys.exit(1)
    print("All checks passed.")


if __name__ == "__main__":
    main()
