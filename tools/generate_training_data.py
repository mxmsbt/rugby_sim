#!/usr/bin/env python3
"""Generate ML training data from rugby simulation matches.

Per match we emit three artifacts that parallel what a human-annotator
pipeline produces for real broadcast footage:

* ``match_NNN.mp4`` — the rendered RGB video of the match
* ``match_NNN.json`` — per-frame bbox annotations in the exact same
  schema as the Exeter-Newcastle sample: ``{video, fps, num_frames,
  width, height, bbox_format, coordinates, frames[{frame_number,
  objects[{bbox, class_id, class, team, jersey_number}]}]}``
* ``match_NNN_events.jsonl`` — timeline of high-level events (tries,
  conversions, knock-ons, scrums, lineouts, breakdowns, game-mode
  transitions, possession changes)

At the top level ``index.json`` lists all matches with aggregate stats.

Usage:
    python tools/generate_training_data.py --matches 3 --steps 600
    python tools/generate_training_data.py --matches 50 --steps 1200 \\
        --fov 22 --back-offset 55 --height 18 \\
        --out /path/to/dataset
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import sys

import numpy as np

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR.parent) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR.parent))

from rugby_sim import create_environment

from tools.project_bboxes import (
    broadcast_camera_from_obs,
    camera_from_obs,
    frame_bboxes,
)

def _class_id_for(obj: dict) -> int:
    """Class IDs match the Exeter-Newcastle broadcast annotation schema:
    0=ball, 1=referee, 2=team_1 player, 3=team_2 player.
    """
    cls = obj.get("class")
    team = obj.get("team")
    if cls == "ball":
        return 0
    if cls == "referee":
        return 1
    if cls == "player":
        if team == "team_1":
            return 2
        if team == "team_2":
            return 3
    return -1
VIDEO_FPS = 10  # one frame per env.step when physics_steps_per_frame=10


def _extract_obs(obs) -> dict:
    if isinstance(obs, list):
        return obs[0] if obs else {}
    return obs


@dataclass
class MatchState:
    prev_score: List[int] = field(default_factory=lambda: [0, 0])
    prev_game_mode: int = 0
    prev_breakdown: bool = False
    prev_scrum: bool = False
    prev_lineout: bool = False
    prev_owner: int = -1
    events: List[dict] = field(default_factory=list)

    def update(self, frame_index: int, o: dict) -> None:
        score = list(o.get("score", [0, 0]))
        for team_idx in (0, 1):
            delta = score[team_idx] - self.prev_score[team_idx]
            if delta > 0:
                kind = {5: "try", 2: "conversion", 3: "kick_goal",
                        7: "try_and_conversion"}.get(delta, f"score_+{delta}")
                self.events.append({"frame": frame_index, "type": kind,
                                     "team": team_idx, "delta": delta,
                                     "total": score[team_idx]})
        gm = int(o.get("game_mode", 0))
        bd = bool(o.get("rugby_breakdown_active", False))
        scrum = bool(o.get("rugby_scrum_active", False))
        lo = bool(o.get("rugby_lineout_active", False))
        owner = int(o.get("ball_owned_team", -1))
        if bd and not self.prev_breakdown:
            self.events.append({"frame": frame_index, "type": "breakdown_start"})
        if self.prev_breakdown and not bd:
            self.events.append({"frame": frame_index, "type": "breakdown_end"})
        if scrum and not self.prev_scrum:
            self.events.append({"frame": frame_index, "type": "scrum_start"})
        if self.prev_scrum and not scrum:
            self.events.append({"frame": frame_index, "type": "scrum_end",
                                 "winner": int(o.get(
                                     "rugby_scrum_winning_team", -1))})
        if lo and not self.prev_lineout:
            self.events.append({"frame": frame_index, "type": "lineout_start"})
        if self.prev_lineout and not lo:
            self.events.append({"frame": frame_index, "type": "lineout_end",
                                 "winner": int(o.get(
                                     "rugby_lineout_winning_team", -1))})
        if gm != self.prev_game_mode:
            self.events.append({"frame": frame_index, "type": "game_mode",
                                 "from": self.prev_game_mode, "to": gm})
        if owner != self.prev_owner and owner != -1 and \
                self.prev_owner not in (-1, owner):
            self.events.append({"frame": frame_index, "type": "possession_change",
                                 "from": self.prev_owner, "to": owner})
        self.prev_score = score
        self.prev_game_mode = gm
        self.prev_breakdown = bd
        self.prev_scrum = scrum
        self.prev_lineout = lo
        self.prev_owner = owner


def run_match(env, match_idx: int, steps: int, out_dir: Path,
              camera_kwargs: dict, first_match: bool) -> dict:
    match_dir = out_dir / f"match_{match_idx:03d}"
    match_dir.mkdir(parents=True, exist_ok=True)

    # env.reset flushes the previous match's AVI (if any) to logdir. We
    # call it before every match so the engine's video trace writer has a
    # predictable boundary; the first reset just initialises.
    env.reset()
    state = MatchState()

    annotation_frames: List[dict] = []
    video_width: Optional[int] = None
    video_height: Optional[int] = None

    for frame_index in range(steps):
        result = env.step([0])  # 1-agent idle; the AI drives everyone else
        if len(result) == 5:
            obs, reward, terminated, truncated, info = result
            done = terminated or truncated
        else:
            obs, reward, done, info = result
        o = _extract_obs(obs)
        if not o:
            break
        cam = broadcast_camera_from_obs(o, **camera_kwargs)
        if video_width is None:
            video_width = cam.width
            video_height = cam.height
        bboxes = frame_bboxes(o, cam)
        for b in bboxes:
            b["class_id"] = _class_id_for(b)
        annotation_frames.append({
            "frame_number": frame_index,
            "objects": bboxes,
        })
        state.update(frame_index, o)
        if done:
            break

    # Video transcode is deferred: the engine only flushes the AVI on the
    # next env.reset or env.close. We pick them up after the run loop
    # completes and rename them in chronological order.
    match_mp4 = match_dir / f"match_{match_idx:03d}.mp4"

    annotation = {
        "video": match_mp4.name,
        "fps": VIDEO_FPS,
        "num_frames": len(annotation_frames),
        "width": video_width or 1920,
        "height": video_height or 1080,
        "bbox_format": "xyxy",
        "coordinates": "absolute_pixels",
        "frames": annotation_frames,
    }
    with open(match_dir / f"match_{match_idx:03d}.json", "w") as fh:
        json.dump(annotation, fh)

    with open(match_dir / f"match_{match_idx:03d}_events.jsonl", "w") as fh:
        for ev in state.events:
            fh.write(json.dumps(ev) + "\n")

    # Per-match summary.
    event_counts: Dict[str, int] = {}
    for ev in state.events:
        event_counts[ev["type"]] = event_counts.get(ev["type"], 0) + 1
    summary = {
        "match": match_idx,
        "frames": len(annotation_frames),
        "final_score": state.prev_score,
        "event_counts": event_counts,
        "video": str(match_mp4.relative_to(out_dir)) if match_mp4.exists()
                 else None,
        "annotations": f"match_{match_idx:03d}/match_{match_idx:03d}.json",
        "events": f"match_{match_idx:03d}/match_{match_idx:03d}_events.jsonl",
    }
    with open(match_dir / "summary.json", "w") as fh:
        json.dump(summary, fh, indent=2)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matches", type=int, default=3)
    parser.add_argument("--steps", type=int, default=600,
                        help="simulation steps per match (≈ 0.1 s each)")
    parser.add_argument("--out", type=str,
                        default="/Users/maximesebti/rugby_sim/artifacts/"
                                "training_data")
    parser.add_argument("--fov", type=float, default=22.0,
                        help="broadcast camera vertical FOV (degrees)")
    parser.add_argument("--height", type=float, default=18.0,
                        help="camera height above pitch (metres)")
    parser.add_argument("--back-offset", type=float, default=55.0,
                        help="camera distance behind the touchline (metres)")
    parser.add_argument("--render", action="store_true",
                        help="render video frames (slow)")
    args = parser.parse_args()

    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    print(f"Generating {args.matches} matches × {args.steps} steps "
          f"(~{args.steps * 0.1:.0f} s simulated each) into {out}")

    # One "agent" player is registered purely so the env returns a raw obs
    # dict (needed for bbox projection + event tracking); its action is
    # always 0 (idle), so the engine's rugby AI drives every player on the
    # pitch exactly as it does with 0-agent configurations.
    env = create_environment(
        number_of_left_players_agent_controls=1,
        number_of_right_players_agent_controls=0,
        render=bool(args.render),
        write_video=bool(args.render),
        write_full_episode_dumps=bool(args.render),
        logdir=str(out),
        other_config_options={"display_game_stats": False},
    ).unwrapped

    camera_kwargs = {
        "fov_deg": args.fov,
        "height": args.height,
        "back_offset": args.back_offset,
    }

    start = time.time()
    summaries: List[dict] = []
    for i in range(args.matches):
        t0 = time.time()
        s = run_match(env, i, args.steps, out, camera_kwargs,
                      first_match=(i == 0))
        summaries.append(s)
        wall = time.time() - t0
        print(f"  match {i:3d}: score={s['final_score']} "
              f"events={s['event_counts']} ({wall:.1f}s)")

    # Flush the last match's video by resetting the env once more, then
    # collect all AVIs in chronological order and transcode them into the
    # corresponding match_NNN/match_NNN.mp4 slots.
    if args.render:
        try:
            env.reset()
        except Exception:
            pass
        avi_files = sorted(out.glob("episode_done_*.avi"))
        for idx, avi in enumerate(avi_files):
            if idx >= len(summaries):
                break
            match_dir = out / f"match_{idx:03d}"
            mp4 = match_dir / f"match_{idx:03d}.mp4"
            try:
                subprocess.run(
                    ["ffmpeg", "-y", "-i", str(avi), "-c:v", "libx264",
                     "-pix_fmt", "yuv420p", "-crf", "23", str(mp4)],
                    check=True, capture_output=True, timeout=300,
                )
                avi.unlink()
                summaries[idx]["video"] = str(mp4.relative_to(out))
                # Rewrite the per-match summary with the video path filled in.
                with open(match_dir / "summary.json", "w") as fh:
                    json.dump(summaries[idx], fh, indent=2)
            except Exception as exc:  # noqa: BLE001
                print(f"  [warn] ffmpeg transcode {avi.name}: {exc}")
        for leftover in out.glob("episode_done_*.dump"):
            leftover.unlink()
        for leftover in out.glob("episode_done_*.avi"):
            leftover.unlink()

    total_wall = time.time() - start
    with open(out / "index.json", "w") as fh:
        json.dump({
            "matches": summaries,
            "total_matches": len(summaries),
            "total_frames": sum(s["frames"] for s in summaries),
            "wall_seconds": round(total_wall, 1),
        }, fh, indent=2)
    print(f"\nTotal wall time: {total_wall:.1f}s "
          f"({total_wall / max(len(summaries), 1):.1f}s per match)")
    print(f"Index: {out / 'index.json'}")


if __name__ == "__main__":
    main()
