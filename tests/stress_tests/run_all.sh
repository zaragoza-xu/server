#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

FAILED_SCENARIOS=()
THREAD_ARGS=()
EXTRA_ARGS=()

usage() {
  cat <<'EOF'
Usage:
  bash tests/stress_tests/run_all.sh [--threads N] [--quick] [--ramp]

Options:
  --threads N   Override thread count for all scenarios
  --quick       Short duration smoke (30s, lower concurrency)
  --ramp        Enable ramp mode via run_stress.sh
  -h, --help    Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads)
      THREAD_ARGS=(--threads "$2")
      shift 2
      ;;
    --quick)
      EXTRA_ARGS+=(--duration-seconds 30 --concurrency 20)
      shift
      ;;
    --ramp)
      EXTRA_ARGS+=(--ramp --ramp-config tests/stress_tests/configs/ramp_quick.yaml --max-concurrency 30)
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

run_scenario() {
  local name="$1"
  local config="$2"
  echo "=== Running ${name} ==="
  if python3 tests/stress_tests/run_stress.py --config "$config" "${THREAD_ARGS[@]}" "${EXTRA_ARGS[@]}"; then
    echo "=== PASS ${name} ==="
  else
    local code=$?
    echo "=== FAIL ${name} (exit=${code}) ==="
    FAILED_SCENARIOS+=("${name}(exit=${code})")
  fi
}

run_scenario "auth_register_login" "tests/stress_tests/configs/auth_register_login.json"
run_scenario "lobby_join_hot_room" "tests/stress_tests/configs/lobby_join_hot_room.json"
run_scenario "e2e_short_conn" "tests/stress_tests/configs/e2e_short_conn.json"
run_scenario "battle_sync" "tests/stress_tests/configs/battle_sync_quick.json"
run_scenario "full_flow" "tests/stress_tests/configs/full_flow_quick.json"

echo "=== Summary ==="
if ((${#FAILED_SCENARIOS[@]} == 0)); then
  echo "All stress scenarios passed."
  exit 0
fi

echo "Failed scenarios:"
for item in "${FAILED_SCENARIOS[@]}"; do
  echo " - ${item}"
done
exit 1
