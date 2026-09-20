#!/usr/bin/env python3
"""Self-test for .github/scripts/lint_metadata_gate.py.

Verifies the metadata gate:
  * accepts a report whose only error is the documented LP012 deviation, and a
    genuinely clean library;
  * fails closed on unexpected errors, malformed/empty/non-library reports,
    unknown rule result/level values, a missing/invalid summary, and a summary
    errorCount that disagrees with the inspected failures.

Runs under CTest.
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


def _count_errors(projects):
    total = 0
    for project in projects:
        if isinstance(project, dict):
            rules = project.get("rules")
            if isinstance(rules, list):
                for rule in rules:
                    if (isinstance(rule, dict)
                            and rule.get("result") == "fail"
                            and rule.get("level") == "ERROR"):
                        total += 1
    return total


def _report(projects, error_count=None, warning_count=0):
    """Wrap *projects* with a summary; errorCount defaults to a consistent value."""
    if error_count is None:
        error_count = _count_errors(projects)
    return {
        "projects": projects,
        "summary": {
            "pass": error_count == 0,
            "warningCount": warning_count,
            "errorCount": error_count,
        },
    }


_SKETCH = {"projectType": "sketch", "path": "/x/examples/Empty", "rules": []}
_LP012 = {"ID": "LP012", "result": "fail", "level": "ERROR",
          "brief": "name starts with Arduino"}
_OTHER_ERROR = {"ID": "LP001", "result": "fail", "level": "ERROR", "brief": "x"}
_WARNING = {"ID": "LP027", "result": "fail", "level": "WARNING", "brief": "x"}


def main():
    cases = [
        # name, expected_exit, factory

        # Accepted.
        ("lp012-only-plus-warnings", 0,
         lambda: _run_json(_report([_library([_LP012, _WARNING]), _SKETCH],
                                   warning_count=1))),
        ("clean-library-no-failures", 0,
         lambda: _run_json(_report([_library([]), _SKETCH]))),

        # Unexpected error.
        ("unexpected-error-fails", 1,
         lambda: _run_json(_report([_library([_LP012, _OTHER_ERROR])]))),

        # Structural fail-closed.
        ("empty-object-fails-closed", 1, lambda: _run_json({})),
        ("empty-projects-fails-closed", 1, lambda: _run_json({"projects": []})),
        ("null-projects-fails-closed", 1, lambda: _run_json({"projects": None})),
        ("no-library-project-fails", 1, lambda: _run_json(_report([_SKETCH]))),
        ("project-not-dict-fails", 1, lambda: _run_json(_report([123]))),
        ("rules-not-list-fails", 1,
         lambda: _run_json(_report([{"projectType": "library", "rules": "x"}]))),
        ("malformed-rule-missing-level-fails", 1,
         lambda: _run_json(_report([_library([{"ID": "LP011", "result": "fail"}])]))),
        ("invalid-json-fails-closed", 1, lambda: _run_text("{ this is not json")),
        ("top-level-array-fails-closed", 1, lambda: _run_text("[]")),

        # Schema-value fail-closed.
        ("uppercase-result-fails", 1,
         lambda: _run_json(_report(
             [_library([{"ID": "LP011", "result": "FAIL", "level": "ERROR"}])]))),
        ("lowercase-level-fails", 1,
         lambda: _run_json(_report(
             [_library([{"ID": "LP011", "result": "fail", "level": "error"}])]))),
        ("empty-result-fails", 1,
         lambda: _run_json(_report(
             [_library([{"ID": "LP011", "result": "", "level": "ERROR"}])]))),
        ("non-string-result-fails", 1,
         lambda: _run_json(_report(
             [_library([{"ID": "LP011", "result": ["fail"], "level": "ERROR"}])]))),
        ("non-string-level-fails", 1,
         lambda: _run_json(_report(
             [_library([{"ID": "LP011", "result": "fail", "level": {}}])]))),
        ("empty-id-fails", 1,
         lambda: _run_json(_report(
             [_library([{"ID": "", "result": "fail", "level": "WARNING"}])]))),

        # Summary fail-closed.
        ("missing-summary-fails", 1,
         lambda: _run_json({"projects": [_library([])]})),
        ("non-int-errorcount-fails", 1,
         lambda: _run_json({"projects": [_library([])],
                            "summary": {"pass": True, "warningCount": 0,
                                        "errorCount": "1"}})),
        ("inconsistent-summary-fails", 1,
         lambda: _run_json({"projects": [_library([])],
                            "summary": {"pass": False, "warningCount": 0,
                                        "errorCount": 1}})),
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
    print(f"gate self-test: all {len(cases)} cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
