#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary="$root/build-local-static/theta-spxw-filter"
output="$root/dist/theta-spxw-filter-linux-amd64.tar.gz"

if [[ ! -x "$binary" ]]; then
  echo "missing built binary: $binary" >&2
  exit 1
fi

mkdir -p "$root/dist"
stage=$(mktemp -d "$root/dist/.stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/bin" "$stage/scripts"
install -m 0755 "$binary" "$stage/bin/theta-spxw-filter"
install -m 0755 "$root/scripts/start-on-majula.sh" "$stage/scripts/start-on-majula.sh"
install -m 0644 "$root/README.runtime.md" "$stage/README.md"

python3 - "$stage/manifest.json" "$stage/bin/theta-spxw-filter" "$root" <<'PY'
import hashlib
import json
import subprocess
import sys
from pathlib import Path

manifest_path = Path(sys.argv[1])
binary = Path(sys.argv[2])
source_root = Path(sys.argv[3])
def call(args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False).stdout
source_sha = call(["git", "-C", str(source_root), "rev-parse", "HEAD"]).strip()
manifest = {
    "artifact": binary.name,
    "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    "source_git_sha": source_sha if source_sha else None,
    "file": call(["file", "-b", str(binary)]).strip(),
    "ldd": call(["ldd", str(binary)]).strip(),
    "readelf_dynamic": call(["readelf", "-d", str(binary)]).strip(),
    "target": "Debian 12 x86_64",
    "source_included": False,
}
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
PY

tar -C "$stage" -czf "$output" .
sha256sum "$output" > "$output.sha256"
printf 'package=%s\nsha256=%s\n' "$output" "$(cut -d' ' -f1 "$output.sha256")"
