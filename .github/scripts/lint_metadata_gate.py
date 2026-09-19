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

This gate keeps the metadata check meaningful: it fails CI on ANY error-level
rule failure other than the single accepted LP012 deviation, so genuine metadata
regressions still break the build.

Usage:
    python3 lint_metadata_gate.py <arduino-lint-json-report>
"""

from __future__ import annotations

import json
import sys

# Rule IDs whose ERROR-level failure is an accepted, documented deviation.
ACCEPTED_ERROR_IDS = {"LP012"}


def collect_error_failures(report):
    projects = report.get("projects", []) if isinstance(report, dict) else report
    accepted = []
    unexpected = []
    for project in projects or []:
        for rule in project.get("rules", []):
            if rule.get("result") == "fail" and rule.get("level") == "ERROR":
                if rule.get("ID") in ACCEPTED_ERROR_IDS:
                    accepted.append(rule)
                else:
                    unexpected.append(rule)
    return accepted, unexpected


def main(argv):
    if len(argv) != 2:
        print("usage: lint_metadata_gate.py <arduino-lint-json-report>", file=sys.stderr)
        return 2

    with open(argv[1], encoding="utf-8") as handle:
        report = json.load(handle)

    accepted, unexpected = collect_error_failures(report)

    for rule in accepted:
        print(f"::notice::arduino-lint {rule.get('ID')} accepted (documented "
              f"deviation): {rule.get('brief')}")

    for rule in unexpected:
        print(f"::error::arduino-lint {rule.get('ID')} - {rule.get('brief')}: "
              f"{rule.get('message')}")

    if unexpected:
        print(f"FAILED: {len(unexpected)} unexpected metadata error(s).")
        return 1

    print("Metadata OK: only the documented LP012 deviation is present "
          f"({len(accepted)} accepted error(s)).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
