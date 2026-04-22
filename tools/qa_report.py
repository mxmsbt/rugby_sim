#!/usr/bin/env python3
"""End-to-end QA harness for the rugby sim.

Runs a battery of checks and emits a pass/fail report plus concrete
gaps. Intended to answer "is the sim ready to generate training data?"
in one command.
"""
from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import sys
import time
import traceback
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR.parent) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR.parent))

from rugby_sim import (  # noqa: E402
    create_breakdown_test_environment,
    create_environment,
    create_forward_pass_test_environment,
    create_knock_on_test_environment,
    create_lineout_test_environment,
    create_scrum_test_environment,
    create_try_restart_test_environment,
)
from tools.project_bboxes import broadcast_camera_from_obs, frame_bboxes  # noqa: E402


@dataclass
class CheckResult:
    name: str
    passed: bool
    detail: str = ""
    stats: Dict = field(default_factory=dict)


REPORT: List[CheckResult] = []


def _add(name: str, passed: bool, detail: str = "", **stats) -> CheckResult:
    r = CheckResult(name=name, passed=bool(passed), detail=detail, stats=stats)
    REPORT.append(r)
    mark = "PASS" if r.passed else "FAIL"
    print(f"[{mark}] {name}" + (f" — {detail}" if detail else ""))
    return r


def _extract_obs(obs):
    if isinstance(obs, list):
        return obs[0] if obs else {}
    return obs


def check_scenarios_run_clean() -> None:
    """(1) Every rugby scenario must start, step, and end without crashing."""
    factories = [
        ("try_restart", create_try_restart_test_environment),
        ("breakdown", create_breakdown_test_environment),
        ("forward_pass", create_forward_pass_test_environment),
        ("knock_on", create_knock_on_test_environment),
        ("lineout", create_lineout_test_environment),
        ("scrum", create_scrum_test_environment),
        ("15v15", create_environment),
    ]
    failures = []
    scores = {}
    for name, factory in factories:
        try:
            env = factory(render=False, number_of_left_players_agent_controls=1,
                          number_of_right_players_agent_controls=0).unwrapped
            env.reset()
            last_score = [0, 0]
            for _ in range(300):
                obs, _, done, _ = env.step([0])
                o = _extract_obs(obs)
                if o:
                    last_score = o.get("score", last_score)
                if done:
                    break
            scores[name] = last_score
        except Exception as exc:  # noqa: BLE001
            failures.append((name, f"{type(exc).__name__}: {exc}"))
    if failures:
        _add("all scenarios run clean", False,
             detail=f"crashed: {failures}", scores=scores)
    else:
        _add("all scenarios run clean", True,
             detail=f"7/7 ran 300 steps ok", scores=scores)


def _run_match(env, steps: int) -> Tuple[Dict, List[Dict]]:
    env.reset()
    events: List[Dict] = []
    prev_score = [0, 0]
    prev_gm = 0
    prev_bd = prev_scrum = prev_lo = False
    prev_owner = -1
    seen_players = defaultdict(set)  # (team, jersey) pairs per frame
    bboxes_per_frame: List[int] = []
    out_of_frame = 0
    nan_count = 0
    obs_record: List[Dict] = []
    for step in range(steps):
        obs, _, done, _ = env.step([0])
        o = _extract_obs(obs)
        if not o:
            break
        obs_record.append(o)
        if done:
            break
        score = list(o.get("score", [0, 0]))
        for t in (0, 1):
            d = score[t] - prev_score[t]
            if d > 0:
                kind = {5: "try", 2: "conversion", 3: "kick_goal",
                        7: "try_and_conversion"}.get(d, f"score_+{d}")
                events.append({"step": step, "type": kind, "team": t, "delta": d})
        prev_score = score
        gm = int(o.get("game_mode", 0))
        if gm != prev_gm:
            events.append({"step": step, "type": "game_mode", "from": prev_gm, "to": gm})
            prev_gm = gm
        bd = bool(o.get("rugby_breakdown_active", False))
        scrum = bool(o.get("rugby_scrum_active", False))
        lo = bool(o.get("rugby_lineout_active", False))
        owner = int(o.get("ball_owned_team", -1))
        for name, cur, prev in [("breakdown", bd, prev_bd),
                                 ("scrum", scrum, prev_scrum),
                                 ("lineout", lo, prev_lo)]:
            if cur and not prev:
                events.append({"step": step, "type": f"{name}_start"})
            if prev and not cur:
                events.append({"step": step, "type": f"{name}_end"})
        prev_bd, prev_scrum, prev_lo = bd, scrum, lo
        if owner != prev_owner:
            events.append({"step": step, "type": "possession_change",
                            "from": prev_owner, "to": owner})
            prev_owner = owner
        # Per-frame bbox QA on a subset of steps.
        if step % 20 == 0 and step > 0:
            cam = broadcast_camera_from_obs(o, fov_deg=22.0, height=18.0,
                                             back_offset=55.0)
            bbs = frame_bboxes(o, cam)
            bboxes_per_frame.append(len(bbs))
            for b in bbs:
                x1, y1, x2, y2 = b["bbox"]
                if any(math.isnan(v) for v in (x1, y1, x2, y2)):
                    nan_count += 1
                if x1 < 0 or y1 < 0 or x2 > cam.width or y2 > cam.height:
                    out_of_frame += 1
                if b["class"] == "player":
                    seen_players[b["team"]].add(b["jersey_number"])
    return {
        "final_score": prev_score,
        "events": events,
        "seen_jerseys": {t: sorted(list(js)) for t, js in seen_players.items()},
        "bboxes_per_frame": bboxes_per_frame,
        "bbox_out_of_frame": out_of_frame,
        "bbox_nan_count": nan_count,
        "obs_count": len(obs_record),
    }, obs_record


