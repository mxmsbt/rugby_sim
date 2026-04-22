// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "referee.hpp"
#include <algorithm>
#include <cmath>

#include "../scene/objectfactory.hpp"
#include "match.hpp"
#include "AIsupport/AIfunctions.hpp"

#include "../main.hpp"

namespace {

float GetRugbyLineoutSelectionDistance(const Vector3 &restartPos,
                                       const Vector3 &playerPos) {
  const float xDistance = fabs(playerPos.coords[0] - restartPos.coords[0]);
  const float yDistance = fabs(playerPos.coords[1] - restartPos.coords[1]);
  return xDistance + yDistance * 1.7f;
}

std::vector<Player *> GetRugbyLineoutLane(Team *team, const Vector3 &restartPos,
                                          Player *excludePlayer,
                                          unsigned int maxPlayers) {
  std::vector<Player *> players;
  std::vector<Player *> lane;
  if (team == nullptr) {
    return lane;
  }
  team->GetActivePlayers(players);
  for (Player *player : players) {
    if (player == nullptr || !player->IsActive()) continue;
    if (player == excludePlayer) continue;
    lane.push_back(player);
  }
  std::sort(lane.begin(), lane.end(),
            [&](Player *left, Player *right) -> bool {
              return GetRugbyLineoutSelectionDistance(
                         restartPos, left->GetPosition()) <
                     GetRugbyLineoutSelectionDistance(restartPos,
                                                     right->GetPosition());
            });
  if (lane.size() > maxPlayers) {
    lane.resize(maxPlayers);
  }
  std::sort(lane.begin(), lane.end(),
            [](Player *left, Player *right) -> bool {
              return left->GetPosition().coords[0] < right->GetPosition().coords[0];
            });
  return lane;
}

Player *ChooseRugbyLineoutJumper(const std::vector<Player *> &lanePlayers,
                                 float targetX) {
  Player *bestPlayer = nullptr;
  float bestScore = -1000.0f;
  for (Player *player : lanePlayers) {
    const float jumpSkill =
        player->GetStat(technical_header) * 0.45f +
        player->GetStat(physical_reaction) * 0.35f +
        player->GetStat(physical_balance) * 0.20f;
    const float targetBias =
        1.0f - clamp(fabs(player->GetPosition().coords[0] - targetX) / 4.5f,
                     0.0f, 1.0f);
    const float score = jumpSkill * 0.8f + targetBias * 0.2f;
    if (score > bestScore) {
      bestScore = score;
      bestPlayer = player;
    }
  }
  return bestPlayer;
}

float GetRugbyLineoutSupportScore(const std::vector<Player *> &lanePlayers,
                                  Player *jumper) {
  float score = 0.0f;
  int count = 0;
  for (Player *player : lanePlayers) {
    if (player == nullptr || player == jumper) continue;
    score += player->GetStat(mental_workrate) * 0.6f +
             player->GetStat(physical_balance) * 0.4f;
    count++;
  }
  if (count == 0) {
    return 0.5f;
  }
  return score / count;
}

Player *ChooseRugbyLineoutReceiver(Team *team, const Vector3 &restartPos,
                                   Player *jumper, Player *excludePlayer) {
  std::vector<Player *> players;
  if (team == nullptr) {
    return nullptr;
  }
  team->GetActivePlayers(players);

  const float touchSide = signSide(restartPos.coords[1]);
  Player *bestReceiver = nullptr;
  float bestScore = 1000.0f;
  for (Player *player : players) {
    if (player == nullptr || !player->IsActive()) continue;
    if (player == jumper || player == excludePlayer) continue;

    const Vector3 pos = player->GetPosition();
    const bool isInfield =
        touchSide > 0 ? pos.coords[1] < restartPos.coords[1]
                      : pos.coords[1] > restartPos.coords[1];
    if (!isInfield) continue;

    float score = fabs(pos.coords[0] - jumper->GetPosition().coords[0]);
    score += fabs(pos.coords[1] - (restartPos.coords[1] - touchSide * 4.0f)) *
             0.8f;
    if (score < bestScore) {
      bestScore = score;
      bestReceiver = player;
    }
  }

  if (bestReceiver != nullptr) {
    return bestReceiver;
  }
  return jumper;
}

std::vector<Player *> GetRugbyScrumPack(Team *team, const Vector3 &restartPos,
                                        unsigned int maxPlayers) {
  std::vector<Player *> players;
  std::vector<Player *> pack;
  if (team == nullptr) return pack;
  team->GetActivePlayers(players);
  for (Player *player : players) {
    if (player == nullptr || !player->IsActive()) continue;
    pack.push_back(player);
  }
  std::sort(pack.begin(), pack.end(),
            [&](Player *left, Player *right) -> bool {
              return (left->GetPosition() - restartPos).GetLength() <
                     (right->GetPosition() - restartPos).GetLength();
            });
  if (pack.size() > maxPlayers) {
    pack.resize(maxPlayers);
  }
  return pack;
}

float GetRugbyScrumPackScore(const std::vector<Player *> &pack) {
  if (pack.empty()) return 0.5f;
  float score = 0.0f;
  for (Player *player : pack) {
    score += player->GetStat(physical_balance) * 0.45f +
             player->GetStat(physical_stamina) * 0.20f +
             player->GetStat(mental_workrate) * 0.20f +
             player->GetStat(physical_reaction) * 0.15f;
  }
  return score / pack.size();
}

Player *ChooseRugbyScrumReceiver(Team *team, const Vector3 &restartPos,
                                 const std::vector<Player *> &pack) {
  std::vector<Player *> players;
  if (team == nullptr) return nullptr;
  team->GetActivePlayers(players);
  Player *best = nullptr;
  float bestScore = 1000.0f;
  for (Player *player : players) {
    if (player == nullptr || !player->IsActive()) continue;
    if (std::find(pack.begin(), pack.end(), player) != pack.end()) continue;
    float score = fabs(player->GetPosition().coords[0] - restartPos.coords[0]);
    score += fabs(player->GetPosition().coords[1] - restartPos.coords[1]) * 0.8f;
    if (score < bestScore) {
      bestScore = score;
      best = player;
    }
  }
  return best;
}

}  // namespace

void Foul::ProcessState(EnvState *state) {
  state->process(foulPlayer);
  state->process(foulVictim);
  state->process(foulType);
  state->process(advantage);
  state->process(foulTime);
  if (state->getConfig()->reverse_team_processing) {
    foulPosition.Mirror();
  }
  state->process(foulPosition);
  if (state->getConfig()->reverse_team_processing) {
    foulPosition.Mirror();
  }
  state->process(hasBeenProcessed);
}

