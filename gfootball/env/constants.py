# coding=utf-8
# Copyright 2019 Google LLC
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


"""Important environment constants."""

from __future__ import print_function

# How many physics steps game engine does per second.
PHYSICS_STEPS_PER_SECOND = 100

# List of observations exposed by the environment.
EXPOSED_OBSERVATIONS = frozenset({
    'ball', 'ball_direction', 'ball_rotation', 'ball_owned_team',
    'ball_owned_player', 'left_team', 'left_team_direction',
    'left_team_tired_factor', 'left_team_yellow_card', 'left_team_active',
    'left_team_roles', 'right_team', 'right_team_direction',
    'right_team_tired_factor', 'right_team_yellow_card', 'right_team_active',
    'right_team_roles', 'score', 'steps_left', 'game_mode',
    'rugby_breakdown_active', 'rugby_pending_initial_breakdown',
    'rugby_force_initial_breakdown_config', 'rugby_breakdown_team',
    'rugby_breakdown_position', 'rugby_recycle_receiver_team',
    'rugby_recycle_receiver_position', 'rugby_possession_protected_team',
    'rugby_offside_line', 'rugby_ball_retainer_team',
    'rugby_designated_possession_team', 'rugby_is_in_set_piece',
    'rugby_lineout_active', 'rugby_lineout_team',
    'rugby_lineout_winning_team',
    'rugby_scrum_active', 'rugby_scrum_team',
    'rugby_scrum_winning_team',
    'rugby_left_team_offside_line', 'rugby_right_team_offside_line',
    'rugby_left_team_side', 'rugby_right_team_side',
    'rugby_actual_time_ms', 'rugby_breakdown_start_time_ms',
    'camera_position', 'camera_orientation', 'camera_fov', 'camera_near',
    'camera_far', 'camera_view_width', 'camera_view_height',
})
