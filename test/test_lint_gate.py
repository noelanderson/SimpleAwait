#!/usr/bin/env python3
"""Self-test for .github/scripts/lint_metadata_gate.py.

Verifies the metadata gate accepts a report whose only error is the documented
LP012 deviation (and a genuinely clean library), and fails closed on unexpected
errors and on malformed/empty/non-library reports. Runs under CTest.
"""

import json
import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", ".github", "scripts"))

import lint_metadata_gate as gate  # noqa: E402


def _run_json(obj):
    handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
    try:
        json.dump(obj, handle)
        handle.close()
        return gate.main(["gate", handle.name])
    finally:
        os.unlink(handle.name)


def _run_text(text):
    handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
    try:
        handle.write(text)
        handle.close()
        return gate.main(["gate", handle.name])
    finally:
        os.unlink(handle.name)


def _library(rules):
    return {"projectType": "library", "path": "/x", "rules": rules}


_SKETCH = {"projectType": "sketch", "path": "/x/examples/Empty", "rules": []}
_LP012 = {"ID": "LP012", "result": "fail", "level": "ERROR",
          "brief": "name starts with Arduino"}
_OTHER_ERROR = {"ID": "LP001", "result": "fail", "level": "ERROR", "brief": "x"}
_WARNING = {"ID": "LP027", "result": "fail", "level": "WARNING", "brief": "x"}


def main():
    cases = [
        # name, expected_exit, factory
        ("lp012-only-plus-warnings", 0,
         lambda: _run_json({"projects": [_library([_LP012, _WARNING]), _SKETCH]})),
        ("clean-library-no-failures", 0,
         lambda: _run_json({"projects": [_library([]), _SKETCH]})),
        ("unexpected-error-fails", 1,
         lambda: _run_json({"projects": [_library([_LP012, _OTHER_ERROR])]})),
        ("empty-object-fails-closed", 1,
         lambda: _run_json({})),
        ("empty-projects-fails-closed", 1,
         lambda: _run_json({"projects": []})),
        ("null-projects-fails-closed", 1,
         lambda: _run_json({"projects": None})),
        ("no-library-project-fails", 1,
         lambda: _run_json({"projects": [_SKETCH]})),
        ("project-not-dict-fails", 1,
         lambda: _run_json({"projects": [123]})),
        ("rules-not-list-fails", 1,
         lambda: _run_json({"projects": [{"projectType": "library", "rules": "x"}]})),
        ("malformed-rule-missing-level-fails", 1,
         lambda: _run_json({"projects": [_library([{"ID": "LP011", "result": "fail"}])]})),
        ("invalid-json-fails-closed", 1,
         lambda: _run_text("{ this is not json")),
        ("top-level-array-fails-closed", 1,
         lambda: _run_text("[]")),
    ]

    failures = 0
    for name, expected, factory in cases:
        got = factory()
        status = "OK" if got == expected else "FAIL"
        if got != expected:
            failures += 1
        print(f"[{status}] {name}: exit={got} (want {expected})")

    if failures:
        print(f"{failures} gate self-test case(s) failed")
        return 1
    print("gate self-test: all cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
