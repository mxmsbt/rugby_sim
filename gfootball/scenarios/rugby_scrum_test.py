# coding=utf-8

from . import *


_LEFT_PACK = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (-0.16, -0.10, e_PlayerRole_CB, False),
    (-0.14, 0.00, e_PlayerRole_CB, False),
    (-0.16, 0.10, e_PlayerRole_CB, False),
    (-0.08, -0.12, e_PlayerRole_DM, False),
    (-0.06, 0.00, e_PlayerRole_DM, False),
    (-0.08, 0.12, e_PlayerRole_DM, False),
    (0.02, -0.06, e_PlayerRole_CM, False),
    (0.02, 0.06, e_PlayerRole_CM, False),
    (-0.30, -0.24, e_PlayerRole_AM, True),
    (-0.28, -0.08, e_PlayerRole_CM, True),
    (-0.28, 0.08, e_PlayerRole_CM, True),
    (-0.30, 0.24, e_PlayerRole_CF, True),
    (-0.46, -0.20, e_PlayerRole_CB, True),
    (-0.46, 0.20, e_PlayerRole_RB, True),
]

_RIGHT_PACK = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (0.16, -0.10, e_PlayerRole_CB, False),
    (0.14, 0.00, e_PlayerRole_CB, False),
    (0.16, 0.10, e_PlayerRole_CB, False),
    (0.08, -0.12, e_PlayerRole_DM, False),
    (0.06, 0.00, e_PlayerRole_DM, False),
    (0.08, 0.12, e_PlayerRole_DM, False),
    (-0.02, -0.06, e_PlayerRole_CM, False),
    (-0.02, 0.06, e_PlayerRole_CM, False),
    (-0.34, -0.24, e_PlayerRole_AM, True),
    (-0.32, -0.08, e_PlayerRole_CM, True),
    (-0.32, 0.08, e_PlayerRole_CM, True),
    (-0.34, 0.24, e_PlayerRole_CF, True),
    (-0.50, -0.20, e_PlayerRole_CB, True),
    (-0.50, 0.20, e_PlayerRole_RB, True),
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
  builder.SetBallPosition(1.06, 0.0)

  builder.SetTeam(Team.e_Left)
  _add_team(builder, _LEFT_PACK)

  builder.SetTeam(Team.e_Right)
  _add_team(builder, _RIGHT_PACK)
