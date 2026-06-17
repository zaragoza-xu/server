"""Apply/clear tc netem via scripts/netem.sh."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path
from typing import Optional


_SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "netem.sh"


def apply_netem(rule: str = "", dev: Optional[str] = None) -> bool:
    """Apply netem rule. Empty rule = clear (no interference). Returns True on success."""
    env = os.environ.copy()
    if dev:
        env["NETEM_DEV"] = dev
    env["NETEM_RULE"] = rule
    try:
        cmd = ["bash", str(_SCRIPT), "apply"]
        if dev:
            cmd.extend(["--dev", dev])
        if rule:
            cmd.extend(["--rule", rule])
        result = subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            print(f"netem warning: {result.stderr.strip() or result.stdout.strip()}")
            return False
        print(result.stdout.strip())
        return True
    except OSError as exc:
        print(f"netem skipped: {exc}")
        return False


def clear_netem(dev: Optional[str] = None) -> bool:
    env = os.environ.copy()
    if dev:
        env["NETEM_DEV"] = dev
    try:
        cmd = ["bash", str(_SCRIPT), "clear"]
        if dev:
            cmd.extend(["--dev", dev])
        result = subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            print(f"netem clear warning: {result.stderr.strip()}")
            return False
        print(result.stdout.strip())
        return True
    except OSError as exc:
        print(f"netem clear skipped: {exc}")
        return False
