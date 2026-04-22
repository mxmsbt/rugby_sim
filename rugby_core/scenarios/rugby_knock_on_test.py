# coding=utf-8

from . import *


_LEFT_PLAYERS = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (-0.10, 0.00, e_PlayerRole_CM, False),
    (0.04, 0.00, e_PlayerRole_CM, False),
    (-0.24, -0.08, e_PlayerRole_DM, True),
    (-0.24, 0.08, e_PlayerRole_DM, True),
    (-0.40, -0.18, e_PlayerRole_CB, True),
    (-0.40, 0.18, e_PlayerRole_CB, True),
    (-0.56, -0.24, e_PlayerRole_LB, True),
    (-0.56, -0.08, e_PlayerRole_CB, True),
    (-0.56, 0.08, e_PlayerRole_CB, True),
    (-0.56, 0.24, e_PlayerRole_RB, True),
    (-0.72, -0.22, e_PlayerRole_DM, True),
    (-0.72, -0.04, e_PlayerRole_CM, True),
    (-0.72, 0.14, e_PlayerRole_AM, True),
    (-0.72, 0.28, e_PlayerRole_CF, True),
]

_RIGHT_PLAYERS = [
    (-1.00, 0.00, e_PlayerRole_GK, True),
    (0.22, -0.18, e_PlayerRole_CB, True),
    (0.22, -0.02, e_PlayerRole_CB, True),
    (0.22, 0.14, e_PlayerRole_CB, True),
    (0.38, -0.24, e_PlayerRole_DM, True),
    (0.38, -0.08, e_PlayerRole_CM, True),
    (0.38, 0.08, e_PlayerRole_CM, True),
    (0.38, 0.24, e_PlayerRole_AM, True),
    (0.54, -0.26, e_PlayerRole_LB, True),
    (0.54, -0.10, e_PlayerRole_CB, True),
    (0.54, 0.06, e_PlayerRole_CB, True),
    (0.54, 0.22, e_PlayerRole_RB, True),
    (0.70, -0.20, e_PlayerRole_DM, True),
    (0.70, 0.00, e_PlayerRole_CM, True),
    (0.70, 0.20, e_PlayerRole_CF, True),
]


def _add_team(builder, players):
  for x, y, role, lazy in players:
    builder.AddPlayer(x, y, role, lazy=lazy)


def build_scenario(builder):
  builder.config().game_duration = 240
  builder.config().deterministic = True
  builder.config().offsides = False
  builder.config().end_episode_on_score = False
  builder.config().end_episode_on_out_of_play = False
  builder.config().end_episode_on_possession_change = False
  builder.config().left_team_difficulty = 1.0
  builder.config().right_team_difficulty = 1.0
  builder.config().rugby_force_knock_on_scrum = True
  builder.config().initial_ball_owner_team = 0
  builder.config().initial_ball_owner_player = 1
  builder.SetBallPosition(-0.10, 0.0)

  builder.SetTeam(Team.e_Left)
  _add_team(builder, _LEFT_PLAYERS)

  builder.SetTeam(Team.e_Right)
  _add_team(builder, _RIGHT_PLAYERS)
