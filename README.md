# RugbySim

A 15-a-side rugby-union simulator and training-data generator. Runs a
full 15 v 15 match with a built-in rugby AI for both teams, renders a
broadcast-style video, and emits per-frame bounding-box annotations in
the same schema used by human annotators of real match footage.

Primary purpose: generate large, labelled, rugby-realistic datasets for
computer-vision and analytics models.

<p align="center">
  <img src="docs/radar_preview.png" alt="rugby pitch" width="600">
</p>

---

## What's in the box

**A playable rugby match**
- 15 v 15 teams, full 80-minute game duration, scoreboard, HUD, and
  broadcast-style camera that follows the action.
- Real rugby laws: try (5), conversion (2, with animated kick),
  penalty goal (3), drop goal (3), knock-on + forward-pass detection,
  ruck offside.
- Set pieces: scrum (8-player bound pack: 3-2-3 formation with
  shoulder-to-shoulder front row), lineout (2-phase throw arc with
  probabilistic jump contest), kickoff restart after every score.
- Open-play mechanics: ball carried in hands at chest height, backward
  passes only, auto-pickup of loose balls, auto-offload when tackled,
  occasional kick-to-touch from deep.
- 15 players per side, all outfield — no goalkeeper.

**A rugby AI**
- Carrier sprints toward the opposition try line.
- Two closest teammates run as support on either shoulder.
- Remaining attackers hold a flat backline behind the carrier.
- Defenders form a flat pressing line; nearest defender breaks to
  tackle.
- Triggers kick-to-touch when deep in own half or cornered.

**A rugby-realistic pitch and visuals**
- Pitch overlay: halfway, 22 m solid, 10 m dashed, 5 m / 15 m dotted
  insets, tick crosses at intersections, touchline hash marks,
  dead-ball lines, shaded in-goal areas.
- H-post goalposts, oval ball, hooped team jerseys, rugby minimap.

**Training-data pipeline**
- `tools/generate_training_data.py` runs N matches and produces, per
  match: MP4 video, JSON bbox annotations, JSONL event timeline,
  plus a top-level index.

**QA harness**
- `tools/qa_report.py` — single-command readiness check across 9
  dimensions (scenario smoke, long-run stability, jersey identity,
  bbox geometry, determinism, pipeline schema, event rates, ball
  tracking, annotation-schema compatibility). Expected verdict:
  `READY`.

---

## Install

Prerequisites (macOS via Homebrew — equivalent packages on Linux):

```bash
brew install cmake sdl2 sdl2_image sdl2_ttf sdl2_gfx boost python@3.14
```

Create a virtualenv and install:

```bash
python3.14 -m venv .venv
source .venv/bin/activate
python3 -m pip install --upgrade pip wheel setuptools
python3 -m pip install -e .
```

`setup.py develop` compiles the native engine via CMake and creates
the `rugby_engine/` symlink at the repo root so the Python side can
import it directly.

---

## Quick start

```python
from rugby_sim import create_environment

env = create_environment(render=False)
env.reset()
obs, reward, done, info = env.step([0] * 15)
```

Available scenarios:

```python
from rugby_sim import (
    create_environment,                     # full 15 v 15 match
    create_try_restart_test_environment,    # starts with a forced try
    create_breakdown_test_environment,      # starts at a ruck
    create_scrum_test_environment,          # starts at a scrum
    create_lineout_test_environment,        # starts at a lineout
    create_forward_pass_test_environment,   # forces a forward-pass scrum
    create_knock_on_test_environment,       # forces a knock-on scrum
)
```

**Run an AI-vs-AI match with rendering**:

```bash
python tools/render_clip_30s.py
# output → artifacts/clip_30s/rugby_30s.mp4 (30 s of simulated play)
```

---

## Training-data pipeline

```bash
python tools/generate_training_data.py \
    --matches 50 --steps 1200 \
    --fov 22 --height 18 --back-offset 55 \
    --out ./dataset --render
```

Per-match output:

```
dataset/
├── index.json                         # summary across all matches
└── match_NNN/
    ├── match_NNN.mp4                  # rendered video (10 fps, 1920×1080)
    ├── match_NNN.json                 # per-frame bbox annotations
    ├── match_NNN_events.jsonl         # try/scrum/lineout/breakdown timeline
    └── summary.json                   # score + event counts
```

**Annotation schema** — directly compatible with real-match annotation
files (same keys, same bbox format, same class-id mapping):

```json
{
  "video": "match_000.mp4",
  "fps": 10,
  "num_frames": 1200,
  "width": 1920,
  "height": 1080,
  "bbox_format": "xyxy",
  "coordinates": "absolute_pixels",
  "frames": [
    {
      "frame_number": 0,
      "objects": [
        {"bbox": [x1, y1, x2, y2], "class_id": 2,
         "class": "player", "team": "team_1", "jersey_number": 5},
        {"bbox": [x1, y1, x2, y2], "class_id": 3,
         "class": "player", "team": "team_2", "jersey_number": 12},
        {"bbox": [x1, y1, x2, y2], "class_id": 0,
         "class": "ball", "team": null, "jersey_number": null}
      ]
    }
  ]
}
```

Class IDs: `0 = ball`, `1 = referee` (reserved), `2 = player team_1`,
`3 = player team_2`.

