#!/usr/bin/env python3
"""Render a ~5-second rugby clip.

Starts a 15v15 rugby env with rendering + video dump enabled. Every ~5
steps the carrier presses rugby_pass so we see ball movement rather than
a solo jog. The .avi and .dump files are written to LOGDIR; the script
prints where they landed.
"""
from __future__ import annotations

import os
import shutil
import time

LOGDIR = "/Users/maximesebti/rugby_sim/artifacts/clip_5s"

if os.path.exists(LOGDIR):
    shutil.rmtree(LOGDIR)
os.makedirs(LOGDIR, exist_ok=True)

from rugby_sim import create_environment

env = create_environment(
    render=True,
    write_video=True,
    write_full_episode_dumps=True,
    logdir=LOGDIR,
)

RUGBY_PASS = 9  # index in the rugby_action_set

print("resetting env...")
env.reset()
print("stepping 50 frames...")
actions_idle = [0] * 15
actions_pass = [RUGBY_PASS] * 15
start = time.time()
for step in range(50):
    # Alternate between idle and pass to encourage visible ball movement.
    actions = actions_pass if step % 6 == 5 else actions_idle
    result = env.step(actions)
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

print("\nArtifacts written:")
for f in sorted(os.listdir(LOGDIR)):
    path = os.path.join(LOGDIR, f)
    size = os.path.getsize(path) if os.path.isfile(path) else 0
    print(f"  {f}  ({size} bytes)")
