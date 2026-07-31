#!/usr/bin/env python3
"""Require approved repository guards on every publishing-capable Actions job."""

from __future__ import annotations

import re
import sys
from pathlib import Path


UPSTREAM_GUARD = "github.repository == 'vladelaina/Catime'"
FORK_RELEASE_GUARD = "github.repository == 'Whittebz/Catime'"
WORKFLOW_DIRECTORY = Path(__file__).resolve().parents[2] / "workflows"

# Third-party credentials and package publishing remain upstream-only. GitHub
# releases may also target the named integration fork when explicitly guarded.
UPSTREAM_ONLY_MARKERS = (
    "${{ secrets.",
    "signpath/github-action-submit-signing-request",
    "winget-releaser",
    "choco push",
    "build-store-package.ps1",
    "./.github/workflows/signpath-sign.yml",
    "./.github/workflows/chocolatey.yml",
)
RELEASE_MARKERS = (
    "softprops/action-gh-release",
    "gh release create",
)


def extract_jobs(text: str) -> list[tuple[str, str]]:
    lines = text.splitlines()
    jobs_start = next(
        (index for index, line in enumerate(lines) if re.fullmatch(r"jobs:\s*", line)),
        None,
    )
    if jobs_start is None:
        return []

    headings: list[tuple[int, str]] = []
    for index in range(jobs_start + 1, len(lines)):
        line = lines[index]
        if line and not line.startswith((" ", "\t", "#")):
            break
        match = re.fullmatch(r"  ([A-Za-z0-9_-]+):\s*", line)
        if match:
            headings.append((index, match.group(1)))

    jobs: list[tuple[str, str]] = []
    for position, (start, name) in enumerate(headings):
        end = headings[position + 1][0] if position + 1 < len(headings) else len(lines)
        jobs.append((name, "\n".join(lines[start:end])))
    return jobs


def main() -> int:
    failures: list[str] = []
    checked_jobs = 0

    workflow_paths = sorted(WORKFLOW_DIRECTORY.glob("*.yml"))
    workflow_paths.extend(sorted(WORKFLOW_DIRECTORY.glob("*.yaml")))

    for path in workflow_paths:
        text = path.read_text(encoding="utf-8")
        for job_name, job_text in extract_jobs(text):
            upstream_only = [marker for marker in UPSTREAM_ONLY_MARKERS if marker in job_text]
            release = [marker for marker in RELEASE_MARKERS if marker in job_text]
            if not upstream_only and not release:
                continue

            checked_jobs += 1
            if upstream_only and UPSTREAM_GUARD not in job_text:
                marker_list = ", ".join(upstream_only)
                failures.append(
                    f"{path.relative_to(WORKFLOW_DIRECTORY.parent.parent)}: "
                    f"job '{job_name}' lacks upstream guard; matched {marker_list}"
                )
            elif release and not any(
                guard in job_text for guard in (UPSTREAM_GUARD, FORK_RELEASE_GUARD)
            ):
                marker_list = ", ".join(release)
                failures.append(
                    f"{path.relative_to(WORKFLOW_DIRECTORY.parent.parent)}: "
                    f"job '{job_name}' lacks an approved release guard; matched {marker_list}"
                )

    if failures:
        print("Publishing guard policy failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        print(
            f"Approved guards: {UPSTREAM_GUARD} or {FORK_RELEASE_GUARD}",
            file=sys.stderr,
        )
        return 1

    print(f"Publishing guard policy passed for {checked_jobs} sensitive jobs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