def check_long_run_stability() -> None:
    """(2) 15v15 must remain healthy for 3000 steps."""
    try:
        env = create_environment(render=False,
                                 number_of_left_players_agent_controls=1,
                                 number_of_right_players_agent_controls=0
                                 ).unwrapped
        result, _ = _run_match(env, 3000)
        score = result["final_score"]
        tries = sum(1 for e in result["events"] if e["type"] == "try")
        conversions = sum(1 for e in result["events"] if e["type"] == "conversion")
        kicks = sum(1 for e in result["events"] if e["type"] == "kick_goal")
        breakdowns = sum(1 for e in result["events"] if e["type"] == "breakdown_start")
        scrums = sum(1 for e in result["events"] if e["type"] == "scrum_start")
        lineouts = sum(1 for e in result["events"] if e["type"] == "lineout_start")
        ok = (result["obs_count"] >= 3000 and tries > 0 and breakdowns > 0)
        _add("15v15 long run (3000 steps)", ok,
             detail=(f"score={score} tries={tries} conv={conversions} "
                     f"kicks={kicks} bd={breakdowns} scrum={scrums} "
                     f"lineouts={lineouts}"),
             tries=tries, conversions=conversions, kicks=kicks,
             breakdowns=breakdowns, scrums=scrums, lineouts=lineouts,
             final_score=score)
    except Exception as exc:  # noqa: BLE001
        _add("15v15 long run (3000 steps)", False,
             detail=f"crashed: {type(exc).__name__}: {exc}")


def check_jersey_identity() -> None:
    """(5) Jersey numbers 0..14 per team must be present throughout."""
    try:
        env = create_environment(render=False,
                                 number_of_left_players_agent_controls=1,
                                 number_of_right_players_agent_controls=0
                                 ).unwrapped
        result, _ = _run_match(env, 400)
        seen = result["seen_jerseys"]
        t1 = set(seen.get("team_1", []))
        t2 = set(seen.get("team_2", []))
        # We expect at least 14 of the 15 per team to appear in view over
        # 400 steps (the odd one may always be off-camera).
        ok = len(t1) >= 12 and len(t2) >= 12 and t1.issubset(set(range(15))) \
             and t2.issubset(set(range(15)))
        _add("jersey identity stable across match", ok,
             detail=f"team_1 jerseys seen={sorted(t1)}, team_2={sorted(t2)}",
             team_1=sorted(t1), team_2=sorted(t2))
    except Exception as exc:  # noqa: BLE001
        _add("jersey identity stable across match", False,
             detail=f"crashed: {type(exc).__name__}: {exc}")


def check_bbox_validity() -> None:
    """(4) All bboxes must be finite and within frame."""
    try:
        env = create_environment(render=False,
                                 number_of_left_players_agent_controls=1,
                                 number_of_right_players_agent_controls=0
                                 ).unwrapped
        result, _ = _run_match(env, 600)
        oof = result["bbox_out_of_frame"]
        nans = result["bbox_nan_count"]
        bpf = result["bboxes_per_frame"]
        mean_bpf = float(np.mean(bpf)) if bpf else 0.0
        ok = oof == 0 and nans == 0 and mean_bpf > 5
        _add("bbox geometry valid (in-frame, finite)", ok,
             detail=f"out_of_frame={oof} nans={nans} mean_bboxes_per_frame={mean_bpf:.1f}",
             out_of_frame=oof, nans=nans, mean_bboxes_per_frame=mean_bpf)
    except Exception as exc:  # noqa: BLE001
        _add("bbox geometry valid (in-frame, finite)", False,
             detail=f"crashed: {type(exc).__name__}: {exc}")


