#!/usr/bin/env python3
"""Generate a minimal goals.ase containing rugby H-posts.

The file replaces the football goal geometry with two rugby H-post frames
(left + right) consisting of: two uprights + one crossbar per side. Netting
meshes are replaced by tiny degenerate triangles so the engine's
PrepareGoalNetting scan finishes in negligible time.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple


PITCH_HALF_W = 55.0      # engine pitch half-length (must match gamedefines.hpp)
GOAL_HALF_WIDTH = 2.82   # rugby posts 5.64 m apart
CROSSBAR_HEIGHT = 3.0
POST_TOTAL_HEIGHT = 8.5  # posts reach up to this height
POST_THICKNESS = 0.12    # side length of square post cross-section
CROSSBAR_THICKNESS = 0.12
# Small offset pushes the frame outside the try line a touch so it
# sits on the back edge of the line just like the football goal did.
TRY_LINE_OFFSET = 0.05


@dataclass
class Box:
    name: str
    cx: float
    cy: float
    cz: float
    sx: float
    sy: float
    sz: float


def box_vertices(box: Box) -> List[Tuple[float, float, float]]:
    hx, hy, hz = box.sx / 2.0, box.sy / 2.0, box.sz / 2.0
    corners = []
    for dx in (-hx, hx):
        for dy in (-hy, hy):
            for dz in (-hz, hz):
                corners.append((box.cx + dx, box.cy + dy, box.cz + dz))
    return corners


# Pre-computed face/normal layout for a generic box. The box_vertices order is
# (-hx,-hy,-hz) (-hx,-hy,hz) (-hx,hy,-hz) (-hx,hy,hz) (hx,-hy,-hz) ... (hx,hy,hz)
# indexed 0..7. The 6 faces are each split into 2 triangles.
BOX_FACES = [
    # -X face (0,1,3,2)
    (0, 1, 3, (-1.0, 0.0, 0.0)),
    (0, 3, 2, (-1.0, 0.0, 0.0)),
    # +X face (4,6,7,5)
    (4, 6, 7, (1.0, 0.0, 0.0)),
    (4, 7, 5, (1.0, 0.0, 0.0)),
    # -Y face (0,4,5,1)
    (0, 4, 5, (0.0, -1.0, 0.0)),
    (0, 5, 1, (0.0, -1.0, 0.0)),
    # +Y face (2,3,7,6)
    (2, 3, 7, (0.0, 1.0, 0.0)),
    (2, 7, 6, (0.0, 1.0, 0.0)),
    # -Z face (0,2,6,4)
    (0, 2, 6, (0.0, 0.0, -1.0)),
    (0, 6, 4, (0.0, 0.0, -1.0)),
    # +Z face (1,5,7,3)
    (1, 5, 7, (0.0, 0.0, 1.0)),
    (1, 7, 3, (0.0, 0.0, 1.0)),
]


def rugby_boxes_for_side(side_x: float, suffix: str) -> List[Box]:
    half_tw = CROSSBAR_THICKNESS / 2.0
    post_cz = POST_TOTAL_HEIGHT / 2.0
    # Uprights centred on crossbar height such that top sits at
    # POST_TOTAL_HEIGHT and bottom at 0.
    return [
        Box(f"goal{suffix}_left_post",
            cx=side_x,
            cy=-GOAL_HALF_WIDTH,
            cz=post_cz,
            sx=POST_THICKNESS,
            sy=POST_THICKNESS,
            sz=POST_TOTAL_HEIGHT),
        Box(f"goal{suffix}_right_post",
            cx=side_x,
            cy=GOAL_HALF_WIDTH,
            cz=post_cz,
            sx=POST_THICKNESS,
            sy=POST_THICKNESS,
            sz=POST_TOTAL_HEIGHT),
        Box(f"goal{suffix}_crossbar",
            cx=side_x,
            cy=0.0,
            cz=CROSSBAR_HEIGHT,
            sx=CROSSBAR_THICKNESS,
            sy=2.0 * GOAL_HALF_WIDTH - POST_THICKNESS,
            sz=CROSSBAR_THICKNESS),
    ]


MATERIAL_BLOCK = """*MATERIAL_LIST {
\t*MATERIAL_COUNT 2
\t*MATERIAL 0 {
\t\t*MATERIAL_NAME "white"
\t\t*MATERIAL_CLASS "Standard"
\t\t*MATERIAL_AMBIENT 0.90 0.90 0.90
\t\t*MATERIAL_DIFFUSE 0.95 0.95 0.95
\t\t*MATERIAL_SPECULAR 0.80 0.80 0.80
\t\t*MATERIAL_SHINE 0.30
\t\t*MATERIAL_SHINESTRENGTH 0.40
\t\t*MATERIAL_TRANSPARENCY 0.0
\t\t*MATERIAL_WIRESIZE 1.0
\t\t*MATERIAL_SHADING Blinn
\t\t*MATERIAL_XP_FALLOFF 0.0
\t\t*MATERIAL_SELFILLUM 0.0
\t\t*MATERIAL_FALLOFF In
\t\t*MATERIAL_XP_TYPE Filter
\t\t*MAP_DIFFUSE {
\t\t\t*MAP_NAME "Map #3"
\t\t\t*MAP_CLASS "Bitmap"
\t\t\t*MAP_SUBNO 1
\t\t\t*MAP_AMOUNT 1.0
\t\t\t*BITMAP "media/objects/stadiums/white.png"
\t\t\t*MAP_TYPE Screen
\t\t\t*UVW_U_OFFSET 0.0
\t\t\t*UVW_V_OFFSET 0.0
\t\t\t*UVW_U_TILING 1.0
\t\t\t*UVW_V_TILING 1.0
\t\t\t*UVW_ANGLE 0.0
\t\t\t*UVW_BLUR 1.0
\t\t\t*UVW_BLUR_OFFSET 0.0
\t\t\t*UVW_NOUSE_AMT 1.0
\t\t\t*UVW_NOISE_SIZE 1.0
\t\t\t*UVW_NOISE_LEVEL 1
\t\t\t*UVW_NOISE_PHASE 0.0
\t\t\t*BITMAP_FILTER Pyramidal
\t\t}
\t}
\t*MATERIAL 1 {
\t\t*MATERIAL_NAME "placeholder"
\t\t*MATERIAL_CLASS "Standard"
\t\t*MATERIAL_AMBIENT 0.50 0.50 0.50
\t\t*MATERIAL_DIFFUSE 0.50 0.50 0.50
\t\t*MATERIAL_SPECULAR 0.30 0.30 0.30
\t\t*MATERIAL_SHINE 0.10
\t\t*MATERIAL_SHINESTRENGTH 0.10
\t\t*MATERIAL_TRANSPARENCY 1.0
\t\t*MATERIAL_WIRESIZE 1.0
\t\t*MATERIAL_SHADING Blinn
\t\t*MATERIAL_XP_FALLOFF 0.0
\t\t*MATERIAL_SELFILLUM 0.0
\t\t*MATERIAL_FALLOFF In
\t\t*MATERIAL_XP_TYPE Filter
\t\t*MAP_DIFFUSE {
\t\t\t*MAP_NAME "Map #1"
\t\t\t*MAP_CLASS "Bitmap"
\t\t\t*MAP_SUBNO 1
\t\t\t*MAP_AMOUNT 1.0
\t\t\t*BITMAP "media/textures/stadium/goalnetting.png"
\t\t\t*MAP_TYPE Screen
\t\t\t*UVW_U_OFFSET 0.0
\t\t\t*UVW_V_OFFSET 0.0
\t\t\t*UVW_U_TILING 1.0
\t\t\t*UVW_V_TILING 1.0
\t\t\t*UVW_ANGLE 0.0
\t\t\t*UVW_BLUR 1.0
\t\t\t*UVW_BLUR_OFFSET 0.0
\t\t\t*UVW_NOUSE_AMT 1.0
\t\t\t*UVW_NOISE_SIZE 1.0
\t\t\t*UVW_NOISE_LEVEL 1
\t\t\t*UVW_NOISE_PHASE 0.0
\t\t\t*BITMAP_FILTER Pyramidal
\t\t}
\t}
}"""


HEADER = """*3DSMAX_ASCIIEXPORT\t200
*COMMENT "Generated by tools/generate_rugby_goals_ase.py"
*SCENE {
\t*SCENE_FILENAME "rugby_goals.max"
\t*SCENE_FIRSTFRAME 0
\t*SCENE_LASTFRAME 100
\t*SCENE_FRAMESPEED 30
\t*SCENE_TICKSPERFRAME 160
\t*SCENE_BACKGROUND_STATIC 0.05 0.05 0.05
\t*SCENE_AMBIENT_STATIC 0.10 0.10 0.10
}"""


def emit_geomobject(box: Box, material_id: int = 0) -> str:
    verts = box_vertices(box)
    lines: List[str] = []
    lines.append("*GEOMOBJECT {")
    lines.append(f"\t*NODE_NAME \"{box.name}\"")
    lines.append("\t*NODE_TM {")
    lines.append(f"\t\t*NODE_NAME \"{box.name}\"")
    lines.append("\t\t*INHERIT_POS 0 0 0")
    lines.append("\t\t*INHERIT_ROT 0 0 0")
    lines.append("\t\t*INHERIT_SCL 0 0 0")
    lines.append("\t\t*TM_ROW0 1.0 0.0 0.0")
    lines.append("\t\t*TM_ROW1 0.0 1.0 0.0")
    lines.append("\t\t*TM_ROW2 0.0 0.0 1.0")
    lines.append("\t\t*TM_ROW3 0.0 0.0 0.0")
    lines.append("\t\t*TM_POS 0.0 0.0 0.0")
    lines.append("\t\t*TM_ROTAXIS 0.0 0.0 1.0")
    lines.append("\t\t*TM_ROTANGLE 0.0")
    lines.append("\t\t*TM_SCALE 1.0 1.0 1.0")
    lines.append("\t\t*TM_SCALEAXIS 0.0 0.0 0.0")
    lines.append("\t\t*TM_SCALEAXISANG 0.0")
    lines.append("\t}")
    lines.append("\t*MESH {")
    lines.append("\t\t*TIMEVALUE 0")
    lines.append(f"\t\t*MESH_NUMVERTEX {len(verts)}")
    lines.append(f"\t\t*MESH_NUMFACES {len(BOX_FACES)}")
    lines.append("\t\t*MESH_VERTEX_LIST {")
    for i, v in enumerate(verts):
        lines.append(f"\t\t\t*MESH_VERTEX {i}\t{v[0]:.4f}\t{v[1]:.4f}\t{v[2]:.4f}")
    lines.append("\t\t}")
    lines.append("\t\t*MESH_FACE_LIST {")
    for i, (a, b, c, _) in enumerate(BOX_FACES):
        lines.append(
            f"\t\t\t*MESH_FACE {i}:\tA:\t{a} B:\t{b} C:\t{c} "
            f"AB:\t1 BC:\t1 CA:\t1\t*MESH_SMOOTHING 1 \t*MESH_MTLID {material_id}"
        )
    lines.append("\t\t}")
    # UV coordinates — each face gets 3 UV verts; we reuse a simple 0/1 ring.
    tvert_count = len(BOX_FACES) * 3
    lines.append(f"\t\t*MESH_NUMTVERTEX {tvert_count}")
    lines.append("\t\t*MESH_TVERTLIST {")
    for i in range(tvert_count):
        u = 0.0 if i % 3 == 0 else 1.0 if i % 3 == 1 else 1.0
        v = 0.0 if i % 3 != 2 else 1.0
        lines.append(f"\t\t\t*MESH_TVERT {i}\t{u:.4f}\t{v:.4f}\t0.0000")
    lines.append("\t\t}")
    lines.append(f"\t\t*MESH_NUMTVFACES {len(BOX_FACES)}")
    lines.append("\t\t*MESH_TFACELIST {")
    for i in range(len(BOX_FACES)):
        base = i * 3
        lines.append(f"\t\t\t*MESH_TFACE {i}\t{base}\t{base + 1}\t{base + 2}")
    lines.append("\t\t}")
    lines.append("\t\t*MESH_NUMCVERTEX 0")
    lines.append("\t\t*MESH_NORMALS {")
    for face_idx, (a, b, c, n) in enumerate(BOX_FACES):
        lines.append(
            f"\t\t\t*MESH_FACENORMAL {face_idx}\t{n[0]:.4f}\t{n[1]:.4f}\t{n[2]:.4f}"
        )
        for vi in (a, b, c):
            lines.append(
                f"\t\t\t\t*MESH_VERTEXNORMAL {vi}\t{n[0]:.4f}\t{n[1]:.4f}\t{n[2]:.4f}"
            )
    lines.append("\t\t}")
    lines.append("\t}")
    lines.append("\t*PROP_MOTIONBLUR 0")
    lines.append("\t*PROP_CASTSHADOW 1")
    lines.append("\t*PROP_RECVSHADOW 1")
    lines.append(f"\t*MATERIAL_REF {material_id}")
    lines.append("}")
    return "\n".join(lines)


def degenerate_netting_placeholder(name: str, side_x: float) -> str:
    """A 1-triangle zero-area mesh using material 1, placed far from play.

    Having a real mesh here keeps PrepareGoalNetting happy (it iterates over
    all vertices of the "goals" object and bins those past the try line as
    "netting" — degenerate triangles collapse to a point so the deformation
    code sees nothing interesting to move.
    """
    far_x = side_x + (0.6 if side_x > 0 else -0.6)
    verts = [(far_x, 0.0, 0.0), (far_x, 0.01, 0.0), (far_x, 0.0, 0.01)]
    lines: List[str] = []
    lines.append("*GEOMOBJECT {")
    lines.append(f"\t*NODE_NAME \"{name}\"")
    lines.append("\t*NODE_TM {")
    lines.append(f"\t\t*NODE_NAME \"{name}\"")
    lines.append("\t\t*INHERIT_POS 0 0 0")
    lines.append("\t\t*INHERIT_ROT 0 0 0")
    lines.append("\t\t*INHERIT_SCL 0 0 0")
    lines.append("\t\t*TM_ROW0 1.0 0.0 0.0")
    lines.append("\t\t*TM_ROW1 0.0 1.0 0.0")
    lines.append("\t\t*TM_ROW2 0.0 0.0 1.0")
    lines.append("\t\t*TM_ROW3 0.0 0.0 0.0")
    lines.append("\t\t*TM_POS 0.0 0.0 0.0")
    lines.append("\t\t*TM_ROTAXIS 0.0 0.0 1.0")
    lines.append("\t\t*TM_ROTANGLE 0.0")
    lines.append("\t\t*TM_SCALE 1.0 1.0 1.0")
    lines.append("\t\t*TM_SCALEAXIS 0.0 0.0 0.0")
    lines.append("\t\t*TM_SCALEAXISANG 0.0")
    lines.append("\t}")
    lines.append("\t*MESH {")
    lines.append("\t\t*TIMEVALUE 0")
    lines.append("\t\t*MESH_NUMVERTEX 3")
    lines.append("\t\t*MESH_NUMFACES 1")
    lines.append("\t\t*MESH_VERTEX_LIST {")
    for i, v in enumerate(verts):
        lines.append(f"\t\t\t*MESH_VERTEX {i}\t{v[0]:.4f}\t{v[1]:.4f}\t{v[2]:.4f}")
    lines.append("\t\t}")
    lines.append("\t\t*MESH_FACE_LIST {")
    lines.append("\t\t\t*MESH_FACE 0:\tA:\t0 B:\t1 C:\t2 AB:\t1 BC:\t1 CA:\t1\t*MESH_SMOOTHING 1 \t*MESH_MTLID 1")
    lines.append("\t\t}")
    lines.append("\t\t*MESH_NUMTVERTEX 3")
    lines.append("\t\t*MESH_TVERTLIST {")
    lines.append("\t\t\t*MESH_TVERT 0\t0.0\t0.0\t0.0")
    lines.append("\t\t\t*MESH_TVERT 1\t1.0\t0.0\t0.0")
    lines.append("\t\t\t*MESH_TVERT 2\t0.0\t1.0\t0.0")
    lines.append("\t\t}")
    lines.append("\t\t*MESH_NUMTVFACES 1")
    lines.append("\t\t*MESH_TFACELIST {")
    lines.append("\t\t\t*MESH_TFACE 0\t0\t1\t2")
    lines.append("\t\t}")
    lines.append("\t\t*MESH_NUMCVERTEX 0")
    lines.append("\t\t*MESH_NORMALS {")
    lines.append("\t\t\t*MESH_FACENORMAL 0\t1.0\t0.0\t0.0")
    lines.append("\t\t\t\t*MESH_VERTEXNORMAL 0\t1.0\t0.0\t0.0")
    lines.append("\t\t\t\t*MESH_VERTEXNORMAL 1\t1.0\t0.0\t0.0")
    lines.append("\t\t\t\t*MESH_VERTEXNORMAL 2\t1.0\t0.0\t0.0")
    lines.append("\t\t}")
    lines.append("\t}")
    lines.append("\t*PROP_MOTIONBLUR 0")
    lines.append("\t*PROP_CASTSHADOW 0")
    lines.append("\t*PROP_RECVSHADOW 0")
    lines.append("\t*MATERIAL_REF 1")
    lines.append("}")
    return "\n".join(lines)


def main() -> None:
    out_path = (
        "/Users/maximesebti/rugby_sim/third_party/rugby_engine/data/media/"
        "objects/stadiums/goals.ase"
    )
    parts: List[str] = [HEADER, MATERIAL_BLOCK]

    # Goal 01 sits on the left try line (negative x). Shift outwards slightly
    # so posts land on the back edge of the try line.
    left_x = -(PITCH_HALF_W + TRY_LINE_OFFSET)
    right_x = PITCH_HALF_W + TRY_LINE_OFFSET

    for box in rugby_boxes_for_side(left_x, suffix="01"):
        parts.append(emit_geomobject(box, material_id=0))
    for box in rugby_boxes_for_side(right_x, suffix="02"):
        parts.append(emit_geomobject(box, material_id=0))

    # Keep the netting node names the engine remembers from football builds so
    # PrepareGoalNetting still finds its targets (just empty now).
    netting_names = [
        ("goal01_sidenetting01", left_x),
        ("goal01_sidenetting02", left_x),
        ("goal01_topnetting01", left_x),
        ("goal01_rearnetting01", left_x),
        ("goal02_sidenetting01", right_x),
        ("goal02_sidenetting02", right_x),
        ("goal02_topnetting01", right_x),
        ("goal02_rearnetting01", right_x),
    ]
    for name, x in netting_names:
        parts.append(degenerate_netting_placeholder(name, x))

    with open(out_path, "w") as f:
        f.write("\n".join(parts) + "\n")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
