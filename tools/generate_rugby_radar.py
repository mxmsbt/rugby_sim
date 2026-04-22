#!/usr/bin/env python3
"""Render a rugby-field minimap bitmap to replace the football radar.

Mirrors the markings that proceduralpitch.cpp::GenerateRugbyOverlay draws
on the real pitch: halfway, 22 m, dashed 10 m and 5 m, dashed 15 m
insets, shaded in-goal areas, dead-ball lines.
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

W, H = 550, 360
OUT = Path(
    "/Users/maximesebti/rugby_sim/third_party/rugby_engine/data/"
    "media/menu/radar/radar.bmp"
)
BACKUP = OUT.with_suffix(".bmp.football_backup")

# Engine field dimensions (must match gamedefines.hpp).
PITCH_HALF_W = 55.0
PITCH_HALF_H = 36.0
PITCH_FULL_HALF_W = 60.0
PITCH_FULL_HALF_H = 40.0

MARGIN = 8  # pixels around the pitch inside the bitmap

# Colours (RGBA for consistency with overlay).
GRASS = (34, 95, 34)
TRY_ZONE = (84, 125, 46)
LINE = (235, 235, 230)
LINE_SOFT = (210, 210, 200)

pitch_w_px = W - 2 * MARGIN
pitch_h_px = H - 2 * MARGIN


def fx(x: float) -> int:
    # Map engine x (-PITCH_FULL_HALF_W..+PITCH_FULL_HALF_W) to pixel x.
    return int(MARGIN + (x / PITCH_FULL_HALF_W * 0.5 + 0.5) * pitch_w_px)


def fy(y: float) -> int:
    return int(MARGIN + (y / PITCH_FULL_HALF_H * 0.5 + 0.5) * pitch_h_px)


def dashed_line(draw, x1, y1, x2, y2, dash=6, gap=5, width=1, fill=LINE):
    if x1 == x2:  # vertical
        y = min(y1, y2)
        yend = max(y1, y2)
        while y < yend:
            draw.line([(x1, y), (x1, min(y + dash, yend))], fill=fill, width=width)
            y += dash + gap
    else:  # horizontal
        x = min(x1, x2)
        xend = max(x1, x2)
        while x < xend:
            draw.line([(x, y1), (min(x + dash, xend), y1)], fill=fill, width=width)
            x += dash + gap


def main() -> None:
    if OUT.exists() and not BACKUP.exists():
        BACKUP.write_bytes(OUT.read_bytes())
        print(f"backed up radar.bmp -> {BACKUP.name}")

    img = Image.new("RGB", (W, H), (20, 60, 20))
    draw = ImageDraw.Draw(img)

    # Full rugby field (including in-goal).
    draw.rectangle(
        [(fx(-PITCH_FULL_HALF_W), fy(-PITCH_FULL_HALF_H)),
         (fx(PITCH_FULL_HALF_W), fy(PITCH_FULL_HALF_H))],
        fill=GRASS,
    )
    # In-goal / try-zone shading (darker green).
    draw.rectangle(
        [(fx(-PITCH_FULL_HALF_W), fy(-PITCH_HALF_H)),
         (fx(-PITCH_HALF_W), fy(PITCH_HALF_H))],
        fill=TRY_ZONE,
    )
    draw.rectangle(
        [(fx(PITCH_HALF_W), fy(-PITCH_HALF_H)),
         (fx(PITCH_FULL_HALF_W), fy(PITCH_HALF_H))],
        fill=TRY_ZONE,
    )

    # Sidelines + try lines + dead-ball lines.
    draw.line([(fx(-PITCH_HALF_W), fy(-PITCH_HALF_H)),
               (fx(PITCH_HALF_W), fy(-PITCH_HALF_H))], fill=LINE, width=1)
    draw.line([(fx(-PITCH_HALF_W), fy(PITCH_HALF_H)),
               (fx(PITCH_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=1)
    # In-goal sidelines.
    draw.line([(fx(-PITCH_FULL_HALF_W), fy(-PITCH_HALF_H)),
               (fx(-PITCH_HALF_W), fy(-PITCH_HALF_H))], fill=LINE, width=1)
    draw.line([(fx(-PITCH_FULL_HALF_W), fy(PITCH_HALF_H)),
               (fx(-PITCH_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=1)
    draw.line([(fx(PITCH_HALF_W), fy(-PITCH_HALF_H)),
               (fx(PITCH_FULL_HALF_W), fy(-PITCH_HALF_H))], fill=LINE, width=1)
    draw.line([(fx(PITCH_HALF_W), fy(PITCH_HALF_H)),
               (fx(PITCH_FULL_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=1)
    # Try lines (solid thick).
    draw.line([(fx(-PITCH_HALF_W), fy(-PITCH_HALF_H)),
               (fx(-PITCH_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=2)
    draw.line([(fx(PITCH_HALF_W), fy(-PITCH_HALF_H)),
               (fx(PITCH_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=2)
    # Dead-ball lines.
    draw.line([(fx(-PITCH_FULL_HALF_W), fy(-PITCH_HALF_H)),
               (fx(-PITCH_FULL_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=1)
    draw.line([(fx(PITCH_FULL_HALF_W), fy(-PITCH_HALF_H)),
               (fx(PITCH_FULL_HALF_W), fy(PITCH_HALF_H))], fill=LINE, width=1)

    # Halfway line.
    draw.line([(fx(0), fy(-PITCH_HALF_H)),
               (fx(0), fy(PITCH_HALF_H))], fill=LINE, width=1)

    # 22 m lines (solid).
    for x in (-22, 22):
        draw.line([(fx(x), fy(-PITCH_HALF_H)),
                   (fx(x), fy(PITCH_HALF_H))], fill=LINE, width=1)

    # 10 m lines (dashed).
    for x in (-10, 10):
        dashed_line(draw, fx(x), fy(-PITCH_HALF_H), fx(x), fy(PITCH_HALF_H),
                    dash=5, gap=4, fill=LINE_SOFT)

    # 5 m lines (dotted).
    for x in (-PITCH_HALF_W + 5, PITCH_HALF_W - 5):
        dashed_line(draw, fx(x), fy(-PITCH_HALF_H), fx(x), fy(PITCH_HALF_H),
                    dash=3, gap=4, fill=LINE_SOFT)

    # 15 m insets (dashed horizontal).
    for y in (-15, 15):
        dashed_line(draw, fx(-PITCH_HALF_W), fy(y), fx(PITCH_HALF_W), fy(y),
                    dash=4, gap=4, fill=LINE_SOFT)
    # 5 m insets (tight dotted).
    for y in (-PITCH_HALF_H + 5, PITCH_HALF_H - 5):
        dashed_line(draw, fx(-PITCH_HALF_W), fy(y), fx(PITCH_HALF_W), fy(y),
                    dash=2, gap=3, fill=LINE_SOFT)

    # + tick crosses at key intersections (the distinctive rugby pattern).
    arm = 4
    xs = [-PITCH_HALF_W + 5, -22, -10, 0, 10, 22, PITCH_HALF_W - 5]
    ys = [-15, -(PITCH_HALF_H - 5), 5 - PITCH_HALF_H, PITCH_HALF_H - 5, 15]
    for xm in xs:
        for ym in ys:
            cx, cy = fx(xm), fy(ym)
            draw.line([(cx - arm, cy), (cx + arm, cy)], fill=LINE, width=1)
            draw.line([(cx, cy - arm), (cx, cy + arm)], fill=LINE, width=1)
    # Touchline hash marks every 10 m along both sidelines.
    hash_len = 4
    for xm in range(-int(PITCH_HALF_W) + 10, int(PITCH_HALF_W), 10):
        if abs(xm) < 1:
            continue
        cx = fx(xm)
        for y_touch, sign in [(fy(-PITCH_HALF_H), 1), (fy(PITCH_HALF_H), -1)]:
            draw.line([(cx, y_touch), (cx, y_touch + sign * hash_len)],
                      fill=LINE, width=1)

    img.save(OUT, format="BMP")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
