#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

"""Syntax-check the inline JavaScript in the console.

The whole console UI is one HTML file, compiled verbatim into the plugin's
BinaryData. Nothing in the C++ build looks inside it, so a JavaScript syntax error
is invisible until run time -- and it does not degrade the interface, it deletes
it: the script never executes and the panel comes up blank.

That is not hypothetical. A licence-header sweep once replaced a copyright line
inside a translated string and wrote a real newline where the two-character escape
belonged. A JavaScript string cannot span a line, so the console's main script
stopped parsing, and the product shipped with an empty window until somebody
happened to parse the file by hand.

Requires node on PATH (only for `--check`; nothing is executed).

    python tools/dev/check_console_js.py [path/to/console.html]

Exit code 0 if every block parses, 1 otherwise.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

DEFAULT = pathlib.Path(__file__).resolve().parents[2] / "docs" / "mockups" / "console.html"

# <script> blocks that have their own body. A block with a src= attribute has
# nothing inline to check.
SCRIPT = re.compile(r"<script(?![^>]*\bsrc=)[^>]*>(.*?)</script>", re.S)


def main(argv: list[str]) -> int:
    path = pathlib.Path(argv[1]) if len(argv) > 1 else DEFAULT
    if not path.is_file():
        print(f"not found: {path}", file=sys.stderr)
        return 1

    node = shutil.which("node")
    if node is None:
        print("node not found on PATH; skipping the console syntax check",
              file=sys.stderr)
        return 0

    source = path.read_text(encoding="utf-8")
    blocks = list(SCRIPT.finditer(source))
    if not blocks:
        print(f"{path}: no inline <script> blocks found -- has the file moved?",
              file=sys.stderr)
        return 1

    failures = 0
    for index, match in enumerate(blocks):
        # The line the block starts on, so an error can be found in the real file
        # rather than in a temporary copy of one fragment of it.
        first_line = source.count("\n", 0, match.start(1)) + 1

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False,
                                         encoding="utf-8") as handle:
            handle.write(match.group(1))
            temp = handle.name

        result = subprocess.run([node, "--check", temp], capture_output=True)
        pathlib.Path(temp).unlink(missing_ok=True)

        if result.returncode != 0:
            failures += 1
            message = result.stderr.decode("utf-8", "replace")
            # node reports lines within the fragment; offset them into the file.
            message = re.sub(r"^.*\.js:(\d+)$",
                             lambda m: f"{path}:{first_line + int(m.group(1)) - 1}",
                             message, count=1, flags=re.M)
            print(f"\n--- inline script {index} (starts at {path}:{first_line}) ---")
            print(message[:4000], file=sys.stderr)

    total = len(blocks)
    if failures:
        print(f"\n{failures} of {total} inline script blocks failed to parse",
              file=sys.stderr)
        return 1

    print(f"{path.name}: {total} inline script blocks parse cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
