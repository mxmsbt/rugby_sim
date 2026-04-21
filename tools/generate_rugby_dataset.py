#!/usr/bin/env python3
"""Generate a rugby dataset from AI-vs-AI matches.

For each episode we record:
- per-tick state snapshot (ball position, ball owner, game mode, score,
  set-piece flags, per-player positions + roles) to `frames.parquet`
- event timeline with deltas (try, conversion, penalty, scrum, lineout,
  breakdown start/end, game-mode transitions) to `events.jsonl`
- per-match summary (final score, counts per event type, possession %,
  mean ball-carry advance) to `summary.json`

Aggregate stats across all episodes are printed so we can sanity-check
whether the simulation is producing rugby-realistic data (tries per
match, set-piece frequency, possession balance, ball range coverage).
"""
from __future__ import annotations

import argparse
import json
import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List

import numpy as np
import pandas as pd

from rugby_sim import create_environment


def _extract_obs_dict(obs):
    if isinstance(obs, list):
        return obs[0] if obs else {}
    return obs


def _capture_frame(step: int, o: dict) -> dict:
    """One row of the per-tick parquet table."""
    lt = np.asarray(o.get("left_team"))
    rt = np.asarray(o.get("right_team"))
    ball = np.asarray(o.get("ball"))
    bd_pos = np.asarray(o.get("rugby_breakdown_position", [0, 0, 0]))
    return {
        "step": step,
        "time_ms": int(o.get("rugby_actual_time_ms", 0)),
        "ball_x": float(ball[0]) if ball.size else np.nan,
        "ball_y": float(ball[1]) if ball.size else np.nan,
        "ball_z": float(ball[2]) if ball.size else np.nan,
        "ball_owned_team": int(o.get("ball_owned_team", -1)),
        "ball_owned_player": int(o.get("ball_owned_player", -1)),
        "game_mode": int(o.get("game_mode", 0)),
        "score_left": int(o.get("score", [0, 0])[0]),
        "score_right": int(o.get("score", [0, 0])[1]),
        "breakdown_active": bool(o.get("rugby_breakdown_active", False)),
        "scrum_active": bool(o.get("rugby_scrum_active", False)),
        "lineout_active": bool(o.get("rugby_lineout_active", False)),
        "breakdown_x": float(bd_pos[0]) if bd_pos.size else 0.0,
        "breakdown_y": float(bd_pos[1]) if bd_pos.size else 0.0,
        "left_positions": lt.flatten().tolist() if lt.size else [],
        "right_positions": rt.flatten().tolist() if rt.size else [],
    }


@dataclass
class EpisodeState:
    """Derives events by comparing the current tick to the previous tick."""

    prev_score: List[int] = field(default_factory=lambda: [0, 0])
    prev_game_mode: int = 0
    prev_breakdown: bool = False
    prev_scrum: bool = False
    prev_lineout: bool = False
    prev_owner: int = -1
    events: List[dict] = field(default_factory=list)
    possession_steps: List[int] = field(default_factory=lambda: [0, 0, 0])

    def update(self, step: int, o: dict) -> None:
        score = list(o.get("score", [0, 0]))
        gm = int(o.get("game_mode", 0))
        bd = bool(o.get("rugby_breakdown_active", False))
        scrum = bool(o.get("rugby_scrum_active", False))
        lo = bool(o.get("rugby_lineout_active", False))
        owner = int(o.get("ball_owned_team", -1))

        # Score deltas.
        for team_idx in (0, 1):
            delta = score[team_idx] - self.prev_score[team_idx]
            if delta > 0:
                if delta == 5:
                    kind = "try"
                elif delta == 2:
                    kind = "conversion"
                elif delta == 3:
                    kind = "kick_goal"  # penalty or drop
                else:
                    kind = f"score_+{delta}"
                self.events.append(
                    {"step": step, "type": kind, "team": team_idx,
                     "delta": delta, "total": score[team_idx]}
                )

        # State transitions.
        if bd and not self.prev_breakdown:
            self.events.append({"step": step, "type": "breakdown_start"})
        if self.prev_breakdown and not bd:
            self.events.append({"step": step, "type": "breakdown_end"})
        if scrum and not self.prev_scrum:
            self.events.append({"step": step, "type": "scrum_start"})
        if self.prev_scrum and not scrum:
            winner = int(o.get("rugby_scrum_winning_team", -1))
            self.events.append({"step": step, "type": "scrum_end",
                                 "winner": winner})
        if lo and not self.prev_lineout:
            self.events.append({"step": step, "type": "lineout_start"})
        if self.prev_lineout and not lo:
            winner = int(o.get("rugby_lineout_winning_team", -1))
            self.events.append({"step": step, "type": "lineout_end",
                                 "winner": winner})
        if gm != self.prev_game_mode:
            self.events.append({"step": step, "type": "game_mode",
                                 "from": self.prev_game_mode, "to": gm})

        # Possession tally.
        if owner == 0:
            self.possession_steps[0] += 1
        elif owner == 1:
            self.possession_steps[1] += 1
        else:
            self.possession_steps[2] += 1

        self.prev_score = score
        self.prev_game_mode = gm
        self.prev_breakdown = bd
        self.prev_scrum = scrum
        self.prev_lineout = lo
        self.prev_owner = owner


