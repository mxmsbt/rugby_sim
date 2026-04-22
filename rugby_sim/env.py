# coding=utf-8
"""High-level RugbySim environment entrypoints."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from rugby_core.env import create_environment as _create_environment


def create_environment(*args, **kwargs):
  """Create a rugby-oriented environment with rugby defaults."""
  kwargs.setdefault("env_name", "rugby_15_vs_15")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  if (kwargs.get("env_name") == "rugby_15_vs_15" and
      kwargs.get("representation") in ("simple115", "simple115v2")):
    raise ValueError(
        "rugby_15_vs_15 requires 'raw', 'extracted', or pixel observations; "
        "simple115 formats are hard-coded for 11v11 football.")
  return _create_environment(*args, **kwargs)


def create_breakdown_test_environment(*args, **kwargs):
  """Create a deterministic rugby breakdown test environment."""
  kwargs.setdefault("env_name", "rugby_breakdown_test")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  return _create_environment(*args, **kwargs)


def create_lineout_test_environment(*args, **kwargs):
  """Create a deterministic rugby lineout test environment."""
  kwargs.setdefault("env_name", "rugby_lineout_test")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  return _create_environment(*args, **kwargs)


def create_scrum_test_environment(*args, **kwargs):
  """Create a deterministic rugby scrum test environment."""
  kwargs.setdefault("env_name", "rugby_scrum_test")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  return _create_environment(*args, **kwargs)


def create_forward_pass_test_environment(*args, **kwargs):
  """Create a deterministic rugby forward-pass infringement environment."""
  kwargs.setdefault("env_name", "rugby_forward_pass_test")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  return _create_environment(*args, **kwargs)


def create_knock_on_test_environment(*args, **kwargs):
  """Create a deterministic rugby knock-on infringement environment."""
  kwargs.setdefault("env_name", "rugby_knock_on_test")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  return _create_environment(*args, **kwargs)


def create_try_restart_test_environment(*args, **kwargs):
  """Create a deterministic rugby try-to-restart environment."""
  kwargs.setdefault("env_name", "rugby_try_restart_test")
  kwargs.setdefault("representation", "raw")
  kwargs.setdefault("number_of_left_players_agent_controls", 15)
  other_config_options = dict(kwargs.pop("other_config_options", {}))
  other_config_options.setdefault("action_set", "rugby")
  kwargs["other_config_options"] = other_config_options
  return _create_environment(*args, **kwargs)
