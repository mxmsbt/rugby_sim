# coding=utf-8

from . import *


RUGBY_LINEUP = [
    (-1.00, 0.00, e_PlayerRole_GK),
    (-0.66, -0.34, e_PlayerRole_LB),
    (-0.69, -0.22, e_PlayerRole_CB),
    (-0.70, -0.10, e_PlayerRole_CB),
    (-0.69, 0.00, e_PlayerRole_CB),
    (-0.70, 0.10, e_PlayerRole_CB),
    (-0.69, 0.22, e_PlayerRole_CB),
    (-0.66, 0.34, e_PlayerRole_RB),
    (-0.48, -0.20, e_PlayerRole_DM),
    (-0.48, 0.00, e_PlayerRole_DM),
    (-0.48, 0.20, e_PlayerRole_DM),
    (-0.26, -0.24, e_PlayerRole_CM),
    (-0.24, -0.04, e_PlayerRole_AM),
    (-0.24, 0.14, e_PlayerRole_CM),
    (-0.08, 0.30, e_PlayerRole_CF),
]


def _add_team(builder, modifier_x, modifier_y):
  for x, y, role in RUGBY_LINEUP:
    builder.AddPlayer(x + modifier_x, y + modifier_y, role)


def build_scenario(builder):
  builder.config().game_duration = 3600
  builder.config().right_team_difficulty = 0.7
  builder.config().deterministic = False
  builder.SetBallPosition(-0.12, 0.0)

  if builder.EpisodeNumber() % 2 == 0:
    attack_team = Team.e_Left
    defend_team = Team.e_Right
    attack_shift = 0.0
    defend_shift = 0.0
  else:
    attack_team = Team.e_Right
    defend_team = Team.e_Left
    attack_shift = 0.0
    defend_shift = 0.0

  builder.SetTeam(attack_team)
  _add_team(builder, attack_shift, 0.0)
  builder.SetTeam(defend_team)
  _add_team(builder, defend_shift - 0.12, 0.0)
