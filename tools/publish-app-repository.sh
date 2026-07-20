#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repository_root="${1:-"$project_root/../app-repository"}"
app_dir="$repository_root/net.scheffers.neiro"
metadata="$app_dir/metadata.json"
binary="$project_root/build/tanmatsu/application.bin"

if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [app-repository]" >&2
    exit 2
fi

if [[ ! -f "$metadata" ]]; then
    echo "Neiro metadata not found: $metadata" >&2
    exit 1
fi

make -C "$project_root" build

if [[ ! -f "$binary" ]]; then
    echo "Build succeeded but application binary is missing: $binary" >&2
    exit 1
fi

cp "$binary" "$app_dir/application.bin"

python3 - "$metadata" <<'PY'
import json
import os
import re
import sys
import tempfile
from pathlib import Path

metadata_path = Path(sys.argv[1])
data = json.loads(metadata_path.read_text())

match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", data.get("version", ""))
if match is None:
    raise SystemExit(f"Unsupported version in {metadata_path}: {data.get('version')!r}")

major, minor, patch = map(int, match.groups())
old_version = data["version"]
data["version"] = f"{major}.{minor}.{patch + 1}"

applications = [
    app
    for app in data.get("application", [])
    if app.get("executable") == "application.bin"
]
if len(applications) != 1 or not isinstance(applications[0].get("revision"), int):
    raise SystemExit(f"Expected one application.bin entry with an integer revision in {metadata_path}")

application = applications[0]
old_revision = application["revision"]
application["revision"] += 1

fd, temporary_name = tempfile.mkstemp(dir=metadata_path.parent, prefix=".metadata.", text=True)
try:
    with os.fdopen(fd, "w") as temporary:
        json.dump(data, temporary, indent=4)
        temporary.write("\n")
    os.replace(temporary_name, metadata_path)
except BaseException:
    try:
        os.unlink(temporary_name)
    except FileNotFoundError:
        pass
    raise

print(f"Neiro {old_version} -> {data['version']}; revision {old_revision} -> {application['revision']}")
PY

echo "Published $binary to $app_dir"