void RefereeBuffer::ProcessState(EnvState *state) {
  DO_VALIDATION;
  state->process(active);
  state->process(desiredSetPiece);
  if (state->getConfig()->reverse_team_processing) {
    teamID = 1 - teamID;
  }
  state->process(teamID);
  if (state->getConfig()->reverse_team_processing) {
    teamID = 1 - teamID;
  }
  state->process(setpiece_team);
  state->process(stopTime);
  state->process(prepareTime);
  state->process(startTime);
  if (state->getConfig()->reverse_team_processing) {
    restartPos.Mirror();
  }
  state->process(restartPos);
  if (state->getConfig()->reverse_team_processing) {
    restartPos.Mirror();
  }
  state->process(taker);
  state->process(endPhase);
}

Referee::Referee(Match *match, bool animations) : match(match), animations(animations) {
  DO_VALIDATION;
  buffer.desiredSetPiece = e_GameMode_KickOff;
  buffer.teamID = match->FirstTeam();
  buffer.setpiece_team = match->GetTeam(match->FirstTeam());
  buffer.stopTime = 0;
  buffer.prepareTime = 0;
  buffer.startTime = 2000;
  buffer.restartPos = GetScenarioConfig().ball_position;
  buffer.taker = 0;
  buffer.endPhase = true;
  buffer.active = true;

  if (GetScenarioConfig().rugby_force_initial_try) {
    buffer.endPhase = false;
    buffer.active = false;
    match->SetMatchPhase(e_MatchPhase_1stHalf);
    match->StartPlay();
  }

  foul.foulPlayer = 0;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foulTime = 0;
  foul.hasBeenProcessed = true;

  afterSetPieceRelaxTime_ms = 0;
  rugbyKickoffRestartActive = false;
  rugbyForcedForwardPassConsumed = false;
  rugbyForcedKnockOnConsumed = false;
  rugbyLineoutBallLaunched = false;
  rugbyPendingLineoutReceiver = 0;
  rugbyPendingLineoutTeam = 0;
  rugbyLineoutCatchTime_ms = 0;
}

Referee::~Referee() { DO_VALIDATION; }

void Referee::StartRugbyKickoffRestart(int scoringTeamID,
                                       const std::string &message) {
  DO_VALIDATION;
  match->StopPlay();
  buffer.desiredSetPiece = e_GameMode_KickOff;
  buffer.stopTime = match->GetActualTime_ms();
  buffer.prepareTime = match->GetActualTime_ms() + 500;
  buffer.startTime = buffer.prepareTime + 500;
  buffer.restartPos = Vector3(0, 0, 0);
  buffer.teamID = 1 - scoringTeamID;
  buffer.setpiece_team = match->GetTeam(buffer.teamID);
  buffer.taker = 0;
  buffer.endPhase = false;
  buffer.active = true;
  rugbyKickoffRestartActive = true;
  rugbyLastHandTouchPlayer = 0;
  rugbyLastHandTouchType = e_TouchType_None;
  rugbyLastHandTouchTime_ms = 0;
  rugbyPendingKnockOnPlayer = 0;
  rugbyPendingKnockOnTime_ms = 0;
  if (!message.empty()) {
    match->SpamMessage(message);
  }
  if (!animations) {
    buffer.prepareTime = match->GetActualTime_ms();
    buffer.startTime = match->GetActualTime_ms();
    randomize(GetScenarioConfig().game_engine_random_seed);
    PrepareSetPiece(buffer.desiredSetPiece);
    match->StartPlay();
    match->StartSetPiece();
  }
}

void Referee::StartRugbyScrum(int feedTeamID, const Vector3 &restartPos,
                              const std::string &message) {
  DO_VALIDATION;
  match->StopPlay();
  buffer.desiredSetPiece = e_GameMode_GoalKick;
  buffer.stopTime = match->GetActualTime_ms();
  buffer.prepareTime = match->GetActualTime_ms() + 800;
  if (!animations) {
    match->BumpActualTime_ms(750);
  }
  buffer.startTime = buffer.prepareTime + 900;
  buffer.restartPos = restartPos;
  buffer.restartPos.coords[0] = clamp(buffer.restartPos.coords[0], -pitchHalfW + 2.0f,
                                      pitchHalfW - 2.0f);
  buffer.restartPos.coords[1] = clamp(buffer.restartPos.coords[1], -pitchHalfH + 5.0f,
                                      pitchHalfH - 5.0f);
  buffer.restartPos.coords[2] = 0;
  buffer.teamID = feedTeamID;
  buffer.setpiece_team = match->GetTeam(feedTeamID);
  buffer.taker = 0;
  buffer.endPhase = false;
  buffer.active = true;
  rugbyLastHandTouchPlayer = 0;
  rugbyLastHandTouchType = e_TouchType_None;
  rugbyLastHandTouchTime_ms = 0;
  rugbyPendingKnockOnPlayer = 0;
  rugbyPendingKnockOnTime_ms = 0;
  if (!message.empty()) {
    match->SpamMessage(message);
  }
}

bool Referee::CheckRugbyBreakdownOffside() {
  DO_VALIDATION;
  if (!match->IsRugbyScenario() || !match->IsInPlay() || match->IsInSetPiece() ||
      !match->IsRugbyBreakdownActive()) {
    return false;
  }
  Player *recycleReceiver = match->GetRugbyRecycleReceiver();
  if (recycleReceiver == nullptr) {
    return false;
  }
  if (match->GetActualTime_ms() < match->GetRugbyRecycleStartTime_ms() + 200) {
    return false;
  }

  for (int teamIndex = 0; teamIndex < 2; ++teamIndex) {
    std::vector<Player *> players;
    Team *team = match->GetTeam(teamIndex);
    team->GetActivePlayers(players);
    for (Player *player : players) {
      if (player == recycleReceiver) {
        continue;
      }
      if (!match->IsRugbyPlayerPenaltyEligibleAtBreakdown(player)) {
        continue;
      }

      match->SpamMessage("ruck offside! penalty!");
      const int kickingTeamID = 1 - team->GetID();
      Team *kickingTeam = match->GetTeam(kickingTeamID);
      Vector3 mark = match->GetBall()->Predict(0).Get2D();
      mark.coords[2] = 0;
      const bool success =
          match->ResolveRugbyKickAtGoal(kickingTeam, mark, 3, "Penalty kick");
      if (success) {
        StartRugbyKickoffRestart(kickingTeamID, "");
      } else {
        // Miss: defending team gets a 22m drop-out style restart by
        // feeding a scrum on the mark.
        StartRugbyScrum(kickingTeamID, mark, "penalty missed — play on");
      }
      return true;
    }
  }
  return false;
}

