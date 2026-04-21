#!/usr/bin/env python3
"""Stretch the ball mesh along a chosen axis to produce a rugby ball shape.

Reads the existing generic.ase, scales every MESH_VERTEX coordinate along
``STRETCH_AXIS`` by ``STRETCH_FACTOR``, and writes the result back to the
same path. The physics radius (0.11f) in ball.cpp is not changed — this is
a purely visual tweak.
"""
from __future__ import annotations

import re
import shutil
from pathlib import Path

ASE_PATH = Path(
    "/Users/maximesebti/rugby_sim/third_party/gfootball_engine/data/media/"
    "objects/balls/generic.ase"
)
BACKUP_PATH = ASE_PATH.with_suffix(".ase.football_backup")

# Axis indices: 0=X, 1=Y, 2=Z. Rugby balls at rest commonly lie along the
# horizontal (y) axis in this coordinate system (z is up). Stretching Y keeps
# the ball looking oval from the default camera perspective when rolling.
STRETCH_AXIS = 1
STRETCH_FACTOR = 1.45  # real rugby ball is ~1.47× longer than wide


VERTEX_RE = re.compile(
    r"^(\s*\*MESH_VERTEX\s+\d+\s+)(-?\d+\.\d+)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s*$"
)


def main() -> None:
    if not BACKUP_PATH.exists():
        shutil.copy2(ASE_PATH, BACKUP_PATH)
        print(f"backed up {ASE_PATH} -> {BACKUP_PATH}")

    src = BACKUP_PATH.read_text().splitlines()
    out = []
    changed = 0
    for line in src:
        match = VERTEX_RE.match(line)
        if not match:
            out.append(line)
            continue
        prefix, x, y, z = match.group(1), float(match.group(2)), float(match.group(3)), float(match.group(4))
        coords = [x, y, z]
        coords[STRETCH_AXIS] *= STRETCH_FACTOR
        out.append(
            f"{prefix}{coords[0]:.6f}\t{coords[1]:.6f}\t{coords[2]:.6f}"
        )
        changed += 1
    ASE_PATH.write_text("\n".join(out) + "\n")
    print(f"stretched {changed} vertices along axis {STRETCH_AXIS} "
          f"by {STRETCH_FACTOR}×")


if __name__ == "__main__":
    main()
