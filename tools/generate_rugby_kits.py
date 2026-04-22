#!/usr/bin/env python3
"""Generate rugby-style kit textures for both teams.

Rugby jerseys differ from football shirts primarily by:
- horizontal hoops (alternating contrasting bands) on the torso
- a contrasting collar/V-neck at the top
- solid shorts (often a third colour)

The existing football kit BMPs carry the UV cut-out for the player body —
colored pixels are on-body, black pixels are off-UV. We keep that mask
and only recolour the non-black pixels with a rugby pattern.
"""
from __future__ import annotations

from pathlib import Path
from typing import List, Tuple

import numpy as np
from PIL import Image

KIT_DIR = Path(
    "/Users/maximesebti/rugby_sim/third_party/rugby_engine/data/"
    "databases/default/images_teams/primeradivision"
)


def _sample_mask(arr: np.ndarray) -> np.ndarray:
    """Pixels that are intended to be on-body (non-black in the source)."""
    rgb = arr[..., :3].astype(np.int16)
    return (rgb.sum(axis=-1) > 30)


def _recolour(
    arr: np.ndarray,
    primary: Tuple[int, int, int],
    secondary: Tuple[int, int, int],
    trim: Tuple[int, int, int],
    short_color: Tuple[int, int, int],
) -> np.ndarray:
    out = arr.copy()
    mask = _sample_mask(arr)
    h, w = arr.shape[:2]
    ys, xs = np.where(mask)
    # The torso occupies roughly the top 540 rows (y<540); below that are
    # the shorts panels. Hoops cycle on the torso; shorts are solid.
    torso_top, torso_bottom = 0, 540
    hoop_count = 6
    for y, x in zip(ys, xs):
        if y < torso_bottom:
            # Normalised torso y -> hoop index -> colour
            t = (y - torso_top) / max(torso_bottom - torso_top, 1)
            band = int(t * hoop_count)
            colour = primary if band % 2 == 0 else secondary
            # Top ~6% of torso is the collar/V-neck trim band.
            if t < 0.06:
                colour = trim
        else:
            colour = short_color
        out[y, x, 0] = colour[0]
        out[y, x, 1] = colour[1]
        out[y, x, 2] = colour[2]
    return out


def generate(source: Path, dest: Path, **colours) -> None:
    backup = source.with_suffix(source.suffix + ".football_backup")
    if not backup.exists():
        backup.write_bytes(source.read_bytes())
        print(f"backed up {source.name} -> {backup.name}")
    img = Image.open(source).convert("RGBA")
    arr = np.array(img)
    out = _recolour(arr, **colours)
    Image.fromarray(out, mode="RGBA").save(dest, format="BMP")
    print(f"wrote {dest.name}")


def main() -> None:
    # Team 1 — red/black hoops with white trim.
    generate(
        KIT_DIR / "fcbarcelona_kit_02.bmp",
        KIT_DIR / "fcbarcelona_kit_02.bmp",
        primary=(200, 30, 30),
        secondary=(30, 30, 30),
        trim=(240, 240, 240),
        short_color=(30, 30, 30),
    )
    # Team 2 — navy/white hoops with gold trim.
    generate(
        KIT_DIR / "realmadrid_kit_02.bmp",
        KIT_DIR / "realmadrid_kit_02.bmp",
        primary=(25, 25, 90),
        secondary=(240, 240, 240),
        trim=(220, 180, 60),
        short_color=(25, 25, 90),
    )
    # The engine still loads a "GK" for one player per team (the fullback
    # stand-in). Give them a distinct yellow/green kit so the 15 isn't
    # mistaken for an outfielder.
    goalie_src = Path(
        "/Users/maximesebti/rugby_sim/third_party/rugby_engine/data/"
        "media/objects/players/textures/goalie_kit.bmp"
    )
    generate(
        goalie_src,
        goalie_src,
        primary=(230, 200, 40),
        secondary=(30, 90, 40),
        trim=(30, 30, 30),
        short_color=(30, 90, 40),
    )


if __name__ == "__main__":
    main()
