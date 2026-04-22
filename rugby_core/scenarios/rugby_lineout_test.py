# coding=utf-8

from . import *


_LEFT_PLAYERS = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (-0.18, 0.30, e_PlayerRole_CB, False),
    (-0.10, 0.30, e_PlayerRole_CB, False),
    (-0.02, 0.30, e_PlayerRole_DM, False),
    (0.06, 0.30, e_PlayerRole_CM, False),
    (0.14, 0.30, e_PlayerRole_CM, False),
    (0.22, 0.30, e_PlayerRole_AM, False),
    (0.30, 0.30, e_PlayerRole_CF, False),
    (-0.70, -0.28, e_PlayerRole_LB, True),
    (-0.66, -0.14, e_PlayerRole_CB, True),
    (-0.64, 0.00, e_PlayerRole_CB, True),
    (-0.62, 0.14, e_PlayerRole_RB, True),
    (-0.48, -0.22, e_PlayerRole_DM, True),
    (-0.44, -0.02, e_PlayerRole_CM, True),
    (-0.40, 0.18, e_PlayerRole_CF, True),
]

_RIGHT_PLAYERS = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (-0.18, 0.24, e_PlayerRole_CB, False),
    (-0.10, 0.24, e_PlayerRole_CB, False),
    (-0.02, 0.24, e_PlayerRole_DM, False),
    (0.06, 0.24, e_PlayerRole_CM, False),
    (0.14, 0.24, e_PlayerRole_CM, False),
    (0.22, 0.24, e_PlayerRole_AM, False),
    (0.30, 0.24, e_PlayerRole_CF, False),
    (-0.68, -0.30, e_PlayerRole_LB, True),
    (-0.64, -0.16, e_PlayerRole_CB, True),
    (-0.62, -0.02, e_PlayerRole_CB, True),
    (-0.60, 0.12, e_PlayerRole_RB, True),
    (-0.46, -0.24, e_PlayerRole_DM, True),
    (-0.42, -0.04, e_PlayerRole_CM, True),
    (-0.38, 0.16, e_PlayerRole_CF, True),
]


def _add_team(builder, players):
  for x, y, role, lazy in players:
    builder.AddPlayer(x, y, role, lazy=lazy)


def build_scenario(builder):
  builder.config().game_duration = 400
  builder.config().deterministic = True
  builder.config().offsides = False
  builder.config().end_episode_on_score = False
  builder.config().end_episode_on_out_of_play = False
  builder.config().end_episode_on_possession_change = False
  builder.config().left_team_difficulty = 1.0
  builder.config().right_team_difficulty = 1.0
  builder.SetBallPosition(0.05, 0.46)

  builder.SetTeam(Team.e_Left)
  _add_team(builder, _LEFT_PLAYERS)

  builder.SetTeam(Team.e_Right)
  _add_team(builder, _RIGHT_PLAYERS)