**Event timeline** (JSON-lines): `try`, `conversion`,
`try_and_conversion`, `kick_goal`, `breakdown_start/end`,
`scrum_start/end` (with `winner`), `lineout_start/end` (with
`winner`), `game_mode` transitions, `possession_change`.

### Scaling

- **Headless** (JSON + events only, no video): ~1 s per 100 simulated
  steps. 50 matches × 1200 steps ≈ 10 min.
- **Rendered** (with video): ~10 s per match of video wall time.
  50 matches × 1200 steps ≈ 2 h for the full pipeline on a laptop.

---

## QA

```bash
python tools/qa_report.py
```

Runs 9 deep checks and prints a pass/fail verdict:

1. All 7 scenarios step cleanly and honour episode `done`.
2. 15 v 15 runs 3000 steps without degradation.
3. Jersey identity stable — all 15 numbers per team visible in a match.
4. Bbox geometry valid — all in-frame, no NaN.
5. Deterministic scenario reproduces its event timeline bit-exact.
6. `generate_training_data.py` produces schema-valid JSON.
7. Event rates (tries, scrums, breakdowns, lineouts) sit in rugby
   bands.
8. Ball bbox present in ≥ 75 % of frames.
9. Annotation schema matches real-broadcast reference.

Expected output:

```
QA RESULT: 9/9 checks passed
VERDICT: READY
```

---

## Camera projection

`tools/project_bboxes.py` projects 3-D world positions to 2-D pixel
boxes. Useful outside the dataset pipeline:

```python
from tools.project_bboxes import broadcast_camera_from_obs, frame_bboxes

cam = broadcast_camera_from_obs(
    obs, fov_deg=22.0, height=18.0, back_offset=55.0,
    width=1920, height_px=1080,
)
bboxes = frame_bboxes(obs, cam)
# [{'class':'player','team':'team_1','jersey_number':5,'bbox':[...]}, ...]
```

The deterministic broadcast camera (recommended) is a stable overhead
view tracking the ball. The engine's own follow-camera is also
exposed via `camera_from_obs(obs)` for use with rendered videos.

---

## Repository layout

```
rugby_sim/              # user-facing Python entrypoint (create_environment, scenarios)
rugby_core/             # Python package — env wrappers, scenarios, observation processing
rugby_engine/           # symlink → third_party/rugby_engine (native engine)
third_party/rugby_engine/
    src/                # C++ engine source (match, referee, AI, humanoid, rendering)
    data/               # assets (pitch overlay, kits, H-post meshes, ball, fonts)
    CMakeLists.txt
tools/                  # dataset generator, QA, video render, asset generators
docs/                   # project docs + previews
```

### Tools

| Tool | Purpose |
|---|---|
| `tools/generate_training_data.py` | N-match dataset: video + JSON + events |
| `tools/generate_rugby_dataset.py` | Lighter variant: parquet per tick + event log |
| `tools/project_bboxes.py` | 3-D → 2-D pixel-bbox helpers |
| `tools/qa_report.py` | Single-command readiness check |
| `tools/render_clip.py` / `render_clip_15s.py` / `render_clip_30s.py` / `render_open_play_15s.py` | Standalone video renders |
| `tools/generate_rugby_goals_ase.py` | H-post mesh generator (`goals.ase`) |
| `tools/generate_rugby_kits.py` | Hooped jersey BMP generator |
| `tools/generate_rugby_radar.py` | Rugby minimap BMP generator |
| `tools/ovalize_ball_ase.py` | Oval rugby-ball mesh generator |

---

## Observation space

Each env step returns a dict with:

- **Ball**: `ball` (x,y,z), `ball_direction`, `ball_rotation`,
  `ball_owned_team`, `ball_owned_player`.
- **Teams**: `left_team` + `right_team` (15 × 2 positions each),
  `left_team_direction`, `left_team_roles`, `left_team_active`,
  `left_team_tired_factor`, same for `right_team`.
- **Rugby state**: `rugby_breakdown_active`, `rugby_breakdown_team`,
  `rugby_breakdown_position`, `rugby_scrum_active`,
  `rugby_scrum_winning_team`, `rugby_lineout_active`,
  `rugby_lineout_winning_team`, `rugby_offside_line`,
  `rugby_ball_retainer_team`, `rugby_is_in_set_piece`,
  `rugby_actual_time_ms`.
- **Camera** (for 3-D → 2-D projection): `camera_position`,
  `camera_orientation` (quaternion xyzw), `camera_fov`,
  `camera_near`, `camera_far`, `camera_view_width`,
  `camera_view_height`.
- **Game**: `game_mode`, `score`, `steps_left`.

---

## Action set

22 discrete actions, indexed 0 – 21:

```
idle, left, top_left, top, top_right, right, bottom_right, bottom,
bottom_left, rugby_pass, spin_pass, box_kick, grubber_kick, tackle,
contest, bind, offload, sprint, release_direction, release_tackle,
release_contest, release_bind, release_sprint
```

See `rugby_core/env/rugby_action_set.py`.

---

## Attribution

RugbySim is built on top of
[`google-research/football`](https://github.com/google-research/football),
which itself builds on Bastiaan Konings Schuiling's *Gameplay Football*
engine. Licensed under Apache 2.0 — see [`LICENSE`](LICENSE) and
[`NOTICE`](NOTICE) for attribution details.

Modified files retain their original copyright headers. New
rugby-specific files are Apache 2.0 under the top-level repository
copyright.