bool Referee::ResolveRugbyKickoff() {
  DO_VALIDATION;
  if (!rugbyKickoffRestartActive || !match->IsRugbyScenario() ||
      !match->IsInSetPiece() ||
      buffer.desiredSetPiece != e_GameMode_KickOff ||
      match->GetActualTime_ms() < buffer.startTime + 300) {
    return false;
  }

  Team *kickingTeam = match->GetTeam(buffer.teamID);
  if (kickingTeam == nullptr) {
    return false;
  }

  Player *taker = buffer.taker ? buffer.taker : kickingTeam->GetController()->GetPieceTaker();
  if (taker == nullptr) {
    return false;
  }

  std::vector<Player *> candidates;
  kickingTeam->GetActivePlayers(candidates);
  Player *receiver = nullptr;
  float bestDistance = 1000000.0f;
  for (Player *player : candidates) {
    if (player == nullptr || !player->IsActive()) continue;
    if (player == taker) continue;
    const float distance =
        (player->GetPosition() - buffer.restartPos).GetLength();
    if (distance < bestDistance) {
      bestDistance = distance;
      receiver = player;
    }
  }
  if (receiver == nullptr) {
    receiver = taker;
  }

  match->GetBall()->SetPosition(receiver->GetPosition() + Vector3(0, 0, 0.13f));
  match->GetBall()->SetMomentum(Vector3(0));
  match->SetBallRetainer(receiver);
  match->SetLastTouchTeamID(kickingTeam->GetID(),
                            e_TouchType_Intentional_Kicked);
  rugbyKickoffRestartActive = false;
  buffer.active = false;
  match->StopSetPiece();
  match->GetTeam(0)->GetController()->PrepareSetPiece(
      e_GameMode_Normal, match->GetTeam(1), -1, -1);
  match->GetTeam(1)->GetController()->PrepareSetPiece(
      e_GameMode_Normal, match->GetTeam(0), -1, -1);
  afterSetPieceRelaxTime_ms = 400;
  foul.foulPlayer = 0;
  foul.foulType = 0;
  return true;
}

bool Referee::ResolveRugbyLineout() {
  DO_VALIDATION;
  if (!match->IsRugbyScenario() || !match->IsInSetPiece() ||
      buffer.desiredSetPiece != e_GameMode_ThrowIn ||
      match->GetActualTime_ms() < buffer.startTime + 300) {
    return false;
  }

  // Phase 2: catch — finalise once the ball-in-flight window has passed.
  if (rugbyLineoutBallLaunched) {
    if (match->GetActualTime_ms() < rugbyLineoutCatchTime_ms) {
      return false;
    }
    Player *receiver = rugbyPendingLineoutReceiver;
    Team *winningTeam = rugbyPendingLineoutTeam;
    if (receiver == nullptr || winningTeam == nullptr ||
        !receiver->IsActive()) {
      rugbyLineoutBallLaunched = false;
      rugbyPendingLineoutReceiver = 0;
      rugbyPendingLineoutTeam = 0;
      return false;
    }
    match->SetBallRetainer(receiver);
    match->GetBall()->SetPosition(receiver->GetPosition() +
                                  Vector3(0, 0, 0.16f));
    match->GetBall()->SetMomentum(Vector3(0));
    match->SetLastTouchTeamID(winningTeam->GetID(),
                              e_TouchType_Intentional_Nonkicked);
    match->SetRugbyLastLineoutOutcome(winningTeam->GetID());
    rugbyLineoutBallLaunched = false;
    rugbyPendingLineoutReceiver = 0;
    rugbyPendingLineoutTeam = 0;
    buffer.active = false;
    match->StopSetPiece();
    match->GetTeam(0)->GetController()->PrepareSetPiece(
        e_GameMode_Normal, match->GetTeam(1), -1, -1);
    match->GetTeam(1)->GetController()->PrepareSetPiece(
        e_GameMode_Normal, match->GetTeam(0), -1, -1);
    afterSetPieceRelaxTime_ms = 400;
    foul.foulPlayer = 0;
    foul.foulType = 0;
    return true;
  }

  Team *takerTeam = match->GetTeam(buffer.teamID);
  if (takerTeam == nullptr) {
    return false;
  }

  Team *defendingTeam = takerTeam->Opponent();
  std::vector<Player *> takerLane =
      GetRugbyLineoutLane(takerTeam, buffer.restartPos, buffer.taker, 7);
  std::vector<Player *> defendingLane =
      GetRugbyLineoutLane(defendingTeam, buffer.restartPos, nullptr, 7);
  if (takerLane.empty() || defendingLane.empty()) {
    return false;
  }

  const float throwTargetX =
      clamp(buffer.restartPos.coords[0] + boostrandom(-1.2f, 2.4f),
            -pitchHalfW + 4.0f, pitchHalfW - 4.0f);
  Player *takerJumper = ChooseRugbyLineoutJumper(takerLane, throwTargetX);
  Player *defendingJumper =
      ChooseRugbyLineoutJumper(defendingLane, throwTargetX);
  if (takerJumper == nullptr || defendingJumper == nullptr) {
    return false;
  }

  float throwQuality = 0.55f;
  if (buffer.taker != nullptr) {
    throwQuality =
        buffer.taker->GetStat(technical_shortpass) * 0.55f +
        buffer.taker->GetStat(technical_highpass) * 0.25f +
        buffer.taker->GetStat(mental_vision) * 0.20f;
  }
  const float takerJumpScore =
      takerJumper->GetStat(technical_header) * 0.50f +
      takerJumper->GetStat(physical_reaction) * 0.30f +
      takerJumper->GetStat(physical_balance) * 0.20f;
  const float defendingJumpScore =
      defendingJumper->GetStat(technical_header) * 0.50f +
      defendingJumper->GetStat(physical_reaction) * 0.30f +
      defendingJumper->GetStat(physical_balance) * 0.20f;
  const float supportDelta =
      GetRugbyLineoutSupportScore(takerLane, takerJumper) -
      GetRugbyLineoutSupportScore(defendingLane, defendingJumper);
  const float jumpDelta = takerJumpScore - defendingJumpScore;
  const float takerWinChance =
      clamp(0.58f + (throwQuality - 0.5f) * 0.25f + jumpDelta * 0.28f +
                supportDelta * 0.12f + boostrandom(-0.10f, 0.10f),
            0.18f, 0.88f);
  const bool takerWins = boostrandom(0.0f, 1.0f) <= takerWinChance;

  Team *winningTeam = takerWins ? takerTeam : defendingTeam;
  Player *winningJumper = takerWins ? takerJumper : defendingJumper;
  Player *receiver = ChooseRugbyLineoutReceiver(winningTeam, buffer.restartPos,
                                                winningJumper, buffer.taker);
  if (receiver == nullptr) {
    receiver = winningTeam->GetDesignatedTeamPossessionPlayer();
  }
  if (receiver == nullptr) {
    receiver = winningJumper;
  }
  if (receiver == nullptr) {
    return false;
  }

  // Phase 1: launch — toss the ball from the thrower's mark toward the
  // winning jumper on a parabolic arc. Catch is finalised on the next pass
  // through this function after the flight window expires.
  const float touchSide = signSide(buffer.restartPos.coords[1]);
  const Vector3 throwStart(buffer.restartPos.coords[0],
                           buffer.restartPos.coords[1], 1.6f);
  const Vector3 contestPoint(winningJumper->GetPosition().coords[0],
                             buffer.restartPos.coords[1] - touchSide * 0.5f,
                             3.2f);
  const float flightTime = 0.55f;  // seconds from mark to apex
  Vector3 velocity = (contestPoint - throwStart) / flightTime;
  // Add an upward pre-apex bias so the ball arcs visibly rather than flying
  // flat across the line.
  velocity.coords[2] += 2.4f;
  match->GetBall()->SetPosition(throwStart);
  match->GetBall()->SetMomentum(velocity);
  rugbyLineoutBallLaunched = true;
  rugbyPendingLineoutReceiver = receiver;
  rugbyPendingLineoutTeam = winningTeam;
  rugbyLineoutCatchTime_ms =
      match->GetActualTime_ms() +
      static_cast<unsigned long>(flightTime * 1000.0f);
  return false;
}

