#!/usr/bin/env python3
"""Render 15s of open-play rugby — no forced infringements."""
from __future__ import annotations

import os
import shutil
import time

LOGDIR = "/Users/maximesebti/rugby_sim/artifacts/clip_open_15s"

if os.path.exists(LOGDIR):
    shutil.rmtree(LOGDIR)
os.makedirs(LOGDIR, exist_ok=True)

from rugby_sim import create_environment

env = create_environment(
    number_of_left_players_agent_controls=0,
    number_of_right_players_agent_controls=0,
    render=True,
    write_video=True,
    write_full_episode_dumps=True,
    logdir=LOGDIR,
    other_config_options={"display_game_stats": False},
)

print("resetting env (AI vs AI, open play)...")
env.reset()
print("stepping 150 frames (15 s)...")
start = time.time()
for step in range(150):
    result = env.step([])
    if len(result) == 5:
        obs, reward, terminated, truncated, info = result
        done = terminated or truncated
    else:
        obs, reward, done, info = result
    if done:
        print(f"episode ended at step {step}")
        break
print(f"done in {time.time()-start:.1f}s wall time")

try:
    env.close()
except Exception as exc:  # noqa: BLE001
    print(f"env.close raised: {exc}")

print("\nArtifacts:")
for f in sorted(os.listdir(LOGDIR)):
    path = os.path.join(LOGDIR, f)
    size = os.path.getsize(path) if os.path.isfile(path) else 0
    print(f"  {f} ({size} bytes)")
