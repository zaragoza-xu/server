#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare stress reports for regression gate")
    parser.add_argument("--baseline", required=True, help="Baseline result.json path")
    parser.add_argument("--candidate", required=True, help="Candidate result.json path")
    parser.add_argument("--max-p95-regression", type=float, default=0.20, help="Max allowed p95 regression ratio")
    parser.add_argument("--max-rps-drop", type=float, default=0.20, help="Max allowed throughput drop ratio")
    return parser.parse_args()


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> None:
    args = parse_args()
    b = _load(Path(args.baseline))
    c = _load(Path(args.candidate))

    b_p95 = float(b["latency_ms"]["p95"])
    c_p95 = float(c["latency_ms"]["p95"])
    b_rps = float(b["requests"]["throughput_rps"])
    c_rps = float(c["requests"]["throughput_rps"])

    p95_regression = 0.0 if b_p95 == 0 else (c_p95 - b_p95) / b_p95
    rps_drop = 0.0 if b_rps == 0 else (b_rps - c_rps) / b_rps

    ok = True
    if p95_regression > args.max_p95_regression:
        ok = False
        print(
            f"FAIL p95 regression too high: baseline={b_p95:.2f} candidate={c_p95:.2f} ratio={p95_regression:.3f}"
        )
    if rps_drop > args.max_rps_drop:
        ok = False
        print(f"FAIL rps drop too high: baseline={b_rps:.2f} candidate={c_rps:.2f} ratio={rps_drop:.3f}")

    if ok:
        print(
            f"PASS compare: p95_ratio={p95_regression:.3f} (limit {args.max_p95_regression:.3f}), "
            f"rps_drop={rps_drop:.3f} (limit {args.max_rps_drop:.3f})"
        )
        sys.exit(0)
    sys.exit(3)


if __name__ == "__main__":
    main()