bool Referee::ResolveRugbyScrum() {
  DO_VALIDATION;
  const bool scrumMode =
      buffer.desiredSetPiece == e_GameMode_GoalKick ||
      buffer.desiredSetPiece == e_GameMode_Corner;
  if (!match->IsRugbyScenario() || !match->IsInSetPiece() || !scrumMode ||
      match->GetActualTime_ms() < buffer.startTime + 500) {
    return false;
  }

  Team *feedTeam = match->GetTeam(buffer.teamID);
  if (feedTeam == nullptr) {
    return false;
  }
  Team *defendingTeam = feedTeam->Opponent();
  std::vector<Player *> feedPack =
      GetRugbyScrumPack(feedTeam, buffer.restartPos, 8);
  std::vector<Player *> defendingPack =
      GetRugbyScrumPack(defendingTeam, buffer.restartPos, 8);
  if (feedPack.size() < 6 || defendingPack.size() < 6) {
    return false;
  }

  const float feedScore = GetRugbyScrumPackScore(feedPack);
  const float defendScore = GetRugbyScrumPackScore(defendingPack);
  const float feedWinChance =
      clamp(0.57f + (feedScore - defendScore) * 0.35f +
                boostrandom(-0.10f, 0.10f),
            0.20f, 0.85f);
  const bool feedWins = boostrandom(0.0f, 1.0f) <= feedWinChance;

  Team *winningTeam = feedWins ? feedTeam : defendingTeam;
  const std::vector<Player *> &winningPack = feedWins ? feedPack : defendingPack;
  Player *receiver = ChooseRugbyScrumReceiver(winningTeam, buffer.restartPos,
                                              winningPack);
  if (receiver == nullptr) {
    receiver = winningTeam->GetDesignatedTeamPossessionPlayer();
  }
  if (receiver == nullptr && !winningPack.empty()) {
    receiver = winningPack.front();
  }
  if (receiver == nullptr) {
    return false;
  }

  const float feedSide = feedTeam->GetDynamicSide();
  const Vector3 tunnelExit =
      buffer.restartPos +
      Vector3(feedSide * (feedWins ? 1.2f : -0.6f), 0.0f, 0.12f);
  match->GetBall()->SetPosition(tunnelExit);
  match->SetBallRetainer(receiver);
  match->GetBall()->SetPosition(receiver->GetPosition() + Vector3(0, 0, 0.13f));
  match->GetBall()->SetMomentum(Vector3(0));
  match->SetLastTouchTeamID(winningTeam->GetID(),
                            e_TouchType_Intentional_Nonkicked);
  match->SetRugbyLastScrumOutcome(winningTeam->GetID());

  // Break up the pack. PrepareSetPiece(Normal,...) is a no-op in the
  // engine, so without an explicit dispersal here the eight bound
  // forwards stay glued in the scrum shape and the AI only slowly drifts
  // them back. Snap every player (except the receiver) to their formation
  // position offset by the scrum mark so the backline forms a natural
  // post-scrum attacking shape.
  for (int teamIndex : {0, 1}) {
    Team *team = match->GetTeam(teamIndex);
    if (team == nullptr) continue;
    std::vector<Player *> all;
    team->GetActivePlayers(all);
    const signed int side = team->GetDynamicSide();
    for (Player *p : all) {
      if (p == nullptr || !p->IsActive()) continue;
      if (p == receiver) continue;
      const Vector3 formation = p->GetFormationEntry().position;
      Vector3 basePos(
          buffer.restartPos.coords[0] +
              formation.coords[0] * side * 18.0f,
          buffer.restartPos.coords[1] +
              formation.coords[1] * 18.0f,
          0);
      // Keep both teams on their proper side of the scrum mark.
      if (team == winningTeam) {
        basePos.coords[0] -= side * 2.5f;
      } else {
        basePos.coords[0] += side * 1.5f;
      }
      p->ResetPosition(basePos, buffer.restartPos);
    }
  }

  buffer.active = false;
  match->StopSetPiece();
  match->GetTeam(0)->GetController()->PrepareSetPiece(
      e_GameMode_Normal, match->GetTeam(1), -1, -1);
  match->GetTeam(1)->GetController()->PrepareSetPiece(
      e_GameMode_Normal, match->GetTeam(0), -1, -1);
  afterSetPieceRelaxTime_ms = 400;
  foul.foulPlayer = 0;
  foul.foulType = 0;
  return true;
}

