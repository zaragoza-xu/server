#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SERVER_BIN="${SERVER_BIN:-./build/server}"
CONFIG_PATH="${CONFIG_PATH:-config/server.json}"

args=(--config "$CONFIG_PATH")
has_duration=false
for arg in "$@"; do
  if [[ "$arg" == "--duration-seconds" || "$arg" == --duration-seconds=* ]]; then
    has_duration=true
    break
  fi
done

if [[ "$has_duration" == false ]]; then
  args+=(--duration-seconds 5)
fi

exec "$SERVER_BIN" "${args[@]}" "$@"
