#!/usr/bin/env python3
"""Project 3D world positions to 2D pixel bboxes using the engine camera.

Reads the camera parameters exposed in each observation
(`camera_position`, `camera_orientation` as quaternion xyzw, `camera_fov`,
`camera_near`, `camera_far`, `camera_view_width`, `camera_view_height`)
and returns bounding boxes in `xyxy` absolute-pixel format — the same
schema used by the Exeter-Newcastle annotation JSON.

Usage:
    from tools.project_bboxes import frame_bboxes
    bboxes = frame_bboxes(obs)  # list of dicts with class/team/bbox

A player is approximated by a world-space AABB 0.55 m wide, 0.55 m
deep, 1.85 m tall centred on their ground position. The ball is a
0.28×0.18×0.18 m oval at its reported position.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

import numpy as np


@dataclass
class CameraParams:
    position: np.ndarray  # (3,) world-space
    orientation: np.ndarray  # (4,) quaternion xyzw
    fov_deg: float
    near: float
    far: float
    width: int
    height: int


def camera_from_obs(obs: Dict) -> CameraParams:
    return CameraParams(
        position=np.asarray(obs["camera_position"], dtype=np.float64),
        orientation=np.asarray(obs["camera_orientation"], dtype=np.float64),
        fov_deg=float(obs["camera_fov"]),
        near=float(obs["camera_near"]),
        far=float(obs["camera_far"]),
        width=int(obs["camera_view_width"]),
        height=int(obs["camera_view_height"]),
    )


def broadcast_camera_from_obs(
    obs: Dict,
    *,
    fov_deg: float = 40.0,
    height: float = 22.0,
    back_offset: float = 45.0,
    side: str = "main",
    width: int = 1920,
    height_px: int = 1080,
) -> CameraParams:
    """Build a deterministic broadcast-style camera from the ball position.

    Mirrors the camera angle a human camera operator would use: positioned
    ~45 m to one side of the ball, ~22 m up, tilted down toward the ball.
    The engine's own follow-cam is useful for rendered video, but for
    machine-friendly dataset generation a repeatable camera is easier to
    compare to human annotations.
    """
    ball = np.asarray(obs.get("ball", [0.0, 0.0, 0.0]), dtype=np.float64)
    # Ball is reported in normalised pitch coords ±1; convert to metres.
    ball_world = np.array([ball[0] * 55.0, ball[1] * 36.0, 0.0])
    sign = -1.0 if side == "main" else 1.0
    cam_pos = np.array([
        ball_world[0] * 0.4,
        ball_world[1] + sign * back_offset,
        height,
    ])
    look_at = ball_world
    forward = look_at - cam_pos
    forward /= max(np.linalg.norm(forward), 1e-9)
    # Build a right-handed basis with world +Z as up.
    up = np.array([0.0, 0.0, 1.0])
    right = np.cross(forward, up)
    right /= max(np.linalg.norm(right), 1e-9)
    up_cam = np.cross(right, forward)
    # project_point treats local +Y as view-forward, +X right, +Z up —
    # a right-handed frame. Columns of R_local_to_world are those three
    # local axes expressed in world space.
    R_local_to_world = np.stack([right, forward, up_cam], axis=1)
    q = _matrix_to_quat(R_local_to_world)
    return CameraParams(
        position=cam_pos,
        orientation=q,
        fov_deg=fov_deg,
        near=1.0,
        far=250.0,
        width=width,
        height=height_px,
    )


def _matrix_to_quat(R: np.ndarray) -> np.ndarray:
    """Convert a 3×3 rotation matrix to quaternion xyzw."""
    m = R
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2.0
        w = 0.25 * s
        x = (m[2, 1] - m[1, 2]) / s
        y = (m[0, 2] - m[2, 0]) / s
        z = (m[1, 0] - m[0, 1]) / s
    elif (m[0, 0] > m[1, 1]) and (m[0, 0] > m[2, 2]):
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2.0
        w = (m[2, 1] - m[1, 2]) / s
        x = 0.25 * s
        y = (m[0, 1] + m[1, 0]) / s
        z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2.0
        w = (m[0, 2] - m[2, 0]) / s
        x = (m[0, 1] + m[1, 0]) / s
        y = 0.25 * s
        z = (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2.0
        w = (m[1, 0] - m[0, 1]) / s
        x = (m[0, 2] + m[2, 0]) / s
        y = (m[1, 2] + m[2, 1]) / s
        z = 0.25 * s
    return np.array([x, y, z, w], dtype=np.float64)


def _quat_to_matrix(q: np.ndarray) -> np.ndarray:
    """Quaternion xyzw → 3×3 rotation matrix.

    The engine stores the camera orientation as the composition of the
    camera rig and the camera's own tilt, expressed as a quaternion
    applied to a +Y-forward / +Z-up world by the renderer. We build the
    rotation that takes a world vector into the camera's local frame
    where -Z is view-forward (standard graphics convention), matching
    the engine's render pipeline.
    """
    x, y, z, w = q
    # Normalise defensively.
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n < 1e-9:
        return np.eye(3)
    x, y, z, w = x / n, y / n, z / n, w / n
    # Standard quaternion → rotation matrix.
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    return np.array([
        [1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy)],
        [2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx)],
        [2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy)],
    ])


def project_point(world: np.ndarray, cam: CameraParams) -> Optional[Tuple[float, float, float]]:
    """Return (u, v, view_forward) in pixels or None if behind the camera.

    Convention used by ``broadcast_camera_from_obs``:
    - local +Y is view-forward (objects in front of the lens have
      positive local y after applying the inverse camera rotation)
    - local +X is right
    - local +Z is up
    This is right-handed and consistent with the matrix we build from
    the camera's world-space forward/up.
    """
    rel = np.asarray(world, dtype=np.float64) - cam.position
    R_inv = _quat_to_matrix(cam.orientation).T
    local = R_inv @ rel
    forward = local[1]
    if forward <= cam.near * 0.25:
        return None
    fov_rad = math.radians(max(cam.fov_deg, 1e-3))
    f_y = 0.5 * cam.height / math.tan(0.5 * fov_rad)
    aspect = cam.width / max(cam.height, 1)
    f_x = f_y * aspect
    u = cam.width * 0.5 + f_x * (local[0] / forward)
    v = cam.height * 0.5 - f_y * (local[2] / forward)
    return float(u), float(v), float(forward)


def _world_aabb_bbox(
    centre: np.ndarray, size: Tuple[float, float, float], cam: CameraParams
) -> Optional[Tuple[float, float, float, float]]:
    """Project an axis-aligned box in world space and return xyxy bbox."""
    sx, sy, sz = size
    corners = [
        centre + np.array([dx, dy, dz])
        for dx in (-sx / 2, sx / 2)
        for dy in (-sy / 2, sy / 2)
        for dz in (0.0, sz)  # AABB sits on the ground (z=0 to z=sz)
    ]
    projected = [project_point(c, cam) for c in corners]
    pts = [p for p in projected if p is not None]
    if len(pts) < 4:
        return None  # mostly behind camera
    us = [p[0] for p in pts]
    vs = [p[1] for p in pts]
    x1, y1, x2, y2 = min(us), min(vs), max(us), max(vs)
    # Clip to screen; skip if fully outside.
    x1c = max(0.0, x1)
    y1c = max(0.0, y1)
    x2c = min(float(cam.width), x2)
    y2c = min(float(cam.height), y2)
    if x2c <= x1c or y2c <= y1c:
        return None
    return x1c, y1c, x2c, y2c


# Player / ball sizes are pitch-scale metres.
PLAYER_SIZE = (0.55, 0.55, 1.85)  # width_x, depth_y, height_z
BALL_SIZE = (0.28, 0.18, 0.18)


def _player_world(centre_norm: Iterable[float]) -> np.ndarray:
    """Normalised ±1 pitch coords → world (metres, z=0)."""
    x, y = float(centre_norm[0]), float(centre_norm[1])
    return np.array([x * 55.0, y * 36.0, 0.0])


def frame_bboxes(obs: Dict, camera: Optional[CameraParams] = None) -> List[Dict]:
    """Build bboxes for every player + the ball in this observation.

    Returns a list of dicts with keys matching the broadcast annotation
    schema: ``class``, ``team`` (or None), ``jersey_number``,
    ``bbox`` (xyxy absolute pixels).

    ``camera`` defaults to a deterministic broadcast camera tracking the
    ball at 22 m height / 45 m back / 40° FOV. Pass
    ``camera_from_obs(obs)`` to use the engine's live camera instead.
    """
    cam = camera if camera is not None else broadcast_camera_from_obs(obs)
    out: List[Dict] = []
    lt = np.asarray(obs.get("left_team", []))
    rt = np.asarray(obs.get("right_team", []))
    active_l = np.asarray(obs.get("left_team_active", []), dtype=bool) \
        if obs.get("left_team_active") is not None else np.ones(len(lt), bool)
    active_r = np.asarray(obs.get("right_team_active", []), dtype=bool) \
        if obs.get("right_team_active") is not None else np.ones(len(rt), bool)
    for idx, pos in enumerate(lt):
        if idx >= len(active_l) or not active_l[idx]:
            continue
        bb = _world_aabb_bbox(_player_world(pos), PLAYER_SIZE, cam)
        if bb is None:
            continue
        out.append({
            "class": "player",
            "team": "team_1",
            "jersey_number": int(idx),
            "bbox": [round(v, 2) for v in bb],
        })
    for idx, pos in enumerate(rt):
        if idx >= len(active_r) or not active_r[idx]:
            continue
        bb = _world_aabb_bbox(_player_world(pos), PLAYER_SIZE, cam)
        if bb is None:
            continue
        out.append({
            "class": "player",
            "team": "team_2",
            "jersey_number": int(idx),
            "bbox": [round(v, 2) for v in bb],
        })
    ball = obs.get("ball")
    if ball is not None:
        ball_world = np.array([float(ball[0]) * 55.0,
                               float(ball[1]) * 36.0,
                               max(float(ball[2]) - BALL_SIZE[2] / 2, 0.0)])
        bb = _world_aabb_bbox(ball_world, BALL_SIZE, cam)
        if bb is not None:
            out.append({
                "class": "ball",
                "team": None,
                "jersey_number": None,
                "bbox": [round(v, 2) for v in bb],
            })
    return out


__all__ = ["camera_from_obs", "project_point", "frame_bboxes", "CameraParams"]