void Referee::Process() {
  DO_VALIDATION;
  if (match->IsInPlay() && !match->IsInSetPiece()) {
    DO_VALIDATION;

    if (match->IsRugbyScenario() && buffer.active == false) {
      Player *touchPlayer = match->GetLastTouchPlayer();
      if (GetScenarioConfig().rugby_force_forward_pass_scrum &&
          !rugbyForcedForwardPassConsumed &&
          touchPlayer != nullptr && match->GetActualTime_ms() >= 2100) {
        rugbyForcedForwardPassConsumed = true;
        StartRugbyScrum(1 - touchPlayer->GetTeam()->GetID(),
                        touchPlayer->GetPosition(), "forward pass!");
        return;
      }
      if (GetScenarioConfig().rugby_force_knock_on_scrum &&
          !rugbyForcedKnockOnConsumed &&
          touchPlayer != nullptr && match->GetActualTime_ms() >= 2100) {
        rugbyForcedKnockOnConsumed = true;
        StartRugbyScrum(1 - touchPlayer->GetTeam()->GetID(),
                        touchPlayer->GetPosition(), "knock on!");
        return;
      }
    }

    if (CheckRugbyBreakdownOffside()) {
      return;
    }

    Vector3 ballPos = match->GetBall()->Predict(0);
    // Single step maps to 1800 units.
    if (match->GetMatchTime_ms() >= 1800 * GetScenarioConfig().second_half &&
        match->GetMatchPhase() == e_MatchPhase_1stHalf) {
      match->StopPlay();
      buffer.desiredSetPiece = e_GameMode_KickOff;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = buffer.stopTime + 100;
      buffer.startTime = buffer.prepareTime + 200;
      buffer.restartPos = GetScenarioConfig().ball_position;
      buffer.active = true;
      buffer.endPhase = true;
      buffer.teamID = GetScenarioConfig().LeftTeamOwnsBall() ? 1 : 0;
      buffer.setpiece_team = match->GetTeam(buffer.teamID);
      buffer.taker = 0;
      foul.foulPlayer = 0;
      foul.foulType = 0;
      foul.advantage = false;
      foul.foulTime = 0;
      foul.hasBeenProcessed = true;
      match->SetMatchPhase(e_MatchPhase_2ndHalf);
    }

    // We process corner setup in not mirrored setup.
    if (GetScenarioConfig().reverse_team_processing) {
      ballPos.Mirror();
    }

    int rugbyScoringTeamID = -1;
    const bool rugbyPendingScore =
        match->IsRugbyScenario() && match->CheckForRugbyScore(rugbyScoringTeamID);

    // goal kick / corner

    if (fabs(ballPos.coords[0]) > pitchHalfW + lineHalfW + 0.11 ||
        match->IsGoalScored() || rugbyPendingScore) {
      DO_VALIDATION;

      foul.advantage = false;
      bool isFoul = false;
      if (!match->IsGoalScored() && !rugbyPendingScore) {
        isFoul = CheckFoul();
      } else {
        foul.foulType = 0;
      }
      // In rugby, wait for the conversion kick animation to finish before
      // scheduling the kickoff restart.
      if (match->IsRugbyScenario() && match->IsRugbyConversionPending()) {
        isFoul = true;  // abuse the flag to skip kickoff setup this tick
      }
      if (isFoul == false) {
        DO_VALIDATION;

        match->StopPlay();

        // corner, goal kick or kick off?
        Team *lastTouchTeam = match->GetLastTouchTeam();
        if (lastTouchTeam == 0) lastTouchTeam = match->GetTeam(GetScenarioConfig().reverse_team_processing ? 1 : 0);
        signed int lastSide = lastTouchTeam->GetStaticSide();

        if (match->IsGoalScored() || rugbyPendingScore) {
          DO_VALIDATION;
          buffer.desiredSetPiece = e_GameMode_KickOff;
          buffer.stopTime = match->GetActualTime_ms();
          // Number of ms for replay.
          buffer.prepareTime = match->GetActualTime_ms() + 500;
          if (!animations) {
            match->BumpActualTime_ms(400);
          }
          // Number of ms for kickoff.
          buffer.startTime = buffer.prepareTime + 500;
          buffer.restartPos = Vector3(0, 0, 0);
          if (match->IsRugbyScenario()) {
            Team *scoringTeam =
                rugbyPendingScore ? match->GetTeam(rugbyScoringTeamID)
                                  : match->GetLastGoalTeam();
            buffer.setpiece_team = scoringTeam->Opponent();
            buffer.teamID = buffer.setpiece_team->GetID();
          } else {
            buffer.teamID = match->FirstTeam();
            buffer.setpiece_team = match->GetLastGoalTeam()->Opponent();
          }
        } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
                   (ballPos.coords[0] < 0 && lastSide < 0)) {
          DO_VALIDATION;
          buffer.desiredSetPiece = e_GameMode_Corner;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          if (!animations) {
            match->BumpActualTime_ms(1900);
          }
          buffer.startTime = buffer.prepareTime + 2000;
          float y = ballPos.coords[1];
          if (y > 0) y = pitchHalfH; else
                     y = -pitchHalfH;
          buffer.restartPos = Vector3(pitchHalfW * lastSide, y, 0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
        } else {
          buffer.desiredSetPiece = e_GameMode_GoalKick;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          if (!animations) {
            match->BumpActualTime_ms(1900);
          }
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = Vector3(pitchHalfW * 0.92 * -lastSide, 0, 0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
        }

        buffer.active = true;
      }
    }

    // over sideline

    if (afterSetPieceRelaxTime_ms == 0) {
      DO_VALIDATION;
      if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW + 0.11) {
        DO_VALIDATION;
        foul.advantage = false;
        if (!CheckFoul()) {
          DO_VALIDATION;
          match->StopPlay();
          Team *lastTouchTeam = match->GetLastTouchTeam();
          if (lastTouchTeam == 0) lastTouchTeam = match->GetTeam(0);
          buffer.teamID = 1 - lastTouchTeam->GetID();
          buffer.desiredSetPiece = e_GameMode_ThrowIn;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          if (!animations) {
            match->BumpActualTime_ms(1900);
          }
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos.coords[0] = clamp(ballPos.coords[0], -pitchHalfW + 0.6f, pitchHalfW - 0.6f);
          if (ballPos.coords[1] >  0) buffer.restartPos.coords[1] = pitchHalfH;
          if (ballPos.coords[1] <= 0) buffer.restartPos.coords[1] = -pitchHalfH;
          buffer.restartPos.coords[2] = 0;
          buffer.active = true;
        }
      }
    }

    CheckFoul();

  } else {  // not in play, maybe something needs to happen?

    if (!match->IsInPlay() && !match->IsInSetPiece() && buffer.active == true) {
      DO_VALIDATION;

      if (buffer.prepareTime == match->GetActualTime_ms()) {
        DO_VALIDATION;
        if (buffer.endPhase == true) {
          if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
            match->SetMatchPhase(e_MatchPhase_1stHalf);
          }
          buffer.endPhase = false;
        }

        randomize(GetScenarioConfig().game_engine_random_seed);
        PrepareSetPiece(buffer.desiredSetPiece);
        DO_VALIDATION;
      }

      if (buffer.startTime == match->GetActualTime_ms()) {
        DO_VALIDATION;
        // blow whistle and wait for set piece taker to touch the ball
        match->StartPlay();
        match->StartSetPiece();
      }
    }
  }

  if (match->IsInSetPiece()) {
    DO_VALIDATION;
    if (ResolveRugbyKickoff()) {
      return;
    }
    if (ResolveRugbyScrum()) {
      return;
    }
    if (ResolveRugbyLineout()) {
      return;
    }
    const bool rugbyLineoutPending =
        match->IsRugbyScenario() &&
        buffer.desiredSetPiece == e_GameMode_ThrowIn;
    const bool rugbyKickoffPending =
        rugbyKickoffRestartActive && match->IsRugbyScenario() &&
        buffer.desiredSetPiece == e_GameMode_KickOff;
    // check if set piece has been taken
    if (!rugbyLineoutPending &&
        ((!rugbyKickoffPending &&
          buffer.desiredSetPiece == e_GameMode_KickOff) ||
         (buffer.taker->TouchAnim() && !buffer.taker->TouchPending()))) {
      DO_VALIDATION;
      buffer.active = false;
      rugbyKickoffRestartActive = false;
      match->StopSetPiece();
      match->GetTeam(0)->GetController()->PrepareSetPiece(e_GameMode_Normal, match->GetTeam(1), -1, -1);
      match->GetTeam(1)->GetController()->PrepareSetPiece(e_GameMode_Normal, match->GetTeam(0), -1, -1);
      afterSetPieceRelaxTime_ms = 400;
      foul.foulPlayer = 0;
      foul.foulType = 0;

      if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
        DO_VALIDATION;
        match->SetMatchPhase(e_MatchPhase_1stHalf);
      }
    }
  }

  if (afterSetPieceRelaxTime_ms > 0) afterSetPieceRelaxTime_ms -= 10;
}