def run_episode(env, episode_idx: int, steps: int, out_dir: Path) -> dict:
    obs = env.reset()
    state = EpisodeState()
    frames: List[dict] = []
    ball_path_x: List[float] = []

    for step in range(steps):
        obs, reward, done, info = env.step([0])
        o = _extract_obs_dict(obs)
        if not o:
            break
        frames.append(_capture_frame(step, o))
        ball_path_x.append(float(o.get("ball", [0, 0, 0])[0]))
        state.update(step, o)
        if done:
            break

    ep_dir = out_dir / f"episode_{episode_idx:03d}"
    ep_dir.mkdir(parents=True, exist_ok=True)

    frames_df = pd.DataFrame(frames)
    frames_df.to_parquet(ep_dir / "frames.parquet", index=False)

    with open(ep_dir / "events.jsonl", "w") as fh:
        for ev in state.events:
            fh.write(json.dumps(ev) + "\n")

    tallies: Dict[str, int] = {}
    for ev in state.events:
        tallies[ev["type"]] = tallies.get(ev["type"], 0) + 1

    total_poss = sum(state.possession_steps) or 1
    summary = {
        "episode": episode_idx,
        "steps": len(frames),
        "final_score": [int(state.prev_score[0]), int(state.prev_score[1])],
        "event_counts": tallies,
        "possession_pct": {
            "left": round(state.possession_steps[0] / total_poss * 100, 1),
            "right": round(state.possession_steps[1] / total_poss * 100, 1),
            "loose": round(state.possession_steps[2] / total_poss * 100, 1),
        },
        "ball_x_range": [
            float(min(ball_path_x)) if ball_path_x else 0.0,
            float(max(ball_path_x)) if ball_path_x else 0.0,
        ],
        "ball_x_std": float(np.std(ball_path_x)) if ball_path_x else 0.0,
    }
    with open(ep_dir / "summary.json", "w") as fh:
        json.dump(summary, fh, indent=2)
    return summary


def aggregate(summaries: List[dict]) -> dict:
    def _col(field: str) -> List[int]:
        return [s.get("event_counts", {}).get(field, 0) for s in summaries]
    n = len(summaries)
    out = {
        "episodes": n,
        "avg_score": [
            round(float(np.mean([s["final_score"][0] for s in summaries])), 2),
            round(float(np.mean([s["final_score"][1] for s in summaries])), 2),
        ],
        "avg_tries": round(float(np.mean(_col("try"))), 2),
        "avg_conversions": round(float(np.mean(_col("conversion"))), 2),
        "avg_kick_goals": round(float(np.mean(_col("kick_goal"))), 2),
        "avg_breakdowns": round(float(np.mean(_col("breakdown_start"))), 2),
        "avg_scrums": round(float(np.mean(_col("scrum_start"))), 2),
        "avg_lineouts": round(float(np.mean(_col("lineout_start"))), 2),
        "avg_possession_pct": {
            "left": round(float(np.mean([s["possession_pct"]["left"]
                                         for s in summaries])), 1),
            "right": round(float(np.mean([s["possession_pct"]["right"]
                                          for s in summaries])), 1),
            "loose": round(float(np.mean([s["possession_pct"]["loose"]
                                          for s in summaries])), 1),
        },
        "avg_ball_x_std": round(
            float(np.mean([s["ball_x_std"] for s in summaries])), 2),
    }
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--episodes", type=int, default=5)
    parser.add_argument("--steps", type=int, default=600)
    parser.add_argument("--out", type=str,
                        default="/Users/maximesebti/rugby_sim/artifacts/dataset")
    args = parser.parse_args()

    out = Path(args.out)
    if out.exists():
        for child in out.glob("episode_*"):
            for f in child.iterdir():
                f.unlink()
            child.rmdir()
    out.mkdir(parents=True, exist_ok=True)

    print(f"Generating {args.episodes} matches × {args.steps} steps "
          f"(~{args.steps * 0.1:.0f}s each)")
    # Single env instance reused across episodes so the scenario's
    # EpisodeNumber() alternates which team starts with the ball.
    env = create_environment(
        number_of_left_players_agent_controls=1,
        number_of_right_players_agent_controls=0,
        render=False,
    ).unwrapped
    start = time.time()
    summaries: List[dict] = []
    for i in range(args.episodes):
        s = run_episode(env, i, args.steps, out)
        summaries.append(s)
        print(f"  ep {i:3d}: score={s['final_score']} events={s['event_counts']}")
    wall = time.time() - start
    print(f"\nTotal wall time: {wall:.1f}s "
          f"({wall / args.episodes:.1f}s per episode)")

    agg = aggregate(summaries)
    with open(out / "aggregate.json", "w") as fh:
        json.dump(agg, fh, indent=2)
    print("\n=== aggregate stats ===")
    print(json.dumps(agg, indent=2))


if __name__ == "__main__":
    main()
