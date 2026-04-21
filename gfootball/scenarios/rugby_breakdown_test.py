# coding=utf-8

from . import *


_LEFT_CORE = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (-0.02, 0.00, e_PlayerRole_CM, False),
    (-0.10, 0.03, e_PlayerRole_DM, False),
    (-0.16, -0.05, e_PlayerRole_CB, False),
]

_RIGHT_CORE = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (0.03, 0.00, e_PlayerRole_CB, False),
    (0.10, -0.03, e_PlayerRole_DM, False),
    (0.16, 0.05, e_PlayerRole_CB, False),
]

_LEFT_FILL = [
    (-0.70, -0.32, e_PlayerRole_LB),
    (-0.72, -0.18, e_PlayerRole_CB),
    (-0.70, -0.06, e_PlayerRole_CB),
    (-0.70, 0.08, e_PlayerRole_CB),
    (-0.70, 0.22, e_PlayerRole_RB),
    (-0.52, -0.26, e_PlayerRole_DM),
    (-0.50, -0.12, e_PlayerRole_CM),
    (-0.50, 0.02, e_PlayerRole_CM),
    (-0.50, 0.18, e_PlayerRole_CM),
    (-0.34, -0.24, e_PlayerRole_AM),
    (-0.34, 0.14, e_PlayerRole_CF),
]

_RIGHT_FILL = [
    (-0.66, -0.30, e_PlayerRole_LB),
    (-0.68, -0.16, e_PlayerRole_CB),
    (-0.68, -0.02, e_PlayerRole_CB),
    (-0.68, 0.12, e_PlayerRole_CB),
    (-0.66, 0.26, e_PlayerRole_RB),
    (-0.48, -0.24, e_PlayerRole_DM),
    (-0.46, -0.10, e_PlayerRole_CM),
    (-0.46, 0.04, e_PlayerRole_CM),
    (-0.46, 0.18, e_PlayerRole_CM),
    (-0.30, -0.22, e_PlayerRole_AM),
    (-0.30, 0.16, e_PlayerRole_CF),
]


def _add_team(builder, core, fillers):
  for x, y, role, lazy in core:
    builder.AddPlayer(x, y, role, lazy=lazy)
  for x, y, role in fillers:
    builder.AddPlayer(x, y, role, lazy=True)


def build_scenario(builder):
  builder.config().game_duration = 400
  builder.config().deterministic = True
  builder.config().offsides = False
  builder.config().end_episode_on_score = False
  builder.config().end_episode_on_out_of_play = False
  builder.config().end_episode_on_possession_change = False
  builder.config().left_team_difficulty = 1.0
  builder.config().right_team_difficulty = 1.0
  builder.config().rugby_force_initial_breakdown = True
  builder.SetBallPosition(-0.02, 0.0)

  if builder.EpisodeNumber() % 2 == 0:
    attack_team = Team.e_Left
    defend_team = Team.e_Right
  else:
    attack_team = Team.e_Right
    defend_team = Team.e_Left

  builder.SetTeam(attack_team)
  _add_team(builder, _LEFT_CORE, _LEFT_FILL)

  builder.SetTeam(defend_team)
  _add_team(builder, _RIGHT_CORE, _RIGHT_FILL)