void Referee::PrepareSetPiece(e_GameMode setPiece) {
  DO_VALIDATION;
  // position players for set piece situation
  if (setPiece == e_GameMode_FreeKick) {
    buffer.restartPos.coords[0] = clamp(buffer.restartPos.coords[0],
                                        -0.95 * pitchHalfW, 0.95 * pitchHalfW);
    buffer.restartPos.coords[1] = clamp(buffer.restartPos.coords[1],
                                        -0.95 * pitchHalfH, 0.95 * pitchHalfH);
  }
  match->ResetSituation(GetScenarioConfig().reverse_team_processing
                            ? -buffer.restartPos
                            : buffer.restartPos);

  match->GetTeam(match->FirstTeam())
      ->GetController()
      ->PrepareSetPiece(setPiece, match->GetTeam(match->SecondTeam()),
                        buffer.setpiece_team->GetID(), buffer.teamID);
  match->GetTeam(match->SecondTeam())
      ->GetController()
      ->PrepareSetPiece(setPiece, match->GetTeam(match->FirstTeam()),
                        buffer.setpiece_team->GetID(), buffer.teamID);

  buffer.taker = match->GetTeam(buffer.teamID)->GetController()->GetPieceTaker();
  // Reset offside state.
  offsidePlayers.clear();
  rugbyLastHandTouchPlayer = 0;
  rugbyLastHandTouchType = e_TouchType_None;
  rugbyLastHandTouchTime_ms = 0;
  rugbyPendingKnockOnPlayer = 0;
  rugbyPendingKnockOnTime_ms = 0;
  rugbyLineoutBallLaunched = false;
  rugbyPendingLineoutReceiver = 0;
  rugbyPendingLineoutTeam = 0;
  rugbyLineoutCatchTime_ms = 0;
}

void Referee::AlterSetPiecePrepareTime(unsigned long newTime_ms) {
  DO_VALIDATION;
  if (buffer.active) {
    DO_VALIDATION;
    buffer.prepareTime = newTime_ms;
    buffer.startTime = buffer.prepareTime + 2000;
  }
}

