#!/usr/bin/env bash
# Apply or clear tc/netem rules on a network device.
# Default (empty NETEM_RULE): remove all shaping — no interference.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  netem.sh apply [--dev IFACE] [--rule "delay 50ms 10ms loss 0.5%"]
  netem.sh clear [--dev IFACE]
  netem.sh status [--dev IFACE]

Environment:
  NETEM_DEV   Network device (auto-detected when omitted)
  NETEM_RULE  netem rule string; empty means clear/no interference
EOF
}

detect_dev() {
  if [[ -n "${NETEM_DEV:-}" ]]; then
    echo "$NETEM_DEV"
    return
  fi
  local route_dev
  route_dev="$(ip route show default 2>/dev/null | awk '/default/ {print $5; exit}')"
  if [[ -n "$route_dev" ]]; then
    echo "$route_dev"
    return
  fi
  echo "eth0"
}

clear_dev() {
  local dev="$1"
  if tc qdisc show dev "$dev" 2>/dev/null | grep -q 'qdisc netem'; then
    tc qdisc del dev "$dev" root 2>/dev/null || true
  fi
}

apply_rule() {
  local dev="$1"
  local rule="$2"
  clear_dev "$dev"
  if [[ -z "$rule" ]]; then
    echo "netem: no rule — cleared (no interference) on $dev"
    return 0
  fi
  # shellcheck disable=SC2086
  tc qdisc add dev "$dev" root netem $rule
  echo "netem: applied '$rule' on $dev"
}

show_status() {
  local dev="$1"
  echo "=== tc qdisc show dev $dev ==="
  tc qdisc show dev "$dev" 2>/dev/null || echo "(none or no permission)"
}

ACTION="${1:-status}"
shift || true
DEV=""
RULE="${NETEM_RULE:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dev) DEV="$2"; shift 2 ;;
    --rule) RULE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

DEV="${DEV:-$(detect_dev)}"

case "$ACTION" in
  apply) apply_rule "$DEV" "$RULE" ;;
  clear) clear_dev "$DEV"; echo "netem: cleared on $DEV" ;;
  status) show_status "$DEV" ;;
  *) echo "Unknown action: $ACTION" >&2; usage; exit 2 ;;
esac
