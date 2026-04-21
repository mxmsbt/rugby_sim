# coding=utf-8

from . import *


_LEFT_TEAM = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (1.04, 0.00, e_PlayerRole_CM, False),
    (0.72, -0.12, e_PlayerRole_DM, True),
    (0.72, 0.12, e_PlayerRole_DM, True),
    (0.56, -0.24, e_PlayerRole_CB, True),
    (0.56, -0.08, e_PlayerRole_CB, True),
    (0.56, 0.08, e_PlayerRole_CB, True),
    (0.56, 0.24, e_PlayerRole_CB, True),
    (0.40, -0.28, e_PlayerRole_LB, True),
    (0.40, -0.14, e_PlayerRole_CM, True),
    (0.40, 0.00, e_PlayerRole_CM, True),
    (0.40, 0.14, e_PlayerRole_CM, True),
    (0.40, 0.28, e_PlayerRole_RB, True),
    (0.22, -0.18, e_PlayerRole_AM, True),
    (0.22, 0.18, e_PlayerRole_CF, True),
]

_RIGHT_TEAM = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (-0.12, -0.24, e_PlayerRole_CB, True),
    (-0.12, -0.08, e_PlayerRole_CB, True),
    (-0.12, 0.08, e_PlayerRole_CB, True),
    (-0.12, 0.24, e_PlayerRole_CB, True),
    (-0.28, -0.28, e_PlayerRole_DM, True),
    (-0.28, -0.14, e_PlayerRole_CM, True),
    (-0.28, 0.00, e_PlayerRole_CM, True),
    (-0.28, 0.14, e_PlayerRole_CM, True),
    (-0.28, 0.28, e_PlayerRole_DM, True),
    (-0.44, -0.24, e_PlayerRole_LB, True),
    (-0.44, -0.08, e_PlayerRole_CB, True),
    (-0.44, 0.08, e_PlayerRole_CB, True),
    (-0.44, 0.24, e_PlayerRole_RB, True),
    (-0.60, 0.00, e_PlayerRole_CF, True),
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
  builder.config().rugby_force_initial_try = True
  builder.config().initial_ball_owner_team = 0
  builder.config().initial_ball_owner_player = 1
  builder.SetBallPosition(0.92, 0.0)

  builder.SetTeam(Team.e_Left)
  _add_team(builder, _LEFT_TEAM)

  builder.SetTeam(Team.e_Right)
  _add_team(builder, _RIGHT_TEAM)
