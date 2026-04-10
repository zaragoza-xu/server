#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

FAILED_SCENARIOS=()
THREAD_ARGS=()

usage() {
  cat <<'EOF'
Usage:
  bash tests/stress_tests/run_all.sh [--threads N]

Options:
  --threads N   Override thread count for all scenarios
  -h, --help    Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --threads)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --threads" >&2
        usage
        exit 2
      fi
      THREAD_ARGS=(--threads "$2")
      shift 2
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
  if python3 tests/stress_tests/run_stress.py --config "$config" "${THREAD_ARGS[@]}"; then
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
