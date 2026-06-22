#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

package_name="${PACKAGE_NAME:-server}"
version="${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo local)}"
system_name="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"

dist_dir="$ROOT_DIR/dist"
stage_name="${package_name}-${version}-${system_name}-${arch}"
stage_dir="$dist_dir/$stage_name"
archive="$dist_dir/${stage_name}.tar.gz"

cmake --preset release
cmake --build --preset release

rm -rf "$stage_dir" "$archive"
cmake --install build --prefix "$stage_dir"

tar -C "$dist_dir" -czf "$archive" "$stage_name"

echo "release package: $archive"
