# RugbySim Engine Rewrite Plan

## Goal

Turn the football-specific engine into a rugby union simulator that supports:

- 15 vs 15 team structure
- contact phases: tackle, ruck, maul
- set pieces: scrum, lineout, restart kicks
- law enforcement: offside, knock-on, forward pass, touch, penalties
- rugby scoring: try, conversion, penalty goal, drop goal

## Rewrite Strategy

The current engine still contains football semantics in the following layers:

1. `third_party/gfootball_engine/src/onthepitch/match.*`
2. `third_party/gfootball_engine/src/onthepitch/referee.*`
3. `third_party/gfootball_engine/src/onthepitch/teamAIcontroller.*`
4. `third_party/gfootball_engine/src/onthepitch/player/controller/*`
5. `gfootball/env/*` action, observation, and reward wrappers

## Current Bootstrap Work

This fork introduces:

- `rugby_sim` Python package entrypoint
- rugby action names in the engine binding layer
- packaging and documentation rebranded around RugbySim

The actual engine rewrite should proceed in this order:

1. Replace football actions with rugby carrier/contact/kicking primitives.
2. Replace football game modes with rugby phases and restart states.
3. Introduce a rugby possession state machine for tackle-to-ruck transitions.
4. Expand team/player roles from football defaults to rugby positions.
5. Update observation schemas and scenario builders for rugby drills and full matches.