void Referee::BallTouched() {
  DO_VALIDATION;

  if (match->IsRugbyScenario()) {
    Player *touchPlayer = match->GetLastTouchPlayer();
    Team *touchTeam = match->GetLastTouchTeam();
    if (match->IsInPlay() && !match->IsInSetPiece() && buffer.active == false &&
        touchPlayer != nullptr && touchTeam != nullptr) {
      const e_TouchType touchType = touchPlayer->GetLastTouchType();
      const Vector3 touchPos = touchPlayer->GetPosition();
      const float forwardDistance =
          (touchPos.coords[0] - rugbyLastHandTouchPos.coords[0]) *
          touchTeam->GetDynamicSide();

      if (GetScenarioConfig().rugby_force_forward_pass_scrum &&
          !rugbyForcedForwardPassConsumed &&
          rugbyLastHandTouchPlayer != nullptr &&
          rugbyLastHandTouchPlayer->GetTeam() == touchTeam &&
          rugbyLastHandTouchPlayer != touchPlayer) {
        rugbyForcedForwardPassConsumed = true;
        StartRugbyScrum(1 - touchTeam->GetID(),
                        (rugbyLastHandTouchPos + touchPos) * 0.5f,
                        "forward pass!");
        return;
      }

      if (GetScenarioConfig().rugby_force_knock_on_scrum &&
          !rugbyForcedKnockOnConsumed &&
          rugbyPendingKnockOnPlayer != nullptr &&
          touchPlayer->GetTeam() == rugbyPendingKnockOnPlayer->GetTeam() &&
          touchPlayer != rugbyPendingKnockOnPlayer) {
        rugbyForcedKnockOnConsumed = true;
        StartRugbyScrum(1 - touchTeam->GetID(),
                        (rugbyPendingKnockOnPos + touchPos) * 0.5f,
                        "knock on!");
        return;
      }

      if (rugbyPendingKnockOnPlayer != nullptr &&
          match->GetActualTime_ms() <= rugbyPendingKnockOnTime_ms + 900) {
        const bool sameTeamRecovery =
            touchPlayer->GetTeam() == rugbyPendingKnockOnPlayer->GetTeam();
        const bool forwardRecovery =
            (touchPos.coords[0] - rugbyPendingKnockOnPos.coords[0]) *
                rugbyPendingKnockOnPlayer->GetTeam()->GetDynamicSide() >
            0.15f;
        if (sameTeamRecovery && touchPlayer != rugbyPendingKnockOnPlayer &&
            forwardRecovery) {
          StartRugbyScrum(1 - touchTeam->GetID(),
                          (rugbyPendingKnockOnPos + touchPos) * 0.5f,
                          "knock on!");
          return;
        }
      }

      // Forward pass: require the previous hand-toucher to have held the
      // ball long enough for it to be a real pass attempt (≥ 250 ms),
      // and require a distance of 1.2 m so transient auto-pickup hand-offs
      // between nearby teammates don't trip the whistle.
      if (touchType == e_TouchType_Intentional_Nonkicked &&
          rugbyLastHandTouchPlayer != nullptr &&
          rugbyLastHandTouchPlayer->GetTeam() == touchTeam &&
          rugbyLastHandTouchPlayer != touchPlayer &&
          match->GetActualTime_ms() <= rugbyLastHandTouchTime_ms + 900 &&
          match->GetActualTime_ms() >= rugbyLastHandTouchTime_ms + 250 &&
          forwardDistance > 1.2f) {
        StartRugbyScrum(1 - touchTeam->GetID(),
                        (rugbyLastHandTouchPos + touchPos) * 0.5f,
                        "forward pass!");
        return;
      }

      if (touchType == e_TouchType_Accidental) {
        rugbyPendingKnockOnPlayer = touchPlayer;
        rugbyPendingKnockOnPos = touchPos;
        rugbyPendingKnockOnTime_ms = match->GetActualTime_ms();
      } else if (GetScenarioConfig().rugby_force_knock_on_scrum &&
                 touchType == e_TouchType_Intentional_Nonkicked &&
                 rugbyPendingKnockOnPlayer == nullptr) {
        rugbyPendingKnockOnPlayer = touchPlayer;
        rugbyPendingKnockOnPos = touchPos;
        rugbyPendingKnockOnTime_ms = match->GetActualTime_ms();
      } else if (touchType == e_TouchType_Intentional_Kicked) {
        rugbyPendingKnockOnPlayer = 0;
        rugbyPendingKnockOnTime_ms = 0;
      }

      if (touchType == e_TouchType_Intentional_Nonkicked ||
          touchType == e_TouchType_Accidental) {
        rugbyLastHandTouchPlayer = touchPlayer;
        rugbyLastHandTouchPos = touchPos;
        rugbyLastHandTouchTime_ms = match->GetActualTime_ms();
        rugbyLastHandTouchType = touchType;
      } else if (touchType == e_TouchType_Intentional_Kicked) {
        rugbyLastHandTouchPlayer = 0;
        rugbyLastHandTouchType = e_TouchType_None;
        rugbyLastHandTouchTime_ms = 0;
      }
    } else if (!match->IsInPlay() || match->IsInSetPiece()) {
      rugbyPendingKnockOnPlayer = 0;
      rugbyPendingKnockOnTime_ms = 0;
    }
  }

  // Football offside has no meaning in rugby — the engine handles rugby
  // offside separately at the breakdown / from the last set-piece mark.
  if (match->IsRugbyScenario()) {
    offsidePlayers.clear();
    return;
  }
  if (!GetScenarioConfig().offsides) {
    DO_VALIDATION;
    return;
  }
  // check for offside player receiving the ball

  int lastTouchTeamID = match->GetLastTouchTeamID();
  if (lastTouchTeamID == -1) return; // shouldn't happen really ;)
  if (match->IsInPlay() && !match->IsInSetPiece() && buffer.active == false &&
      match->GetTeam(1 - lastTouchTeamID)->GetActivePlayersCount() > 1) {
    DO_VALIDATION;  // disable if only 1 player: that's debug mode with only
                    // keeper
    auto ballOwner = match->GetLastTouchPlayer();
    for (auto p : offsidePlayers) {
      DO_VALIDATION;
      if (p == ballOwner) {
        DO_VALIDATION;
        foul.advantage = false;
        if (!CheckFoul()) {
          DO_VALIDATION;
          // uooooga uooooga offside!
          match->StopPlay();
          buffer.desiredSetPiece = e_GameMode_FreeKick;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          if (!animations) {
            match->BumpActualTime_ms(1900);
          }
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = ballOwner->GetPitchPosition();
          buffer.teamID = 1 - lastTouchTeamID;
          buffer.active = true;
          match->SpamMessage("offside!");
        }
      }
    }
  }

  offsidePlayers.clear();

  if (match->IsInPlay() &&
      (buffer.active == false ||
       (buffer.active == true && buffer.desiredSetPiece != e_GameMode_ThrowIn &&
        buffer.desiredSetPiece != e_GameMode_Corner))) {
    DO_VALIDATION;
    // check for offside players at moment of touch
    MentalImage mentalImage(match);
    float offside = AI_GetOffsideLine(match, &mentalImage, 1 - lastTouchTeamID);
    std::vector<Player*> players;
    Team *team = match->GetTeam(lastTouchTeamID);
    team->GetActivePlayers(players);
    for (auto player : players) {
      DO_VALIDATION;
      if (player != team->GetLastTouchPlayer()) {
        DO_VALIDATION;
        if (player->GetPosition().coords[0] * team->GetDynamicSide() <
            offside * team->GetDynamicSide()) {
          DO_VALIDATION;
          offsidePlayers.push_back(player);
        }
      }
    }
  }
}

