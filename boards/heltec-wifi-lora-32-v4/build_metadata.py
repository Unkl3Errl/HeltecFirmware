"""Resolve deterministic build identity for the Heltec V4 firmware."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import subprocess
from typing import Mapping, Sequence


RELEASE_TAG = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
SAFE_VALUE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z._+-]*$")
MAX_VERSION_LENGTH = 32
MAX_COMMIT_LENGTH = 48


@dataclass(frozen=True)
class BuildMetadata:
    version: str
    commit: str


def _safe_value(label: str, value: str, maximum: int) -> str:
    value = value.strip()
    if not value or len(value) > maximum or not SAFE_VALUE.fullmatch(value):
        raise ValueError(
            f"{label} must be 1-{maximum} characters using letters, digits, '.', '_', '+', or '-'"
        )
    return value


def release_version(tag: str) -> str | None:
    match = RELEASE_TAG.fullmatch(tag.strip())
    if not match:
        return None
    return ".".join(match.groups())


def select_version(
    explicit: str | None,
    github_ref_type: str | None,
    github_ref_name: str | None,
    exact_tags: Sequence[str],
) -> str:
    if explicit and explicit.strip():
        value = explicit.strip()
        value = release_version(value) or value
        return _safe_value("firmware version", value, MAX_VERSION_LENGTH)

    if github_ref_type == "tag" and github_ref_name:
        value = release_version(github_ref_name)
        if value:
            return value

    releases = []
    for tag in exact_tags:
        value = release_version(tag)
        if value:
            releases.append((tuple(int(part) for part in value.split(".")), value))
    if releases:
        return max(releases)[1]
    return "dev"


def select_commit(
    explicit: str | None,
    github_sha: str | None,
    git_sha: str | None,
    dirty: bool,
) -> str:
    candidate = explicit or github_sha or git_sha or "unknown"
    candidate = candidate.strip()
    if re.fullmatch(r"[0-9a-fA-F]{13,64}", candidate):
        candidate = candidate[:12].lower()
    candidate = _safe_value("firmware commit", candidate, MAX_COMMIT_LENGTH)
    if dirty and explicit is None and github_sha is None and candidate != "unknown":
        candidate = f"{candidate}-dirty"
    return _safe_value("firmware commit", candidate, MAX_COMMIT_LENGTH)


def _git(project_dir: Path, *args: str) -> str | None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(project_dir), *args],
            check=True,
            capture_output=True,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return completed.stdout.strip()


def resolve_build_metadata(
    project_dir: Path,
    environ: Mapping[str, str] | None = None,
) -> BuildMetadata:
    values = os.environ if environ is None else environ
    tags_output = _git(project_dir, "tag", "--points-at", "HEAD") or ""
    git_sha = _git(project_dir, "rev-parse", "HEAD")
    dirty_output = _git(project_dir, "status", "--porcelain")
    dirty = bool(dirty_output)
    version = select_version(
        values.get("HELTEC_FIRMWARE_VERSION"),
        values.get("GITHUB_REF_TYPE"),
        values.get("GITHUB_REF_NAME"),
        tags_output.splitlines(),
    )
    commit = select_commit(
        values.get("HELTEC_FIRMWARE_COMMIT"),
        values.get("GITHUB_SHA"),
        git_sha,
        dirty,
    )
    return BuildMetadata(version=version, commit=commit)
