# coding=utf-8
"""Compatibility layer for gym/gymnasium."""

try:
  import gym as _gym
  from gym.envs.registration import register
except ImportError:
  import gymnasium as _gym
  from gymnasium.envs.registration import register

gym = _gym
Env = _gym.Env
Wrapper = _gym.Wrapper
ObservationWrapper = _gym.ObservationWrapper
RewardWrapper = _gym.RewardWrapper
spaces = _gym.spaces
make = _gym.make