void Referee::TripNotice(Player *tripee, Player *tripper, int tackleType) {
  DO_VALIDATION;

  if (buffer.active) return;
  if (match->IsRugbyScenario() && tripee != nullptr && tripper != nullptr &&
      tripee->GetTeam()->GetID() != tripper->GetTeam()->GetID() &&
      tripee == match->GetBallRetainer()) {
    match->RegisterRugbyTackle(tripee, tripper);
    return;
  }

  if (tackleType == 2) {
    DO_VALIDATION;  // standing tackle
    if (tripee->GetTeam()->GetFadingTeamPossessionAmount() > 1.1 &&
        (tripper->GetCurrentFunctionType() == e_FunctionType_Interfere ||
         tripper->GetCurrentFunctionType() == e_FunctionType_Sliding) &&
        (tripee->GetPosition() - match->GetBall()->Predict(0).Get2D())
                .GetLength() < 2.0 &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID()) {
      DO_VALIDATION;
      // uooooga uooooga foul!
      foul.foulType = 1;
      foul.advantage = true;
      foul.foulPlayer = tripper;
      foul.foulVictim = tripee;
      foul.foulTime = match->GetActualTime_ms();
      foul.foulPosition = tripee->GetPitchPosition();
      foul.hasBeenProcessed = false;
    }

  } else if (tackleType == 3 &&
             (tripper != foul.foulPlayer || foul.foulType == 0)) {
    DO_VALIDATION;  // sliding tackle

    if (match->GetActualTime_ms() - tripper->GetLastTouchTime_ms() > 600 &&
        tripper->GetCurrentFunctionType() == e_FunctionType_Sliding &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID() &&
        (match->GetBall()->Predict(0) - tripee->GetPosition()).GetLength() <
            8.0) {
      DO_VALIDATION;
      float severity = 1.0;
      if (tripper->TouchAnim()) {
        DO_VALIDATION;
        severity = std::pow(clamp(fabs(tripper->GetTouchFrame() -
                                       tripper->GetCurrentFrame()) /
                                      tripper->GetTouchFrame(),
                                  0.0, 1.0),
                            0.7) *
                   0.5;
        severity += NormalizedClamp((match->GetBall()->Predict(0) - tripper->GetTouchPos()).GetLength(), 0.0, 2.0) * 0.5;
      }
      // from behind?
      severity += (tripee->GetPosition() - tripper->GetPosition()).GetNormalized(0).GetDotProduct(tripee->GetDirectionVec()) * 0.5 + 0.5;

      if (severity > 1.0) {
        DO_VALIDATION;
        // uooooga uooooga foul!
        //printf("sliding! %lu ms ago\n", match->GetActualTime_ms() - tripper->GetLastTouchTime_ms());
        foul.foulType = 1;
        foul.advantage = false;
        foul.foulPlayer = tripper;
        foul.foulVictim = tripee;
        foul.foulTime = match->GetActualTime_ms();
        foul.foulPosition = tripee->GetPitchPosition();
        foul.hasBeenProcessed = false;
        if (severity > 1.4) foul.foulType = 2;
        if (severity > 2.0) {
          DO_VALIDATION;
          foul.foulType = 3;
        }
      }
    }
  }
}

void Referee::ProcessState(EnvState *state) {
  DO_VALIDATION;
  buffer.ProcessState(state);
  state->process(afterSetPieceRelaxTime_ms);
  int size = offsidePlayers.size();
  state->process(size);
  offsidePlayers.resize(size);
  for (auto &i : offsidePlayers) {
    DO_VALIDATION;
    state->process(i);
  }
  foul.ProcessState(state);
}

bool Referee::CheckFoul() {
  DO_VALIDATION;

  bool penalty = false;
  if (foul.foulType != 0) {
    DO_VALIDATION;
    if (fabs(foul.foulPosition.coords[1]) < 20.15 - lineHalfW &&
        foul.foulPosition.coords[0] *
                -foul.foulVictim->GetTeam()->GetStaticSide() >
            pitchHalfW - 16.5 + lineHalfW)
      penalty = true;
  }

  if (foul.advantage) {
    DO_VALIDATION;
    if (penalty) {
      DO_VALIDATION;
      foul.advantage = false;
    } else {
      if (match->GetActualTime_ms() - 600 > foul.foulTime) {
        DO_VALIDATION;
        if (match->GetActualTime_ms() - 3000 > foul.foulTime) {
          DO_VALIDATION;
          // cancel foul, advantage took long enough

          foul.foulPlayer = 0;
          foul.foulType = 0;
        } else {
          // calculate if there's advantage still
          if (foul.foulVictim->GetTeam()->GetFadingTeamPossessionAmount() <
              1.0) {
            DO_VALIDATION;
            foul.advantage = false;
          }
        }
      }
    }
  }

  if (foul.foulType != 0 && foul.advantage == false && !foul.hasBeenProcessed) {
    DO_VALIDATION;

    match->StopPlay();
    if (!penalty) {
      DO_VALIDATION;
      buffer.desiredSetPiece = e_GameMode_FreeKick;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = match->GetActualTime_ms() + 2000;
      if (!animations) {
        match->BumpActualTime_ms(1900);
      }
      if (foul.foulType >= 2) {
        buffer.prepareTime += 10000;
        if (!animations) {
          match->BumpActualTime_ms(10000);
        }

      }
      buffer.startTime = buffer.prepareTime + 2000;
      buffer.restartPos = foul.foulPosition;
    } else {
      buffer.desiredSetPiece = e_GameMode_Penalty;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = match->GetActualTime_ms() + 2000;
      if (!animations) {
        match->BumpActualTime_ms(1900);
      }
      if (foul.foulType >= 2) {
        buffer.prepareTime += 10000;
        if (!animations) {
          match->BumpActualTime_ms(10000);
        }
      }
      buffer.startTime = buffer.prepareTime + 2000;
      buffer.restartPos = Vector3(
          (pitchHalfW - 11.0) * foul.foulPlayer->GetTeam()->GetStaticSide(), 0,
          0);
    }
    buffer.teamID = foul.foulVictim->GetTeam()->GetID();
    buffer.active = true;
    std::string spamMessage = "foul!";
    if (foul.foulType == 2) {
      DO_VALIDATION;
      spamMessage.append(" yellow card");
      foul.foulPlayer->GiveYellowCard(match->GetActualTime_ms() + 6000); // need to find out proper moment
    }
    if (foul.foulType == 3) {
      DO_VALIDATION;
      spamMessage.append(" red card!!!");
      foul.foulPlayer->GiveRedCard(match->GetActualTime_ms() + 6000); // need to find out proper moment
    }
    match->SpamMessage(spamMessage);

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}
