#!/usr/bin/env python3
from pathlib import Path
import sys
import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github" / "workflows"


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def workflow(path: Path):
    data = yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
    if not isinstance(data, dict):
        fail(f"{path} is not a YAML mapping")
    if "on" not in data:
        fail(f"{path} has no on trigger")
    if "jobs" not in data or not isinstance(data["jobs"], dict):
        fail(f"{path} has no jobs mapping")
    return data


def main() -> int:
    ci = workflow(WORKFLOWS / "ci.yml")
    release = workflow(WORKFLOWS / "release.yml")

    ci_events = ci["on"]
    if not all(event in ci_events for event in ("push", "pull_request", "workflow_dispatch")):
        fail("ci.yml must trigger on push, pull_request, and workflow_dispatch")
    for job_name in ("host-validation", "firmware-build"):
        if job_name not in ci["jobs"]:
            fail(f"ci.yml missing job: {job_name}")
    if "needs" not in ci["jobs"]["firmware-build"]:
        fail("firmware-build must depend on host-validation")

    release_events = release["on"]
    if "push" not in release_events or "tags" not in release_events["push"]:
        fail("release.yml must trigger on pushed version tags")
    if release["permissions"].get("contents") != "write":
        fail("release.yml must grant contents: write")
    for job_name in ("validate-tag", "build-release"):
        if job_name not in release["jobs"]:
            fail(f"release.yml missing job: {job_name}")
    if "needs" not in release["jobs"]["build-release"]:
        fail("build-release must depend on validate-tag")

    release_text = (WORKFLOWS / "release.yml").read_text(encoding="utf-8")
    for marker in ("esp-idf-ci-action@v1", "gh release create", "SHA256SUMS", "v[0-9]+\\.[0-9]+\\.[0-9]+"):
        if marker not in release_text:
            fail(f"release.yml is missing required marker: {marker}")

    print("GitHub Actions workflow validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
