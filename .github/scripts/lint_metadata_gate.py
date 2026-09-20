#!/usr/bin/env python3
"""Gate arduino-lint JSON output for ArduinoAwait's metadata.

arduino-lint rule LP012 always fails for this project because the frozen public
name "ArduinoAwait" starts with the reserved "Arduino" prefix. That prefix is an
intentional, documented deviation tied to the project's fixed public identity
(see docs/arduinoawait/V1_API_CONTRACT.md) and to a separate, future Arduino
Library Manager publication decision. There is no arduino-lint compliance level
that passes a name with this prefix, and we deliberately do NOT enable arduino-
lint "official" mode (that would falsely designate the library as an official
Arduino project).

This gate keeps the metadata check meaningful AND fails closed:

  * it fails on ANY error-level rule failure other than the accepted LP012
    deviation, so genuine metadata regressions still break the build; and
  * it rejects a malformed, empty, or non-library report instead of silently
    passing, so a crashed/short-circuited linter cannot certify the metadata; and
  * it rejects unknown rule result/level values and a report whose summary
    errorCount disagrees with the inspected error-level failures, so a
    schema-invalid or internally inconsistent report cannot slip through.

A legitimately clean library project has no failing rules; that is accepted.
LP012 is NOT required to be present (only tolerated when it is).

Usage:
    python3 lint_metadata_gate.py <arduino-lint-json-report>
"""

from __future__ import annotations

import json
import sys

# Rule IDs whose ERROR-level failure is an accepted, documented deviation.
ACCEPTED_ERROR_IDS = {"LP012"}

# Schema enums for the pinned arduino-lint 1.3.0 JSON output. Unknown values are
# treated as a malformed report and fail closed, rather than silently bypassing
# the exact "fail"/"ERROR" comparisons below (e.g. "FAIL" or "error").
_ALLOWED_RESULTS = {"pass", "fail", "skipped", "unable to run"}
_ALLOWED_LEVELS = {"INFO", "WARNING", "ERROR", "NOTICE"}


def _error(message):
    print(f"::error::{message}")


def evaluate(report):
    """Return a process exit code for the parsed arduino-lint *report*.

    Fails closed: structural problems, unknown schema values, an internally
    inconsistent error count, or the absence of an audited library project are
    all treated as failures, not as an implicit pass.
    """
    if not isinstance(report, dict):
        _error("arduino-lint report is not a JSON object")
        return 1

    projects = report.get("projects")
    if not isinstance(projects, list) or not projects:
        _error("arduino-lint report has no projects to evaluate")
        return 1

    summary = report.get("summary")
    if not isinstance(summary, dict):
        _error("arduino-lint report has no summary object")
        return 1
    reported_errors = summary.get("errorCount")
    # bool is a subclass of int; reject it explicitly.
    if not isinstance(reported_errors, int) or isinstance(reported_errors, bool):
        _error("arduino-lint report summary is missing an integer errorCount")
        return 1

    saw_library = False
    accepted = []
    unexpected = []

    for project in projects:
        if not isinstance(project, dict):
            _error("arduino-lint report contains a malformed project entry")
            return 1
        if project.get("projectType") == "library":
            saw_library = True

        rules = project.get("rules")
        if not isinstance(rules, list):
            _error("arduino-lint project is missing a rules array")
            return 1

        for rule in rules:
            if not isinstance(rule, dict):
                _error("arduino-lint report contains a malformed rule entry")
                return 1
            rule_id = rule.get("ID")
            result = rule.get("result")
            level = rule.get("level")
            if not (isinstance(rule_id, str) and rule_id):
                _error("arduino-lint rule record has a missing or empty ID")
                return 1
            if result not in _ALLOWED_RESULTS:
                _error(f"arduino-lint rule {rule_id} has an unknown result value: {result!r}")
                return 1
            if level not in _ALLOWED_LEVELS:
                _error(f"arduino-lint rule {rule_id} has an unknown level value: {level!r}")
                return 1
            if result == "fail" and level == "ERROR":
                if rule_id in ACCEPTED_ERROR_IDS:
                    accepted.append(rule)
                else:
                    unexpected.append(rule)

    if not saw_library:
        _error("arduino-lint report audited no library project")
        return 1

    total_errors = len(accepted) + len(unexpected)
    if total_errors != reported_errors:
        _error("arduino-lint report is internally inconsistent: summary "
               f"errorCount={reported_errors} but {total_errors} error-level "
               "failure(s) were found")
        return 1

    for rule in accepted:
        print(f"::notice::arduino-lint {rule.get('ID')} accepted (documented "
              f"deviation): {rule.get('brief')}")

    for rule in unexpected:
        _error(f"arduino-lint {rule.get('ID')} - {rule.get('brief')}: "
               f"{rule.get('message')}")

    if unexpected:
        print(f"FAILED: {len(unexpected)} unexpected metadata error(s).")
        return 1

    print("Metadata OK: audited a library project; report is internally "
          f"consistent; only documented deviations present ({len(accepted)} "
          "accepted error(s)).")
    return 0


def main(argv):
    if len(argv) != 2:
        print("usage: lint_metadata_gate.py <arduino-lint-json-report>",
              file=sys.stderr)
        return 2

    try:
        with open(argv[1], encoding="utf-8") as handle:
            report = json.load(handle)
    except (OSError, ValueError) as exc:
        _error(f"could not read arduino-lint report '{argv[1]}': {exc}")
        return 1

    return evaluate(report)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
