"""Load ramp (step-up / recovery) profiles for stress tests."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List


def _default_profile() -> Dict[str, Any]:
    return {
        "warmup_seconds": 30,
        "steps": [
            {"concurrency": 10, "hold_seconds": 30},
            {"concurrency": 25, "hold_seconds": 30},
            {"concurrency": 50, "hold_seconds": 30},
            {"concurrency": 100, "hold_seconds": 30},
        ],
        "recovery": [
            {"concurrency": 50, "hold_seconds": 20},
            {"concurrency": 10, "hold_seconds": 20},
        ],
    }


def load_ramp_profile(path: str | Path | None) -> Dict[str, Any]:
    if path is None:
        return _default_profile()
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"Ramp config not found: {p}")
    text = p.read_text(encoding="utf-8")
    if p.suffix.lower() in (".yaml", ".yml"):
        try:
            import yaml  # type: ignore
        except ImportError:
            # Minimal YAML subset parser for our flat config.
            return _parse_minimal_yaml(text)
        return yaml.safe_load(text)
    return json.loads(text)


def _parse_minimal_yaml(text: str) -> Dict[str, Any]:
    """Parse the small ramp YAML without PyYAML dependency."""
    profile: Dict[str, Any] = {"warmup_seconds": 0, "steps": [], "recovery": []}
    section = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.endswith(":") and not line.startswith("-"):
            key = line[:-1]
            if key == "steps":
                section = "steps"
            elif key == "recovery":
                section = "recovery"
            elif key == "warmup_seconds":
                section = "warmup"
            continue
        if section == "warmup" and ":" in line:
            profile["warmup_seconds"] = int(line.split(":", 1)[1].strip())
            section = None
            continue
        if line.startswith("warmup_seconds:"):
            profile["warmup_seconds"] = int(line.split(":", 1)[1].strip())
            continue
        if line.startswith("- {") and section in ("steps", "recovery"):
            inner = line.strip("- {}")
            parts = {}
            for pair in inner.split(","):
                k, v = pair.split(":", 1)
                k = k.strip()
                v = v.strip()
                parts[k] = int(v) if v.isdigit() else v
            profile[section].append(parts)
    return profile


def build_ramp_sequence(
    profile: Dict[str, Any],
    base_concurrency: int | None = None,
    max_concurrency: int | None = None,
) -> List[Dict[str, Any]]:
    """Return ordered list of {phase, concurrency, hold_seconds}."""
    sequence: List[Dict[str, Any]] = []
    warmup = int(profile.get("warmup_seconds", 0))
    if warmup > 0:
        first_step_c = int(profile.get("steps", [{}])[0].get("concurrency", 1)) if profile.get("steps") else 1
        c = min(base_concurrency or 1, first_step_c) if base_concurrency else first_step_c
        sequence.append({"phase": "warmup", "concurrency": c, "hold_seconds": warmup})

    for step in profile.get("steps", []):
        c = int(step["concurrency"])
        if max_concurrency is not None:
            c = min(c, max_concurrency)
        sequence.append(
            {
                "phase": "ramp_up",
                "concurrency": c,
                "hold_seconds": int(step["hold_seconds"]),
            }
        )

    for step in profile.get("recovery", []):
        c = int(step["concurrency"])
        sequence.append(
            {
                "phase": "recovery",
                "concurrency": c,
                "hold_seconds": int(step["hold_seconds"]),
            }
        )
    return sequence
