#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

OUT_DIR="tests/stress_tests/output/ci_quick"
BASELINE="tests/stress_tests/output/baseline/auth_register_login_result.json"

mkdir -p "$OUT_DIR"

python3 tests/stress_tests/run_stress.py \
  --config tests/stress_tests/configs/auth_register_login.json \
  --output-dir "$OUT_DIR"

if [[ -f "$BASELINE" ]]; then
  python3 tests/stress_tests/compare_results.py \
    --baseline "$BASELINE" \
    --candidate "$OUT_DIR/latest/result.json" \
    --max-p95-regression 0.20 \
    --max-rps-drop 0.20
else
  echo "No baseline found at $BASELINE, skip compare step."
fi
