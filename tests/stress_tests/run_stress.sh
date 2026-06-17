#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STRESS_DIR="${ROOT_DIR}/tests/stress_tests"
cd "$ROOT_DIR"

MODE="local"
SCENARIO=""
CONFIG=""
EXTRA_ARGS=()
DOCKER_COMPOSE="docker compose -f tests/stress_tests/docker-compose.stress.yml"

usage() {
  cat <<'EOF'
Usage:
  run_stress.sh <scenario> [options]

Scenarios:
  auth_register_login | lobby_join_hot_room | e2e_short_conn
  battle_sync | full_flow

Options:
  --docker              Run in isolated Docker Compose environment
  --local               Run against local server (default)
  --ramp                Enable step ramp-up and recovery
  --ramp-config PATH    Ramp profile (default: configs/ramp_default.yaml)
  --netem RULE          tc netem rule, e.g. "delay 50ms 10ms loss 0.5%"
  --netem-dev DEV       Network device for netem
  --duration-seconds N  Override duration (non-ramp)
  --concurrency N       Override concurrency (non-ramp)
  --max-concurrency N   Cap ramp concurrency
  --threads N           Worker threads
  --quick               Short duration for smoke test (30s / ramp quick profile)
  -h, --help            Show help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    auth_register_login|lobby_join_hot_room|e2e_short_conn|battle_sync|full_flow)
      SCENARIO="$1"; shift ;;
    --docker) MODE="docker"; shift ;;
    --local) MODE="local"; shift ;;
    --ramp) EXTRA_ARGS+=(--ramp); shift ;;
    --ramp-config) EXTRA_ARGS+=(--ramp-config "$2"); shift 2 ;;
    --netem) EXTRA_ARGS+=(--netem "$2"); shift 2 ;;
    --netem-dev) EXTRA_ARGS+=(--netem-dev "$2"); shift 2 ;;
    --duration-seconds) EXTRA_ARGS+=(--duration-seconds "$2"); shift 2 ;;
    --concurrency) EXTRA_ARGS+=(--concurrency "$2"); shift 2 ;;
    --max-concurrency) EXTRA_ARGS+=(--max-concurrency "$2"); shift 2 ;;
    --threads) EXTRA_ARGS+=(--threads "$2"); shift 2 ;;
    --quick)
      EXTRA_ARGS+=(--duration-seconds 30 --concurrency 20)
      shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$SCENARIO" ]]; then
  echo "Scenario required." >&2
  usage
  exit 2
fi

CONFIG="tests/stress_tests/configs/${SCENARIO}.json"
if [[ ! -f "$CONFIG" ]]; then
  echo "Config not found: $CONFIG" >&2
  exit 2
fi

chmod +x tests/stress_tests/scripts/netem.sh 2>/dev/null || true

cleanup_docker() {
  if [[ "$MODE" == "docker" ]]; then
    $DOCKER_COMPOSE down --remove-orphans 2>/dev/null || true
  fi
}
trap cleanup_docker EXIT

if [[ "$MODE" == "docker" ]]; then
  if ! command -v docker &>/dev/null; then
    echo "docker not found; use --local or install Docker." >&2
    exit 2
  fi
  $DOCKER_COMPOSE build --quiet 2>/dev/null || $DOCKER_COMPOSE build
  $DOCKER_COMPOSE up -d server
  echo "Waiting for server ports..."
  for i in $(seq 1 30); do
    if bash -c 'echo >/dev/tcp/127.0.0.1/8765' 2>/dev/null; then
      break
    fi
    sleep 1
  done
  $DOCKER_COMPOSE run --rm \
    --cap-add=NET_ADMIN \
    runner python3 tests/stress_tests/run_stress.py \
      --config "$CONFIG" \
      "${EXTRA_ARGS[@]}"
else
  for port in 8765 8766; do
    if ! bash -c "echo >/dev/tcp/127.0.0.1/${port}" 2>/dev/null; then
      echo "WARNING: port ${port} not reachable. Start server first:"
      echo "  ./build/server --config config/server.json"
    fi
  done
  python3 tests/stress_tests/run_stress.py \
    --config "$CONFIG" \
    "${EXTRA_ARGS[@]}"
fi