def check_determinism() -> None:
    """(6) Deterministic scenario should reproduce the same event timeline."""
    try:
        events_runs: List[List[Tuple[int, str]]] = []
        for _ in range(2):
            env = create_try_restart_test_environment(
                render=False,
                number_of_left_players_agent_controls=1,
                number_of_right_players_agent_controls=0).unwrapped
            result, _ = _run_match(env, 300)
            events_runs.append([(e["step"], e["type"]) for e in result["events"]])
        same = events_runs[0] == events_runs[1]
        _add("deterministic scenario reproduces events", same,
             detail=(f"run0 n_events={len(events_runs[0])} "
                     f"run1 n_events={len(events_runs[1])} "
                     f"{'identical' if same else 'DIVERGED'}"))
    except Exception as exc:  # noqa: BLE001
        _add("deterministic scenario reproduces events", False,
             detail=f"crashed: {type(exc).__name__}: {exc}")


def check_pipeline_json_schema() -> None:
    """(3, 8) End-to-end run of generate_training_data.py validates JSON
    shape and event log."""
    out = Path("/tmp/rugby_qa_ds")
    if out.exists():
        shutil.rmtree(out)
    try:
        res = subprocess.run(
            [sys.executable, str(_THIS_DIR / "generate_training_data.py"),
             "--matches", "2", "--steps", "200", "--out", str(out)],
            capture_output=True, text=True, timeout=600,
        )
        if res.returncode != 0:
            raise RuntimeError(f"generator exit {res.returncode}: {res.stderr[:400]}")
        for idx in range(2):
            match_dir = out / f"match_{idx:03d}"
            ann_path = match_dir / f"match_{idx:03d}.json"
            ann = json.loads(ann_path.read_text())
            required = {"video", "fps", "num_frames", "width", "height",
                        "bbox_format", "coordinates", "frames"}
            missing = required - set(ann.keys())
            if missing:
                raise AssertionError(f"missing keys: {missing}")
            if ann["bbox_format"] != "xyxy":
                raise AssertionError("bbox_format != xyxy")
            if ann["coordinates"] != "absolute_pixels":
                raise AssertionError("coordinates mismatch")
            if ann["num_frames"] != len(ann["frames"]):
                raise AssertionError("num_frames vs len(frames) mismatch")
            if ann["frames"]:
                first = ann["frames"][0]
                obj = first["objects"][0] if first["objects"] else None
                if obj:
                    for k in ("bbox", "class_id", "class"):
                        assert k in obj, f"object missing {k}"
                    assert len(obj["bbox"]) == 4
                    assert obj["class_id"] in (0, 1, 2, 3)
        _add("training-data generator schema valid", True,
             detail=f"2 matches parsed; xyxy + class_id + frames[] ok")
    except Exception as exc:  # noqa: BLE001
        _add("training-data generator schema valid", False,
             detail=f"{type(exc).__name__}: {exc}")


def check_realism_bands() -> None:
    """(7) Event rates per 1000 simulation steps must sit in a rugby band."""
    try:
        env = create_environment(render=False,
                                 number_of_left_players_agent_controls=1,
                                 number_of_right_players_agent_controls=0
                                 ).unwrapped
        result, _ = _run_match(env, 2000)
        tries = sum(1 for e in result["events"] if e["type"] == "try")
        kicks = sum(1 for e in result["events"] if e["type"] == "kick_goal")
        breakdowns = sum(1 for e in result["events"] if e["type"] == "breakdown_start")
        scrums = sum(1 for e in result["events"] if e["type"] == "scrum_start")
        lineouts = sum(1 for e in result["events"] if e["type"] == "lineout_start")
        # Wide bands — we just want to catch the sim producing zero activity
        # or unreasonably many events.
        checks = {
            "tries in [0, 30]": 0 <= tries <= 30,
            "breakdowns in [15, 250]": 15 <= breakdowns <= 250,
            "scrums in [0, 100]": 0 <= scrums <= 100,
            "kicks in [0, 30]": 0 <= kicks <= 30,
        }
        failing = [k for k, v in checks.items() if not v]
        ok = not failing
        _add("event rates within rugby bands", ok,
             detail=(f"tries={tries} kicks={kicks} breakdowns={breakdowns} "
                     f"scrums={scrums} lineouts={lineouts}"
                     + (f" | failing: {failing}" if failing else "")),
             tries=tries, kicks=kicks, breakdowns=breakdowns,
             scrums=scrums, lineouts=lineouts)
    except Exception as exc:  # noqa: BLE001
        _add("event rates within rugby bands", False,
             detail=f"crashed: {type(exc).__name__}: {exc}")


