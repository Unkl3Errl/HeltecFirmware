"""PlatformIO pre-build hook for deterministic firmware metadata."""

from pathlib import Path
import sys

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
metadata_dir = project_dir / "boards" / "heltec-wifi-lora-32-v4"
sys.path.insert(0, str(metadata_dir))

from build_metadata import resolve_build_metadata  # noqa: E402


metadata = resolve_build_metadata(project_dir)
env.AppendUnique(
    CPPDEFINES=[
        ("BRUCE_VERSION", f'\\"{metadata.version}\\"'),
        ("GIT_COMMIT_HASH", f'\\"{metadata.commit}\\"'),
    ]
)
print(f"FIRMWARE BUILD METADATA: version={metadata.version} commit={metadata.commit}")