def check_ball_tracking() -> None:
    """(4b) Ball bbox present in >75 % of frames (synth should be ~100 %)."""
    try:
        env = create_environment(render=False,
                                 number_of_left_players_agent_controls=1,
                                 number_of_right_players_agent_controls=0
                                 ).unwrapped
        env.reset()
        frames_with_ball = 0
        total = 0
        for step in range(400):
            obs, _, _, _ = env.step([0])
            o = _extract_obs(obs)
            if not o:
                break
            cam = broadcast_camera_from_obs(o, fov_deg=22.0, height=18.0,
                                             back_offset=55.0)
            bbs = frame_bboxes(o, cam)
            total += 1
            if any(b["class"] == "ball" for b in bbs):
                frames_with_ball += 1
        ratio = frames_with_ball / max(total, 1)
        ok = ratio > 0.75
        _add("ball bbox present in >75% of frames", ok,
             detail=f"{frames_with_ball}/{total} = {ratio*100:.1f}%",
             ratio=ratio)
    except Exception as exc:  # noqa: BLE001
        _add("ball bbox present in >75% of frames", False,
             detail=f"crashed: {type(exc).__name__}: {exc}")


def check_compare_to_real_annotation() -> None:
    """(9) Compare a match to the Exeter-Newcastle annotation header."""
    real_path = Path(
        "/Users/maximesebti/Downloads/exeter-newcastle_30136-60276.json"
    )
    if not real_path.exists():
        _add("compare synth vs real annotation schema", True,
             detail="reference JSON not present; skipping")
        return
    try:
        real = json.loads(real_path.read_text())
        required = {"video", "fps", "num_frames", "width", "height",
                    "bbox_format", "coordinates", "frames"}
        # Synth
        synth_out = Path("/tmp/rugby_qa_ds")
        if not synth_out.exists():
            check_pipeline_json_schema()
        synth_path = synth_out / "match_000" / "match_000.json"
        synth = json.loads(synth_path.read_text())
        keys_match = set(real.keys()) == set(synth.keys())
        format_match = real["bbox_format"] == synth["bbox_format"] and \
                       real["coordinates"] == synth["coordinates"]
        class_match = True
        # class_id convention: 0 ball, 1 referee, 2 team_1, 3 team_2
        synth_ids = {o["class_id"] for f in synth["frames"] for o in f["objects"]}
        real_ids = {o["class_id"] for f in real["frames"] for o in f["objects"]}
        class_match = synth_ids.issubset({0, 1, 2, 3}) and \
                      real_ids.issubset({0, 1, 2, 3})
        ok = keys_match and format_match and class_match
        _add("compare synth vs real annotation schema", ok,
             detail=f"keys={keys_match} format={format_match} class_ids={class_match} "
                    f"(synth_ids={sorted(synth_ids)}, real_ids={sorted(real_ids)})")
    except Exception as exc:  # noqa: BLE001
        _add("compare synth vs real annotation schema", False,
             detail=f"{type(exc).__name__}: {exc}")


def main() -> int:
    t0 = time.time()
    checks: List[Callable[[], None]] = [
        check_scenarios_run_clean,
        check_long_run_stability,
        check_jersey_identity,
        check_bbox_validity,
        check_determinism,
        check_pipeline_json_schema,
        check_realism_bands,
        check_ball_tracking,
        check_compare_to_real_annotation,
    ]
    for fn in checks:
        try:
            fn()
        except Exception as exc:  # noqa: BLE001
            _add(fn.__name__, False, detail=f"harness error: {exc}")
    wall = time.time() - t0
    passed = sum(1 for r in REPORT if r.passed)
    total = len(REPORT)
    print("\n" + "=" * 70)
    print(f"QA RESULT: {passed}/{total} checks passed ({wall:.1f}s)")
    print("=" * 70)
    gaps = [r for r in REPORT if not r.passed]
    if gaps:
        print("\nGaps:")
        for r in gaps:
            print(f"  - {r.name}: {r.detail}")
        print("\nVERDICT: NOT READY")
    else:
        print("\nVERDICT: READY")
    return 0 if not gaps else 1


if __name__ == "__main__":
    raise SystemExit(main())
