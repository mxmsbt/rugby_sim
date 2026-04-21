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

#include "match.hpp"

#include <algorithm>
#include <cmath>

#include "../base/geometry/triangle.hpp"
#include "../base/log.hpp"
#include "../game_env.hpp"
#include "../main.hpp"
#include "../menu/pagefactory.hpp"
#include "../menu/startmatch/loadingmatch.hpp"
#include "../scene/objectfactory.hpp"
#include "../scene/objects/light.hpp"
#include "../utils/splitgeometry.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "file.h"
#include "player/playerofficial.hpp"
#include "proceduralpitch.hpp"

constexpr unsigned int replaySize_ms = 10000;
constexpr unsigned int camPosSize = 150;

boost::shared_ptr<AnimCollection> Match::GetAnimCollection() {
  DO_VALIDATION;
  return GetContext().anims;
}

bool Match::IsRugbyScenario() const {
  return GetScenarioConfig().left_team.size() == 15 &&
         GetScenarioConfig().right_team.size() == 15;
}

e_GameMode Match::GetGameMode() const {
  if (IsRugbyScenario()) {
    if (IsInSetPiece()) {
      e_GameMode desired = referee->GetBuffer().desiredSetPiece;
      switch (desired) {
        case e_GameMode_KickOff:
          return e_GameMode_RugbyKickoff;
        case e_GameMode_ThrowIn:
          return e_GameMode_Lineout;
        case e_GameMode_GoalKick:
        case e_GameMode_Corner:
          return e_GameMode_Scrum;
        default:
          return desired;
      }
    }
    if (goalScored) {
      return e_GameMode_Conversion;
    }
    if (rugbyBreakdownActive) {
      return e_GameMode_Ruck;
    }
  }
  return IsInSetPiece() ? referee->GetBuffer().desiredSetPiece
                        : e_GameMode_Normal;
}

bool Match::CheckForRugbyScore(int &scoringTeamID) const {
  scoringTeamID = -1;
  if (!IsRugbyScenario() || GetBallRetainer() == nullptr) {
    return false;
  }

  Player *carrier = GetBallRetainer();
  Vector3 ballPos = ball->Predict(0);
  if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW) {
    return false;
  }

  const int attackingSide = carrier->GetTeam()->GetDynamicSide();
  const bool crossedTryLine =
      (attackingSide > 0 && ballPos.coords[0] > pitchHalfW + lineHalfW) ||
      (attackingSide < 0 && ballPos.coords[0] < -pitchHalfW - lineHalfW);
  if (!crossedTryLine) {
    return false;
  }

  scoringTeamID = carrier->GetTeam()->GetID();
  return true;
}

Player *Match::FindRugbyKickAtGoalKicker(Team *team) const {
  DO_VALIDATION;
  if (team == nullptr) return nullptr;
  const std::vector<Player *> &players = team->GetAllPlayers();
  Player *best = nullptr;
  float bestScore = -1.0f;
  for (Player *p : players) {
    if (p == nullptr || !p->IsActive()) continue;
    if (p->GetFormationEntry().role == e_PlayerRole_GK) continue;
    const float score = p->GetStat(technical_shot) * 0.7f +
                        p->GetStat(technical_highpass) * 0.3f;
    if (score > bestScore) {
      bestScore = score;
      best = p;
    }
  }
  return best;
}

float Match::ComputeRugbyKickAtGoalProb(Player *kicker,
                                        const Vector3 &markPos) const {
  DO_VALIDATION;
  // Posts sit at centre-y=0, attacker's try line at x = ±pitchHalfW.
  // Distance from center-y raises the kicking angle difficulty.
  const float lateralRatio =
      std::min(std::fabs(markPos.coords[1]) / std::max(pitchHalfH, 0.001f),
               1.0f);
  // Distance from try line (behind the mark) — further away = harder.
  const float depthRatio =
      clamp(1.0f - std::fabs(markPos.coords[0]) / std::max(pitchHalfW, 0.001f),
            0.0f, 1.0f);
  float skill = 0.55f;
  if (kicker != nullptr) {
    skill = kicker->GetStat(technical_shot) * 0.7f +
            kicker->GetStat(technical_highpass) * 0.3f;
  }
  const float prob =
      0.82f - lateralRatio * 0.55f - depthRatio * 0.25f + (skill - 0.5f) * 0.35f;
  return clamp(prob, 0.10f, 0.95f);
}

bool Match::ResolveRugbyKickAtGoal(Team *kickingTeam, const Vector3 &markPos,
                                   int awardPoints, const std::string &label) {
  DO_VALIDATION;
  if (kickingTeam == nullptr || awardPoints <= 0) return false;
  Player *kicker = FindRugbyKickAtGoalKicker(kickingTeam);
  const float prob = ComputeRugbyKickAtGoalProb(kicker, markPos);
  const bool success = boostrandom(0.0f, 1.0f) < prob;
  if (success) {
    const int teamID = kickingTeam->GetID();
    matchData->SetGoalCount(teamID,
                            matchData->GetGoalCount(teamID) + awardPoints);
    scoreboard->SetGoalCount(teamID, matchData->GetGoalCount(teamID));
    std::string kickerName;
    if (kicker != nullptr) {
      kickerName = " " + kicker->GetPlayerData()->GetLastName();
    }
    SpamMessage(label + " GOOD" + kickerName + "! +" +
                    std::to_string(awardPoints),
                3500);
  } else {
    SpamMessage(label + " missed", 3000);
  }
  return success;
}

void Match::RunRugbyAI() {
  DO_VALIDATION;
  if (!IsRugbyScenario() || !IsInPlay() || IsInSetPiece()) return;
  if (goalScored || rugbyBreakdownActive) return;

  Player *carrier = GetBallRetainer();

  if (carrier == nullptr) {
    // Loose ball: only the nearest 3 players from each team chase it.
    const Vector3 ballPos = ball->Predict(0);
    for (int teamIndex : {first_team, second_team}) {
      std::vector<Player *> players;
      teams[teamIndex]->GetActivePlayers(players);
      std::sort(players.begin(), players.end(),
                [&](Player *a, Player *b) {
                  return (a->GetPosition() - ballPos).GetLength() <
                         (b->GetPosition() - ballPos).GetLength();
                });
      int chased = 0;
      for (Player *p : players) {
        if (p == nullptr || !p->IsActive()) continue;
        if (p->GetFormationEntry().role == e_PlayerRole_GK) continue;
        if (chased >= 3) break;
        chased++;
        Vector3 toBall = ballPos - p->GetPosition();
        toBall.coords[2] = 0;
        const float d = toBall.GetLength();
        if (d < 0.3f) continue;
        const float step = std::min(d, 0.11f);
        p->OffsetPosition(toBall * (step / d));
      }
    }
    return;
  }

  Team *attackTeam = carrier->GetTeam();
  Team *defendTeam = attackTeam->Opponent();
  const signed int attackSide = attackTeam->GetDynamicSide();
  const Vector3 carrierPos = carrier->GetPosition();

  // --- Carrier: sprint toward the opposition try line. ---
  {
    const float sprintStep = 0.11f;  // ~11 m/s at 100 steps/s
    Vector3 forward(attackSide * sprintStep, 0, 0);
    // Slight drift toward the middle so the carrier doesn't walk into
    // touch.
    forward.coords[1] -= signSide(carrierPos.coords[1]) * 0.012f;
    carrier->OffsetPosition(forward);
  }

  // --- Attacking shape: two closest teammates run as support backs (one
  // each side, 2 m back), others hold a flat backline 6 m behind the
  // carrier spread across ~25 m of pitch width. ---
  {
    std::vector<Player *> attackers;
    attackTeam->GetActivePlayers(attackers);
    // Remove carrier + GK from the pool.
    attackers.erase(std::remove_if(attackers.begin(), attackers.end(),
                                   [&](Player *p) {
                                     return p == nullptr || p == carrier ||
                                            !p->IsActive() ||
                                            p->GetFormationEntry().role ==
                                                e_PlayerRole_GK;
                                   }),
                    attackers.end());
    // Nearest two → immediate support.
    std::sort(attackers.begin(), attackers.end(),
              [&](Player *a, Player *b) {
                return (a->GetPosition() - carrierPos).GetLength() <
                       (b->GetPosition() - carrierPos).GetLength();
              });
    const int supportCount =
        std::min(2, static_cast<int>(attackers.size()));
    for (int i = 0; i < supportCount; ++i) {
      Player *p = attackers[i];
      const float lateral = (i == 0 ? -1.0f : 1.0f) * 3.0f;
      Vector3 target(carrierPos.coords[0] - attackSide * 2.5f,
                     carrierPos.coords[1] + lateral, 0);
      Vector3 delta = target - p->GetPosition();
      delta.coords[2] = 0;
      const float d = delta.GetLength();
      if (d < 0.4f) continue;
      const float step = std::min(d, 0.10f);
      p->OffsetPosition(delta * (step / d));
    }
    // Remaining attackers: a flat line 6 m behind the carrier, spread
    // evenly across the pitch on their team's half.
    const int lineCount = static_cast<int>(attackers.size()) - supportCount;
    for (int i = 0; i < lineCount; ++i) {
      Player *p = attackers[supportCount + i];
      const float spread = 25.0f;
      const float slotY = -spread * 0.5f +
                          spread * (i + 0.5f) / std::max(lineCount, 1);
      Vector3 target(carrierPos.coords[0] - attackSide * 6.0f,
                     slotY, 0);
      Vector3 delta = target - p->GetPosition();
      delta.coords[2] = 0;
      const float d = delta.GetLength();
      if (d < 0.5f) continue;
      const float step = std::min(d, 0.09f);
      p->OffsetPosition(delta * (step / d));
    }
  }

  // --- Defensive line: flat line 2 m in front of the carrier, spread
  // across the pitch. Closest defender charges the carrier; the rest
  // hold the line. ---
  {
    std::vector<Player *> defenders;
    defendTeam->GetActivePlayers(defenders);
    defenders.erase(std::remove_if(defenders.begin(), defenders.end(),
                                   [](Player *p) {
                                     return p == nullptr || !p->IsActive() ||
                                            p->GetFormationEntry().role ==
                                                e_PlayerRole_GK;
                                   }),
                    defenders.end());
    std::sort(defenders.begin(), defenders.end(),
              [&](Player *a, Player *b) {
                return (a->GetPosition() - carrierPos).GetLength() <
                       (b->GetPosition() - carrierPos).GetLength();
              });
    for (int i = 0; i < static_cast<int>(defenders.size()); ++i) {
      Player *p = defenders[i];
      Vector3 target;
      if (i == 0) {
        // Nearest defender goes straight for the tackle.
        target = carrierPos;
      } else {
        const float spread = 30.0f;
        const float slotY =
            -spread * 0.5f +
            spread * (i - 0.5f) / std::max<int>(defenders.size() - 1, 1);
        target = Vector3(carrierPos.coords[0] + attackSide * 2.0f, slotY, 0);
      }
      Vector3 delta = target - p->GetPosition();
      delta.coords[2] = 0;
      const float d = delta.GetLength();
      if (d < 0.4f) continue;
      const float step = std::min(d, (i == 0) ? 0.11f : 0.09f);
      p->OffsetPosition(delta * (step / d));
    }
  }

  // Carrier autopass: if a defender is within 2.2 m, release the ball so
  // possession keeps moving rather than collapsing into a tackle.
  float nearestDef = 1e9f;
  {
    std::vector<Player *> defenders2;
    defendTeam->GetActivePlayers(defenders2);
    for (Player *p : defenders2) {
      if (p == nullptr || !p->IsActive()) continue;
      if (p->GetFormationEntry().role == e_PlayerRole_GK) continue;
      nearestDef = std::min(nearestDef,
                            (p->GetPosition() - carrierPos).GetLength());
    }
    if (nearestDef < 2.2f) {
      TryRugbyPass(attackTeam->GetID());
    }
  }

  // Kick to touch: if carrier is deep in own half (≥15 m from halfway on
  // their side) or is cornered with a defender close and no valid
  // backward pass, hoof the ball toward the nearest sideline. Ball flies
  // out → referee awards a lineout. Small per-tick probability keeps it
  // rare enough that passes stay the default.
  const float carrierOwnDepth =
      -carrierPos.coords[0] * attackSide;  // >0 means in own half
  const bool deepInOwnHalf = carrierOwnDepth > 15.0f;
  const bool cornered = nearestDef < 2.0f;
  const float kickProb = deepInOwnHalf ? 0.02f : (cornered ? 0.015f : 0.0f);
  if (kickProb > 0.0f && boostrandom(0.0f, 1.0f) < kickProb) {
    const float touchSide = signSide(carrierPos.coords[1]);
    const Vector3 kickFrom = carrierPos + Vector3(0, 0, 1.02f);
    Vector3 kickTarget(
        carrierPos.coords[0] +
            attackSide * std::min(15.0f, carrierOwnDepth + 5.0f),
        touchSide * (pitchHalfH + 2.0f), 0.2f);
    const float flightTime = 0.9f;
    Vector3 velocity = (kickTarget - kickFrom) / flightTime;
    velocity.coords[2] += 6.0f;  // high kick arc
    ball->SetPosition(kickFrom);
    ball->SetMomentum(velocity);
    SetBallRetainer(0);
    SetLastTouchTeamID(attackTeam->GetID(), e_TouchType_Intentional_Kicked);
    // Lock passer pickup so auto-pickup doesn't snatch it on the next tick.
    rugbyLastPasser = carrier;
    rugbyPasserPickupLockUntil_ms = actualTime_ms + 400;
    rugbyPendingPassReceiver = 0;
    rugbyPassInFlightUntil_ms = 0;
  }
}

bool Match::TryRugbyPass(int teamID) {
  DO_VALIDATION;
  if (!IsRugbyScenario() || !IsInPlay() || IsInSetPiece()) return false;
  Player *carrier = GetBallRetainer();
  if (carrier == nullptr) return false;
  if (carrier->GetTeam()->GetID() != teamID) return false;
  if (actualTime_ms < rugbyLastPassTime_ms + 500) return false;

  Team *team = carrier->GetTeam();
  const signed int side = team->GetDynamicSide();
  const Vector3 carrierPos = carrier->GetPosition();

  // Pick the nearest teammate who sits behind or level with the carrier —
  // forward passes are illegal in rugby, so the AI never targets them.
  std::vector<Player *> teammates;
  team->GetActivePlayers(teammates);
  Player *target = nullptr;
  float bestDist = 22.0f;
  for (Player *p : teammates) {
    if (p == nullptr || p == carrier || !p->IsActive()) continue;
    if (p->GetFormationEntry().role == e_PlayerRole_GK) continue;
    const float forwardOffset =
        (p->GetPosition().coords[0] - carrierPos.coords[0]) * side;
    if (forwardOffset > 0.2f) continue;  // forward pass forbidden
    const float dist = (p->GetPosition() - carrierPos).GetLength();
    if (dist < 1.5f) continue;  // receiver too close for a meaningful pass
    if (dist < bestDist) {
      bestDist = dist;
      target = p;
    }
  }
  if (target == nullptr) return false;

  const Vector3 carrierHand = carrierPos + Vector3(0, 0, 1.02f);
  const Vector3 targetHand = target->GetPosition() + Vector3(0, 0, 1.02f);
  Vector3 passVec = targetHand - carrierHand;
  const float passLen = passVec.GetLength();
  if (passLen < 0.01f) return false;
  // Seed the ball 0.9 m along the flight line so the passer doesn't
  // immediately re-absorb it via auto-pickup on the same tick.
  const Vector3 from = carrierHand + passVec * (0.9f / passLen);
  const Vector3 to = targetHand;
  const float flightTime = 0.45f;
  Vector3 velocity = (to - from) / flightTime;
  velocity.coords[2] += 1.3f;  // small upward arc

  ball->SetPosition(from);
  ball->SetMomentum(velocity);
  SetBallRetainer(0);
  // Mark as the carrier's last touch so forward-pass detection stays
  // anchored to them and the receiver's auto-pickup counts as a new touch.
  SetLastTouchTeamID(team->GetID(), e_TouchType_Intentional_Nonkicked);
  rugbyLastPassTime_ms = actualTime_ms;
  rugbyLastPasser = carrier;
  rugbyPasserPickupLockUntil_ms = actualTime_ms + 400;
  rugbyPendingPassReceiver = target;
  rugbyPassInFlightUntil_ms =
      actualTime_ms + static_cast<unsigned long>(flightTime * 1000.0f) + 150;
  return true;
}

bool Match::CheckForRugbyDropGoal(int &scoringTeamID) {
  DO_VALIDATION;
  scoringTeamID = -1;
  if (!IsRugbyScenario() || !IsInPlay() || IsInSetPiece() || goalScored) {
    return false;
  }
  if (rugbyLastKickedByTeamID < 0) return false;
  if (actualTime_ms < rugbyLastKickTime_ms + 50) return false;
  // Avoid re-awarding while ball still in the same kick trajectory.
  if (actualTime_ms < rugbyLastDropGoalCheckTime_ms + 80) return false;
  rugbyLastDropGoalCheckTime_ms = actualTime_ms;
  // The ball must cross the try line with sufficient height through the
  // uprights. Posts half-width is ~2.8m (5.6m apart) — use 2.8 engine units.
  const float kPostHalfWidth = 2.8f;
  const float kCrossbarHeight = 3.0f;
  const Vector3 ballPos = ball->Predict(0);
  Team *kickerTeam = teams[rugbyLastKickedByTeamID];
  if (kickerTeam == nullptr) return false;
  const signed int attackingSide = kickerTeam->GetDynamicSide();
  const bool crossedTryLine =
      (attackingSide > 0 && ballPos.coords[0] > pitchHalfW) ||
      (attackingSide < 0 && ballPos.coords[0] < -pitchHalfW);
  if (!crossedTryLine) return false;
  if (ballPos.coords[2] < kCrossbarHeight) return false;
  if (std::fabs(ballPos.coords[1]) > kPostHalfWidth) return false;
  // Only from an open-play kick, not during a set piece restart itself.
  scoringTeamID = kickerTeam->GetID();
  return true;
}

float Match::GetRugbyOffsideLine(int teamID) const {
  DO_VALIDATION;
  if (!IsRugbyScenario()) return 0.0f;
  if (!rugbyBreakdownActive || rugbyBreakdownTeam == 0) return rugbyBreakdownPos.coords[0];

  const signed int protected_side = rugbyBreakdownTeam->GetDynamicSide();
  if (teamID == rugbyBreakdownTeam->GetID()) {
    return rugbyBreakdownPos.coords[0] - protected_side * 0.9f;
  }
  return rugbyBreakdownPos.coords[0] + protected_side * 0.4f;
}

bool Match::IsRugbyPlayerOffsideAtBreakdown(Player *player) const {
  DO_VALIDATION;
  if (!IsRugbyScenario() || !rugbyBreakdownActive || rugbyBreakdownTeam == 0 ||
      player == nullptr || !player->IsActive()) {
    return false;
  }
  if (player == rugbyTackledPlayer || player == rugbyTackler) {
    return false;
  }
  if (player == rugbyRecycleReceiver) {
    return false;
  }
  Team *team = player->GetTeam();
  const float offsideLine = GetRugbyOffsideLine(team->GetID());
  const float signedDistance =
      (player->GetPosition().coords[0] - offsideLine) * team->GetDynamicSide();
  return signedDistance > 0.02f;
}

bool Match::IsRugbyPlayerPenaltyEligibleAtBreakdown(Player *player) const {
  DO_VALIDATION;
  if (!IsRugbyPlayerOffsideAtBreakdown(player)) {
    return false;
  }
  const Vector3 offset = player->GetPosition() - rugbyBreakdownPos;
  const float lateralDistance = std::fabs(offset.coords[1]);
  const float depthDistance = std::fabs(offset.coords[0]);
  return lateralDistance < 0.45f && depthDistance < 1.2f;
}

void Match::EnforceRugbyBreakdownOffside() {
  DO_VALIDATION;
  if (!IsRugbyScenario() || !rugbyBreakdownActive || rugbyBreakdownTeam == 0) {
    return;
  }

  for (int teamIndex : {first_team, second_team}) {
    const std::vector<Player *> &players = teams[teamIndex]->GetAllPlayers();
    const float offsideLine = GetRugbyOffsideLine(teams[teamIndex]->GetID());
    const float legalBuffer = 0.02f;
    const float targetX =
        offsideLine - teams[teamIndex]->GetDynamicSide() * legalBuffer;
    for (Player *player : players) {
      if (!IsRugbyPlayerOffsideAtBreakdown(player)) {
        continue;
      }
      Vector3 offset(0);
      offset.coords[0] = targetX - player->GetPosition().coords[0];
      player->OffsetPosition(offset);
    }
  }
}

void Match::RegisterRugbyTackle(Player *carrier, Player *tackler, bool force) {
  DO_VALIDATION;
  if (!IsRugbyScenario() || (IsInSetPiece() && !force)) return;
  if (carrier == nullptr || tackler == nullptr) return;
  if (!force && carrier != GetBallRetainer()) return;
  if (carrier->GetTeam() == tackler->GetTeam()) return;
  if (goalScored || rugbyBreakdownActive) return;

  rugbyBreakdownActive = true;
  rugbyBreakdownStartTime_ms = actualTime_ms;
  rugbyBreakdownPos = carrier->GetPosition();
  rugbyBreakdownPos.coords[2] = 0.0f;
  rugbyBreakdownTeam = carrier->GetTeam();
  rugbyTackledPlayer = carrier;
  rugbyTackler = tackler;
  rugbyRecycleReceiver = 0;
  rugbyRecycleStartTime_ms = 0;
  rugbyProtectedTeam = carrier->GetTeam();
  rugbyPossessionProtectionUntil_ms = actualTime_ms + 900;

  Vector3 groundedBallPos = rugbyBreakdownPos;
  groundedBallPos.coords[0] -= carrier->GetTeam()->GetDynamicSide() * 0.9f;
  groundedBallPos.coords[2] = 0.11f;
  ball->SetPosition(groundedBallPos);
  ball->SetMomentum(Vector3(0));
  SetBallRetainer(0);
  designatedPossessionPlayer = carrier;
  rugbyPendingInitialBreakdown = false;
  SpamMessage("Tackle", 800);
}

Player *Match::GetRugbyRecycleTarget() const {
  DO_VALIDATION;
  if (!rugbyBreakdownActive || rugbyBreakdownTeam == 0) return nullptr;

  std::vector<Player *> candidates;
  AI_GetClosestPlayers(rugbyBreakdownTeam, rugbyBreakdownPos, false, candidates,
                       rugbyBreakdownTeam->GetAllPlayers().size());
  const signed int side = rugbyBreakdownTeam->GetDynamicSide();
  for (Player *candidate : candidates) {
    if (candidate == nullptr || !candidate->IsActive()) continue;
    if (candidate == rugbyTackledPlayer) continue;
    if (candidate->GetFormationEntry().role == e_PlayerRole_GK) continue;
    if ((candidate->GetPosition().coords[0] - rugbyBreakdownPos.coords[0]) *
            side <=
        1.0f) {
      return candidate;
    }
  }

  if (rugbyTackledPlayer != nullptr && rugbyTackledPlayer->IsActive()) {
    return rugbyTackledPlayer;
  }
  return rugbyBreakdownTeam->GetDesignatedTeamPossessionPlayer();
}

void Match::UpdateRugbyPhase() {
  DO_VALIDATION;
  if (!IsRugbyScenario() || !rugbyBreakdownActive) return;

  if (goalScored) {
    rugbyBreakdownActive = false;
    rugbyBreakdownTeam = 0;
    rugbyTackledPlayer = 0;
    rugbyTackler = 0;
    rugbyRecycleReceiver = 0;
    rugbyRecycleStartTime_ms = 0;
    return;
  }

  // Soft-magnet nearby players toward the breakdown so the ruck visibly
  // clumps up. Only pulls players already close enough to be committed;
  // keeps further-out defenders/attackers on their line so the offside
  // enforcement pass that ran earlier this tick isn't immediately undone.
  {
    const float commitRadius = 3.2f;
    const float maxStep = 0.04f;
    for (int teamIndex : {first_team, second_team}) {
      std::vector<Player *> players;
      teams[teamIndex]->GetActivePlayers(players);
      for (Player *player : players) {
        if (player == nullptr || !player->IsActive()) continue;
        if (player == rugbyTackledPlayer || player == rugbyTackler) continue;
        if (player == rugbyRecycleReceiver) continue;
        if (player == GetBallRetainer()) continue;
        if (player->GetFormationEntry().role == e_PlayerRole_GK) continue;
        Vector3 delta = rugbyBreakdownPos - player->GetPosition();
        delta.coords[2] = 0.0f;
        const float distance = delta.GetLength();
        if (distance > commitRadius || distance < 0.3f) continue;
        Vector3 step = delta * 0.10f;
        const float stepLen = step.GetLength();
        if (stepLen > maxStep) step = step * (maxStep / stepLen);
        player->OffsetPosition(step);
      }
    }
  }

  const signed int side = rugbyBreakdownTeam->GetDynamicSide();
  Vector3 groundedBallPos = rugbyBreakdownPos;
  groundedBallPos.coords[0] -= side * 0.9f;
  groundedBallPos.coords[2] = 0.11f;

  if (rugbyRecycleReceiver == nullptr) {
    ball->SetPosition(groundedBallPos);
    ball->SetMomentum(Vector3(0));
    SetBallRetainer(0);
  }

  if (actualTime_ms < rugbyBreakdownStartTime_ms + 700 &&
      rugbyRecycleReceiver == nullptr) {
    return;
  }

  if (rugbyRecycleReceiver == nullptr) {
    Player *receiver = GetRugbyRecycleTarget();
    if (receiver != nullptr) {
      rugbyRecycleReceiver = receiver;
      rugbyRecycleStartTime_ms = actualTime_ms;
      designatedPossessionPlayer = receiver;
      SetBallRetainer(receiver);
      Vector3 recyclePos = receiver->GetPosition();
      recyclePos.coords[2] = 0.11f;
      ball->SetPosition(recyclePos);
      ball->SetMomentum(Vector3(0));
      rugbyProtectedTeam = receiver->GetTeam();
      rugbyPossessionProtectionUntil_ms = actualTime_ms + 900;
      SetLastTouchTeamID(receiver->GetTeam()->GetID(),
                         e_TouchType_Intentional_Nonkicked);
    }
    return;
  }

  const bool receiverStillValid =
      rugbyRecycleReceiver != nullptr && rugbyRecycleReceiver->IsActive() &&
      rugbyRecycleReceiver->GetTeam() == rugbyBreakdownTeam;
  if (!receiverStillValid) {
    rugbyRecycleReceiver = 0;
    return;
  }

  if (GetBallRetainer() != rugbyRecycleReceiver) {
    SetBallRetainer(rugbyRecycleReceiver);
  }
  designatedPossessionPlayer = rugbyRecycleReceiver;

  const Vector3 liftVector =
      ball->Predict(0).Get2D() - groundedBallPos.Get2D();
  const bool ballLiftedFromBase = liftVector.GetLength() > 0.35f;
  const bool receiverCarryingAway =
      (rugbyRecycleReceiver->GetPosition().coords[0] - rugbyBreakdownPos.coords[0]) *
          side >
      0.2f;
  if (actualTime_ms < rugbyRecycleStartTime_ms + 150 ||
      (!ballLiftedFromBase && !receiverCarryingAway)) {
    return;
  }

  rugbyBreakdownActive = false;
  rugbyBreakdownTeam = 0;
  rugbyTackledPlayer = 0;
  rugbyTackler = 0;
  rugbyRecycleReceiver = 0;
  rugbyRecycleStartTime_ms = 0;
}

const std::vector<Vector3> &Match::GetAnimPositionCache(Animation *anim) const {
  return GetContext().animPositionCache.find(anim)->second;
}

Match::Match(MatchData *matchData, const std::vector<AIControlledKeyboard *> &controllers, bool animations)
    : matchData(matchData),
      first_team(GetScenarioConfig().reverse_team_processing ? 1 : 0),
      second_team(GetScenarioConfig().reverse_team_processing ? 0 : 1),
      controllers(controllers),
      possessionSideHistory(6000),
      matchDurationFactor(
          GetConfiguration()->GetReal("match_duration", 1.0) * 0.2f + 0.05f),
      _useMagnet(GetScenarioConfig().use_magnet) {
  DO_VALIDATION;
  auto& anims = GetContext().anims;
  GetContext().stablePlayerCount = 0;

  // shared ptr to menutask, because menutask shouldn't die before match does
  menuTask = GetMenuTask();

  actualTime_ms = 0;
  goalScoredTimer = 0;

  resetNetting = false;
  nettingHasChanged = false;

  dynamicNode = boost::intrusive_ptr<Node>(new Node("dynamicNode"));
  GetScene3D()->AddNode(dynamicNode);

  ball = new Ball(this);

  if (!anims) {
    DO_VALIDATION;
    anims = boost::shared_ptr<AnimCollection>(new AnimCollection());
    anims->Load();
    // cache animation positions

    const std::vector < Animation* > &animationsTmp = anims->GetAnimations();
    for (unsigned int i = 0; i < animationsTmp.size(); i++) {
      DO_VALIDATION;
      std::vector<Vector3> positions;
      Animation *someAnim = animationsTmp[i];
      Quaternion dud;
      Vector3 position;
      for (int frame = 0; frame < someAnim->GetFrameCount(); frame++) {
        DO_VALIDATION;
        someAnim->GetKeyFrame(player, frame, dud, position);
        position.coords[2] = 0.0f;
        positions.push_back(position);
      }
      GetContext().animPositionCache.insert(std::pair < Animation*, std::vector<Vector3> >(someAnim, positions));
    }
    GetVertexColors(GetContext().colorCoords);
  } else {
    for (auto& a : anims->GetAnimations()) {
      a->DirtyCache();
    }
  }
  // full body model template

  ObjectLoader loader;
  if (!GetContext().fullbodyNode) {
    GetContext().fullbodyNode = loader.LoadObject("media/objects/players/fullbody.object");
  }

  designatedPossessionPlayer = 0;


  // teams

  assert(matchData != 0);

  teams[first_team] =
      new Team(first_team, this, &matchData->GetTeamData(first_team),
               first_team ? GetScenarioConfig().right_team_difficulty
                          : GetScenarioConfig().left_team_difficulty);
  teams[second_team] =
      new Team(second_team, this, &matchData->GetTeamData(second_team),
               second_team ? GetScenarioConfig().right_team_difficulty
                           : GetScenarioConfig().left_team_difficulty);
  teams[first_team]->SetOpponent(teams[second_team]);
  teams[second_team]->SetOpponent(teams[first_team]);
  teams[first_team]->InitPlayers(GetContext().fullbodyNode, GetContext().colorCoords);
  teams[second_team]->InitPlayers(GetContext().fullbodyNode, GetContext().colorCoords);

  std::vector<Player*> activePlayers;
  teams[first_team]->GetActivePlayers(activePlayers);
  designatedPossessionPlayer = activePlayers.at(0);
  ballRetainer = 0;
  rugbyPendingInitialBreakdown = GetScenarioConfig().rugby_force_initial_breakdown;
  rugbyPendingInitialTry = GetScenarioConfig().rugby_force_initial_try;


  // officials

  std::string kitFilename = "media/objects/players/textures/referee_kit.png";
  boost::intrusive_ptr<Resource<Surface> > kit =
      GetContext().surface_manager.Fetch(kitFilename);
  officials = new Officials(this, GetContext().fullbodyNode, GetContext().colorCoords, kit, anims);

  dynamicNode->AddObject(officials->GetYellowCardGeom());
  dynamicNode->AddObject(officials->GetRedCardGeom());

  // camera

  camera = new Camera("camera");
  GetScene3D()->CreateSystemObjects(camera);
  camera->Init();

  camera->SetFOV(25);
  cameraNode = boost::intrusive_ptr<Node>(new Node("cameraNode"));
  cameraNode->AddObject(camera);
  cameraNode->SetPosition(Vector3(40, 0, 100));
  GetDynamicNode()->AddNode(cameraNode);

  autoUpdateIngameCamera = true;


  // stadium
  Node* tmpStadiumNode;
  if (GetGameConfig().render) {
    GetTracker()->setDisabled(true);
    if (!GetContext().stadiumRender) {
      GetContext().stadiumRender = loader.LoadObject("media/objects/stadiums/test/test.object");
    }
    tmpStadiumNode = GetContext().stadiumRender.get();
    RandomizeAdboards(tmpStadiumNode);
    GetTracker()->setDisabled(false);
  } else {
    if (!GetContext().stadiumNoRender) {
      GetContext().stadiumNoRender = loader.LoadObject("media/objects/stadiums/test/pitchonly.object");
    }
    tmpStadiumNode = GetContext().stadiumNoRender.get();
  }
  std::list < boost::intrusive_ptr<Geometry> > stadiumGeoms;

  // split stadium geometry into multiple geometry objects, for more efficient culling
  tmpStadiumNode->GetObjects<Geometry>(e_ObjectType_Geometry, stadiumGeoms);
  assert(stadiumGeoms.size() != 0);

  stadiumNode = boost::intrusive_ptr<Node>(new Node("stadium"));

  std::list < boost::intrusive_ptr<Geometry> >::iterator iter = stadiumGeoms.begin();
  while (iter != stadiumGeoms.end()) {
    DO_VALIDATION;
    boost::intrusive_ptr<Node> tmpNode = SplitGeometry(GetScene3D(), *iter, 24);
    tmpNode->SetLocalMode(e_LocalMode_Absolute);
    stadiumNode->AddNode(tmpNode);

    iter++;
  }

  stadiumNode->SetLocalMode(e_LocalMode_Absolute);
  GetScene3D()->AddNode(stadiumNode);


  // goal netting
  if (!GetContext().goalsNode) {
    GetContext().goalsNode = loader.LoadObject("media/objects/stadiums/goals.object");
    GetContext().goalsNode->SetLocalMode(e_LocalMode_Absolute);
  }
  GetScene3D()->AddNode(GetContext().goalsNode);
  PrepareGoalNetting();


  // pitch
  if (GetGameConfig().render) {
    GeneratePitch(2048, 1024, 1024, 512, 2048, 1024);
  }

  // sun
  sunNode = loader.LoadObject("media/objects/lighting/generic.object");
  GetDynamicNode()->AddNode(sunNode);
  SetRandomSunParams();


  // human gamers
  UpdateControllerSetup();


  // 12th man sound

  // match params

  matchTime_ms = 0;
  lastGoalTeam = 0;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    DO_VALIDATION;
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;
  SetMatchPhase(e_MatchPhase_PreMatch);

  // everybody hates him, this poor bloke
  referee = new Referee(this, animations);

  // GUI
  Gui2Root *root = menuTask->GetWindowManager()->GetRoot();

  radar = new Gui2Radar(menuTask->GetWindowManager(), "game_radar", 38, 78, 24, 18, this, matchData->GetTeamData(0).GetColor1(), matchData->GetTeamData(0).GetColor2(), matchData->GetTeamData(1).GetColor1(), matchData->GetTeamData(1).GetColor2());
  root->AddView(radar);
  radar->Show();

  scoreboard = new Gui2ScoreBoard(menuTask->GetWindowManager(), this);
  root->AddView(scoreboard);
  scoreboard->Show();

  messageCaption = new Gui2Caption(menuTask->GetWindowManager(), "game_messages", 0, 0, 80, 8, "");
  messageCaption->SetTransparency(0.3f);
  root->AddView(messageCaption);
  messageCaptionRemoveTime_ms = actualTime_ms + 5000;

  // for usage in destructor
  scene3D = GetScene3D();

  lastBodyBallCollisionTime_ms = 0;

  menuTask->GetWindowManager()->GetPageFactory()->CreatePage((int)e_PageID_Game, 0);
}

Match::~Match() { DO_VALIDATION; }

void Match::Mirror(bool team_0, bool team_1, bool ball) {
  GetTracker()->setDisabled(true);
  if (team_0) {
    teams[0]->Mirror();
  }
  if (team_1) {
    teams[1]->Mirror();
  }
  if (ball) {
    ball_mirrored = !ball_mirrored;
    this->ball->Mirror();
  }
  for (auto &i : mentalImages) {
    i.Mirror(team_0, team_1, ball);
  }
  GetTracker()->setDisabled(false);
}

void Match::Exit() {
  DO_VALIDATION;
  teams[first_team]->Exit();
  teams[second_team]->Exit();
  delete teams[first_team];
  delete teams[second_team];
  delete officials;
  delete ball;
  delete referee;
  delete matchData;
  menuTask->SetMatchData(0);
  mentalImages.clear();

  messageCaption->Exit();
  delete messageCaption;

  scene3D->DeleteNode(GetDynamicNode());
  scene3D->DeleteNode(stadiumNode);
  radar->Exit();
  delete radar;

  scoreboard->Exit();
  delete scoreboard;

  menuTask.reset();
}

void Match::SetRandomSunParams() {
  DO_VALIDATION;

  float brightness = 1.0f;

  Vector3 sunPos = Vector3(-1.2f, 0.4f, 1.0f); // sane default
  float averageHeightMultiplier = 1.3f;
  sunPos = Vector3(clamp(boostrandom(-1.7f, 1.7f), -1.0, 1.0),
                   clamp(boostrandom(-1.7f, 1.7f), -1.0, 1.0),
                   averageHeightMultiplier);
  sunPos.Normalize();
  if (boostrandom(0, 1) > 0.5f && sunPos.coords[1] > 0.25f)
    sunPos.coords[1] =
        -sunPos.coords[1];  // sun more often on (default) camera side (coming
                            // from front == clearer lighting on players)
  sunNode->GetObject("sun")->SetPosition(sunPos * 10000.0f);

  float defaultRadius = 1000000.0f;
  float sunRadius = defaultRadius;
  boost::static_pointer_cast<Light>(sunNode->GetObject("sun"))->SetRadius(sunRadius);

  Vector3 sunColorNoon(0.9, 0.8, 1.0); sunColorNoon *= 1.4f;
  Vector3 sunColorDusk(1.4, 0.9, 0.7); sunColorDusk *= 1.2f;

  float noonBias =
      std::pow(NormalizedClamp(sunPos.coords[2], 0.5f, 1.0f), 1.2f);
  Vector3 sunColor = sunColorNoon * noonBias + sunColorDusk * (1.0f - noonBias);

  Vector3 randomAddition(boostrandom(-0.1, 0.1), boostrandom(-0.1, 0.1),
                         boostrandom(-0.1, 0.1));
  randomAddition *= 1.2f;
  sunColor += randomAddition;

  boost::static_pointer_cast<Light>(sunNode->GetObject("sun"))->SetColor(sunColor * brightness);
}

void Match::RandomizeAdboards(boost::intrusive_ptr<Node> stadiumNode) {
  DO_VALIDATION;
  // collect texture files

  std::vector<std::string> files;
  GetFiles("media/textures/adboards", "bmp", files);
  sort(files.begin(), files.end());

  std::vector < boost::intrusive_ptr < Resource<Surface> > > adboardSurfaces;
  for (unsigned int i = 0; i < files.size(); i++) {
    DO_VALIDATION;
    adboardSurfaces.push_back(GetContext().surface_manager.Fetch(files[i]));
  }
  if (adboardSurfaces.empty()) return;


  // collect adboard geoms

  std::list < boost::intrusive_ptr<Geometry> > stadiumGeoms;
  stadiumNode->GetObjects<Geometry>(e_ObjectType_Geometry, stadiumGeoms, true);
  // replace

  std::list < boost::intrusive_ptr<Geometry> >::const_iterator stadiumGeomsIter = stadiumGeoms.begin();
  while (stadiumGeomsIter != stadiumGeoms.end()) {
    DO_VALIDATION;

    boost::intrusive_ptr<Geometry> geomObject = *stadiumGeomsIter;
    assert(geomObject != boost::intrusive_ptr<Object>());
    boost::intrusive_ptr< Resource<GeometryData> > adboardGeom = geomObject->GetGeometryData();

    std::vector < MaterializedTriangleMesh > &tmesh = adboardGeom->GetResource()->GetTriangleMeshesRef();

    for (unsigned int i = 0; i < tmesh.size(); i++) {
      DO_VALIDATION;
      if (tmesh[i].material.diffuseTexture !=
          boost::intrusive_ptr<Resource<Surface> >()) {
        DO_VALIDATION;
        std::string identString = tmesh[i].material.diffuseTexture->GetIdentString();
        //printf("%s\n", identString.c_str());
        if (identString.find("ad_placeholder") == 0) {
          DO_VALIDATION;
          tmesh[i].material.diffuseTexture = adboardSurfaces.at(
              int(std::floor(random_non_determ(0, adboardSurfaces.size() - 1.001f))));
          tmesh[i].material.specular_amount = 0.2f;
          tmesh[i].material.shininess = 0.1f;
        }
      }
    }

    geomObject->OnUpdateGeometryData();

    stadiumGeomsIter++;
  }
}

void Match::UpdateControllerSetup() {
  DO_VALIDATION;
  const std::vector<SideSelection> controller = menuTask->GetControllerSetup();
  std::vector<AIControlledKeyboard*> left_players;
  std::vector<AIControlledKeyboard*> right_players;
  for (unsigned int i = 0; i < controller.size(); i++) {
    DO_VALIDATION;
    float mirror = 1.0;
    if (controller[i].side == -1) {
      left_players.push_back(controllers.at(controller[i].controllerID));
      DO_VALIDATION;
    } else if (controller[i].side == 1) {
      right_players.push_back(controllers.at(controller[i].controllerID));
      DO_VALIDATION;
      if (teams[1]->GetDynamicSide() == -1) {
        DO_VALIDATION;
        mirror = -1.0;
      }
    }
    controllers.at(controller[i].controllerID)->Mirror(mirror);
  }
  teams[0]->AddHumanGamers(left_players);
  teams[1]->AddHumanGamers(right_players);
}

void Match::SpamMessage(const std::string &msg, int time_ms) {
  DO_VALIDATION;
  messageCaption->SetCaption(msg);
  float w = messageCaption->GetTextWidthPercent();
  messageCaption->SetPosition(50 - w * 0.5f, 5);
  messageCaption->Show();
  messageCaptionRemoveTime_ms = actualTime_ms + time_ms;
}

void Match::GetActiveTeamPlayers(int teamID, std::vector<Player *> &players) {
  DO_VALIDATION;
  teams[teamID]->GetActivePlayers(players);
}

void Match::GetOfficialPlayers(std::vector<PlayerBase *> &players) {
  DO_VALIDATION;
  officials->GetPlayers(players);
}

MentalImage *Match::GetMentalImage(int history_ms) {
  DO_VALIDATION;
  int index = int(round((float)history_ms / 100.0));
  if (index >= (signed int)mentalImages.size()) index = mentalImages.size() - 1;
  if (index < 0) index = 0;
  return &mentalImages[index];
}

void Match::UpdateLatestMentalImageBallPredictions() {
  DO_VALIDATION;
  if (!mentalImages.empty()) mentalImages[0].UpdateBallPredictions();
}

void Match::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  camPos.clear();
  SetBallRetainer(0);
  SetGoalScored(false);
  mentalImages.clear();
  goalScored = false;
  ballIsInGoal = false;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    DO_VALIDATION;
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeam = 0;
  rugbyBreakdownActive = false;
  rugbyPendingInitialBreakdown = GetScenarioConfig().rugby_force_initial_breakdown;
  rugbyBreakdownStartTime_ms = 0;
  rugbyBreakdownPos = Vector3(0);
  rugbyBreakdownTeam = 0;
  rugbyTackledPlayer = 0;
  rugbyTackler = 0;
  rugbyRecycleReceiver = 0;
  rugbyRecycleStartTime_ms = 0;
  rugbyProtectedTeam = 0;
  rugbyPossessionProtectionUntil_ms = 0;
  rugbyConversionAttempted = false;
  rugbyLastKickedByTeamID = -1;
  rugbyLastKickTime_ms = 0;
  rugbyLastDropGoalCheckTime_ms = 0;
  rugbyLastPassTime_ms = 0;
  rugbyLastPasser = 0;
  rugbyPasserPickupLockUntil_ms = 0;
  rugbyPendingPassReceiver = 0;
  rugbyPassInFlightUntil_ms = 0;

  possessionSideHistory.Clear();

  lastBodyBallCollisionTime_ms = 0;

  ball->ResetSituation(focusPos);
  teams[first_team]->ResetSituation(focusPos);
  teams[second_team]->ResetSituation(focusPos);
  officials->GetReferee()->ResetSituation(focusPos);

  if (GetScenarioConfig().initial_ball_owner_team >= 0 &&
      GetScenarioConfig().initial_ball_owner_team <= 1) {
    Team *ownerTeam = teams[GetScenarioConfig().initial_ball_owner_team];
    const int playerIndex = GetScenarioConfig().initial_ball_owner_player;
    const std::vector<Player *> &ownerPlayers = ownerTeam->GetAllPlayers();
    if (playerIndex >= 0 && playerIndex < (signed int)ownerPlayers.size()) {
      Player *owner = ownerPlayers[playerIndex];
      if (owner != nullptr && owner->IsActive()) {
        Vector3 carriedBallPos = owner->GetPosition();
        carriedBallPos.coords[2] = 0.11f;
        ball->SetPosition(carriedBallPos);
        ball->SetMomentum(Vector3(0));
        ownerTeam->UpdateDesignatedTeamPossessionPlayer();
        designatedPossessionPlayer = owner;
        SetBallRetainer(owner);
        ownerTeam->SetLastTouchPlayer(owner, e_TouchType_Intentional_Nonkicked);
      }
    }
  }
}

void Match::SetMatchPhase(e_MatchPhase newMatchPhase) {
  matchPhase = newMatchPhase;
  teams[first_team]->RelaxFatigue(1.0f);
  teams[second_team]->RelaxFatigue(1.0f);
}

Team *Match::GetBestPossessionTeam() {
  DO_VALIDATION;
  return bestPossessionTeam;
}

void Match::UpdateIngameCamera() {
  DO_VALIDATION;
  // camera

  float fov = 0.0f;
  float zoom = 0.0f;
  float height = 0.0f;

  fov = 0.5f + _default_CameraFOV * 0.5f;
  zoom = _default_CameraZoom;
  height = _default_CameraHeight * 1.5f;

  float playerBias = 0.6f;//0.7f;
  Vector3 ballPos = ball->Predict(0) * (1.0f - playerBias) + GetDesignatedPossessionPlayer()->GetPosition() * playerBias;
  // look in possession player's direction
  ballPos += GetDesignatedPossessionPlayer()->GetDirectionVec() * 1.0f;
  // look in possession team's attacking direction
  ballPos +=
      Vector3(((teams[first_team]->GetFadingTeamPossessionAmount() - 1.0f) *
                   -teams[first_team]->GetDynamicSide() +
               (teams[second_team]->GetFadingTeamPossessionAmount() - 1.0f) *
                   -teams[second_team]->GetDynamicSide()) *
                  4.0f,
              0, 0);

  ballPos.coords[2] *= 0.1f;

  float maxW = pitchHalfW * 0.84f * (1.0 / (zoom + 0.01f));// * (height * 0.75f + 0.25f);
  float maxH = pitchHalfH * 0.60f * (1.0 / (zoom + 0.01f)) * (height * 0.75f + 0.25f); // 0.52f
  if (fabs(ballPos.coords[0]) > maxW) ballPos.coords[0] = maxW * signSide(ballPos.coords[0]);
  if (fabs(ballPos.coords[1]) > maxH) ballPos.coords[1] = maxH * signSide(ballPos.coords[1]);

  Vector3 shudder =
      Vector3(boostrandom(-0.1f, 0.1f), boostrandom(-0.1f, 0.1f), 0) *
      (ball->GetMovement().GetLength() * 0.8f + 6.0f);
  shudder *= 0.2f;
  camPos.push_back(ballPos + shudder * ((float)camPos.size() / (float)camPosSize));
  if (camPos.size() > camPosSize) camPos.pop_front();

  Vector3 average;
  std::deque<Vector3>::iterator camIter = camPos.begin();
  float count = 0;
  float indexSize = camPos.size();
  int index = 0;
  while (camIter != camPos.end()) {
    DO_VALIDATION;
    float weight = std::sin((index / indexSize - 0.3f) * 1.4f * pi) * 0.5f +
                   0.5f;  // healthy mix of latest & middle | wa: sin((x / 100 -
                          // 0.3) * 1.4 * pi) * 0.5 + 0.5 | from x = 0 to 100
    weight *= std::pow(
        1.0f - index / indexSize,
        0.3f);  // sharp cutoff @ latest (because cameraperson can't 'foresee'
                // the current moment that fast) | wa: (1.0 - x / 100) ^ 0.3 *
                // (<prev formula>) | from x = 0 to 100
    average += (*camIter) * weight;
    count += weight;
    camIter++;
    index++;
  }

  average /= count;

  radian angleFac = 1.0f - _default_CameraAngleFactor * 0.4f; // 0.0 == 90 degrees max, 1.0 == sideline view

  // normal cam

  int camMethod = 1; // 1 == wide, 2 == birds-eye, 3 == tele

  if (!IsGoalScored() || (IsGoalScored() && goalScoredTimer < 1000)) {
    DO_VALIDATION;

    if (camMethod == 1) {
      DO_VALIDATION;

      // wide cam

      zoom = (0.6f + zoom * 1.0f) * (1.0f / fov);
      height = 4.0f + height * 10;

      float distRot = average.coords[1] / 800.0f;

      cameraOrientation.SetAngleAxis(distRot + (0.42f - height * 0.01f) * pi, Vector3(1, 0, 0));
      cameraNodeOrientation.SetAngleAxis((-average.coords[0] / pitchHalfW) * (1.0f - angleFac) * 0.25f * pi * 1.24f, Vector3(0, 0, 1));
      cameraNodePosition =
          average * Vector3(1.0f * (1.0f - _default_CameraAngleFactor * 0.2f) *
                                (1.0f - _default_CameraZoom * 0.3f),
                            0.9f - _default_CameraZoom * 0.3f, 0.2f) +
          Vector3(
              0,
              -41.4f - (_default_CameraFOV * 3.7f) + std::pow(height, 1.2f) * 0.46f,
              10.0f + height) *
              zoom;
      cameraFOV = (fov * 28.0f) - (cameraNodePosition.coords[1] / 30.0f);
      cameraNearCap = cameraNodePosition.coords[2];
      cameraFarCap = 200;

    } else if (camMethod == 2) {
      DO_VALIDATION;

      // birds-eye cam

      cameraOrientation = QUATERNION_IDENTITY;
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraNodePosition = average * Vector3(1, 1, 0) + Vector3(0, 0, 50 + zoom * 20.0);
      cameraFOV = 28;
      cameraNearCap = 40 + height - 5;
      cameraFarCap = 250;//65 + height * 1.2; doesn't work wtf?

    } else if (camMethod == 3) {
      DO_VALIDATION;

      // tele cam

      zoom = (0.6f + zoom * 1.0f) * (1.0f / fov);

      cameraOrientation.SetAngleAxis(0.3f * pi * height + 0.4f * pi * (1.0 - height), Vector3(1, 0, 0));
      cameraNodeOrientation = QUATERNION_IDENTITY;
      Vector3 offset = Vector3(0, -175.0f, 125.0f) * height + Vector3(0, -230.0f, 65.0f) * (1.0 - height);
      cameraNodePosition = average * Vector3(0.9f, 0.7f, 0.2f) + offset * zoom * 0.4f;
      cameraFOV = 15.0f;
      cameraNearCap = 50 + zoom * 10.0f;
      cameraFarCap = 300;
    }

  } else {
    // scorer cam

    Vector3 targetPos = ball->Predict(0).Get2D();
    if (lastGoalScorer) {
      DO_VALIDATION;
      targetPos = lastGoalScorer->GetPosition();
    }

    radian rot = (float)goalScoredTimer * 0.0005f;
    cameraOrientation.SetAngleAxis(0.45f * pi, Vector3(1, 0, 0));
    cameraNodeOrientation.SetAngleAxis(rot, Vector3(0, 0, 1));
    cameraNodePosition = targetPos + Vector3(0, -1, 0).GetRotated2D(rot) * 15.0f + Vector3(0, 0, 3);
    cameraFOV = 35.0f;

    cameraNearCap = 1;
    cameraFarCap = 220;
  }
}

void Match::ProcessState(EnvState* state) {
  if (state->getConfig()->reverse_team_processing) {
    std::swap(first_team, second_team);
  }
  state->process(first_team);
  state->process(second_team);
  if (state->getConfig()->reverse_team_processing) {
    std::swap(first_team, second_team);
  }
  bool team_0_mirror = teams[0]->isMirrored();
  bool team_1_mirror = teams[1]->isMirrored();
  bool ball_mirror =
      ball_mirrored ^ state->getConfig()->reverse_team_processing;
  Mirror(team_0_mirror, team_1_mirror, ball_mirror);
  std::vector<Player*> players;
  teams[first_team]->GetAllPlayers(players);
  teams[second_team]->GetAllPlayers(players);
  state->SetControllers(controllers);
  state->SetPlayers(players);
  state->SetAnimations(state->getContext()->anims->GetAnimations());
  state->SetTeams(teams[first_team], teams[second_team]);

  int size = mentalImages.size();
  state->process(size);
  mentalImages.resize(size);
  for (int x = 0; x < size; x++) {
    mentalImages[x].ProcessState(state, this);
  }
  teams[first_team]->ProcessState(state);
  teams[second_team]->ProcessState(state);
  std::vector<HumanGamer*> humanControllers;
  teams[first_team]->GetHumanControllers(humanControllers);
  teams[second_team]->GetHumanControllers(humanControllers);
  state->SetHumanControllers(humanControllers);
  for (auto &player : players) {
    player->ProcessState(state);
  }
  matchData->ProcessState(state, first_team);
  officials->ProcessState(state);
  {
    std::vector<HumanGamer*> human_gamers;
    std::set<AIControlledKeyboard*> visited;
    teams[first_team]->GetHumanControllers(human_gamers);
    teams[second_team]->GetHumanControllers(human_gamers);
    for (auto& c : human_gamers) {
      c->GetHIDevice()->ProcessState(state);
      visited.insert(c->GetHIDevice());
    }
    for (auto& c : controllers) {
      if (!visited.count(c)) {
        c->ProcessState(state);
      }
    }
  }
  ball->ProcessState(state);
  state->process(matchTime_ms);
  state->process(actualTime_ms);
  state->process(goalScoredTimer);
  state->process(matchPhase);
  state->process(inPlay);
  state->process(inSetPiece);
  state->process(goalScored);
  state->process(ballIsInGoal);
  state->process(lastGoalTeam);
  state->process(lastGoalScorer);
  if (first_team == 1) {
    for (int &v : lastTouchTeamIDs) {
      if (v != -1) {
        v = 1 - v;
      }
    }
  }
  for (int& v : lastTouchTeamIDs) {
    state->process(v);
  }
  if (first_team == 1) {
    for (int &v : lastTouchTeamIDs) {
      if (v != -1) {
        v = 1 - v;
      }
    }
  }
  if (first_team == 1 && lastTouchTeamID != -1) {
    lastTouchTeamID = 1 - lastTouchTeamID;
  }
  state->process(lastTouchTeamID);
  if (first_team == 1 && lastTouchTeamID != -1) {
    lastTouchTeamID = 1 - lastTouchTeamID;
  }
  state->process(bestPossessionTeam);
  state->process(designatedPossessionPlayer);
  state->process(ballRetainer);
  state->process(rugbyBreakdownActive);
  state->process(rugbyBreakdownStartTime_ms);
  state->process(rugbyBreakdownPos);
  state->process(rugbyBreakdownTeam);
  state->process(rugbyTackledPlayer);
  state->process(rugbyTackler);
  state->process(rugbyRecycleReceiver);
  state->process(rugbyRecycleStartTime_ms);
  state->process(rugbyProtectedTeam);
  state->process(rugbyPossessionProtectionUntil_ms);
  possessionSideHistory.ProcessState(state);
  state->process(autoUpdateIngameCamera);
  state->setValidate(false);
  state->process(cameraOrientation);
  state->process(cameraNodeOrientation);
  state->process(cameraNodePosition);
  state->process(cameraFOV);
  state->process(cameraNearCap);
  state->process(cameraFarCap);
  size = camPos.size();
  state->process(size);
  camPos.resize(size);
  for (auto& v : camPos) {
    state->process(v);
  }
  state->setValidate(true);
  state->process(lastBodyBallCollisionTime_ms);
  referee->ProcessState(state);

  resetNetting = true;
  nettingHasChanged = true;
  Mirror(team_0_mirror, team_1_mirror, ball_mirror);
}

void Match::GetTeamState(SharedInfo *state,
                         std::map<AIControlledKeyboard *, int> &controller_mapping,
                         int team_id) {
  DO_VALIDATION;
  std::vector<PlayerInfo> &team =
      team_id == 0 ? state->left_team : state->right_team;
  team.clear();
  std::vector<Player *> players;
  teams[team_id]->GetAllPlayers(players);
  auto main_player = teams[team_id]->MainSelectedPlayer();
  for (auto player : players) {
    DO_VALIDATION;
    auto controller = player->ExternalController();
    if (controller) {
      DO_VALIDATION;
      if (team_id == 0) {
        state->left_controllers[controller_mapping[controller->GetHIDevice()]]
            .controlled_player = team.size();
      } else {
        state->right_controllers[controller_mapping[controller->GetHIDevice()]]
            .controlled_player = team.size();
      }
    }
    if (player->CastHumanoid() != NULL) {
      DO_VALIDATION;
      auto position = player->GetPosition();
      auto movement = player->GetMovement();
      if (team_id == 1) {
        position.Mirror();
        movement.Mirror();
      }
      PlayerInfo info;
      info.player_position = position.coords;
      info.player_direction =
          (movement / GetGameConfig().physics_steps_per_frame).coords;
      info.tired_factor = 1 - player->GetFatigueFactorInv();
      info.has_card = player->HasCards();
      info.is_active = player->IsActive();
      info.role = player->GetFormationEntry().role;
      if (player->HasPossession() && GetLastTouchTeamID() != -1 &&
          GetLastTouchTeam()->GetLastTouchPlayer() == player) {
        DO_VALIDATION;
        state->ball_owned_player = team.size();
        state->ball_owned_team = GetLastTouchTeamID();
      }
      info.designated_player = player == main_player;
      team.push_back(info);
    }
  }
}

void Match::GetState(SharedInfo *state) {
  DO_VALIDATION;
  state->ball_position = ball->GetAveragePosition(5).coords;
  state->ball_rotation =
      (ball->GetRotation() / GetGameConfig().physics_steps_per_frame).coords;
  state->ball_direction =
      (ball->GetMovement() / GetGameConfig().physics_steps_per_frame).coords;
  state->ball_owned_player = -1;
  state->ball_owned_team = -1;
  state->rugby_breakdown_active = rugbyBreakdownActive;
  state->rugby_pending_initial_breakdown = rugbyPendingInitialBreakdown;
  state->rugby_force_initial_breakdown_config =
      GetScenarioConfig().rugby_force_initial_breakdown;
  state->rugby_breakdown_team =
      rugbyBreakdownTeam ? rugbyBreakdownTeam->GetID() : -1;
  state->rugby_breakdown_position = rugbyBreakdownPos.coords;
  state->rugby_recycle_receiver_team =
      rugbyRecycleReceiver ? rugbyRecycleReceiver->GetTeam()->GetID() : -1;
  state->rugby_recycle_receiver_position =
      rugbyRecycleReceiver ? rugbyRecycleReceiver->GetPosition().coords
                           : Vector3(0).coords;
  state->rugby_possession_protected_team =
      rugbyProtectedTeam ? rugbyProtectedTeam->GetID() : -1;
  state->rugby_offside_line = GetRugbyOffsideLine(first_team);
  state->rugby_ball_retainer_team =
      ballRetainer ? ballRetainer->GetTeam()->GetID() : -1;
  state->rugby_designated_possession_team =
      designatedPossessionPlayer ? designatedPossessionPlayer->GetTeam()->GetID()
                                 : -1;
  state->rugby_is_in_set_piece = IsInSetPiece();
  state->rugby_lineout_active =
      IsInSetPiece() && referee->GetBuffer().desiredSetPiece == e_GameMode_ThrowIn;
  state->rugby_lineout_team =
      state->rugby_lineout_active ? referee->GetBuffer().teamID : -1;
  state->rugby_lineout_winning_team =
      actualTime_ms <= rugbyLastLineoutResolveTime_ms + 1200
          ? rugbyLastLineoutWinningTeam
          : -1;
  state->rugby_scrum_active =
      IsInSetPiece() &&
      (referee->GetBuffer().desiredSetPiece == e_GameMode_GoalKick ||
       referee->GetBuffer().desiredSetPiece == e_GameMode_Corner);
  state->rugby_scrum_team =
      state->rugby_scrum_active ? referee->GetBuffer().teamID : -1;
  state->rugby_scrum_winning_team =
      actualTime_ms <= rugbyLastScrumResolveTime_ms + 1200
          ? rugbyLastScrumWinningTeam
          : -1;
  state->rugby_left_team_offside_line = GetRugbyOffsideLine(0);
  state->rugby_right_team_offside_line = GetRugbyOffsideLine(1);
  state->rugby_left_team_side = teams[0]->GetDynamicSide();
  state->rugby_right_team_side = teams[1]->GetDynamicSide();
  state->rugby_actual_time_ms = actualTime_ms;
  state->rugby_breakdown_start_time_ms = rugbyBreakdownStartTime_ms;
  state->left_goals = GetScore(0);
  state->right_goals = GetScore(1);
  // Report a step before game starts as in play, so that we know which players
  // are controlled by agents and which are controlled using action_builtin_ai.
  // 1900 = 2000 (game start) - 100 (single step time).
  state->is_in_play = IsInPlay() || GetActualTime_ms() == 1900;
  state->game_mode = GetGameMode();
  state->left_controllers.clear();
  state->left_controllers.resize(GetScenarioConfig().left_team.size());
  state->right_controllers.clear();
  state->right_controllers.resize(GetScenarioConfig().right_team.size());

  std::map<AIControlledKeyboard*, int> controller_mapping;
  {
    auto controllers = GetControllers();
    CHECK(controllers.size() == 2 * MAX_PLAYERS);
    for (int x = 0; x < MAX_PLAYERS; x++) {
      DO_VALIDATION;
      controller_mapping[controllers[x]] = x;
      controller_mapping[controllers[x + MAX_PLAYERS]] = x;
    }
  }
  GetTeamState(state, controller_mapping, first_team);
  GetTeamState(state, controller_mapping, second_team);
}

// THE SPICE

bool Match::Process() {
  DO_VALIDATION;
  bool reverse = GetScenarioConfig().reverse_team_processing;
  DO_VALIDATION;

  if (IsRugbyScenario() && rugbyPendingInitialTry && !goalScored &&
      !IsInSetPiece() && actualTime_ms <= 500) {
    Player *carrier = nullptr;
    if (GetScenarioConfig().initial_ball_owner_team >= 0 &&
        GetScenarioConfig().initial_ball_owner_team <= 1) {
      Team *ownerTeam = teams[GetScenarioConfig().initial_ball_owner_team];
      const int playerIndex = GetScenarioConfig().initial_ball_owner_player;
      const std::vector<Player *> &ownerPlayers = ownerTeam->GetAllPlayers();
      if (playerIndex >= 0 && playerIndex < (signed int)ownerPlayers.size()) {
        carrier = ownerPlayers[playerIndex];
      }
    }
    if (carrier == nullptr) {
      carrier = designatedPossessionPlayer;
    }
    if (carrier == nullptr && GetBestPossessionTeam() != nullptr) {
      carrier = GetBestPossessionTeam()->GetDesignatedTeamPossessionPlayer();
    }
    if (carrier != nullptr) {
      Team *scoringTeam = carrier->GetTeam();
      const int scoringTeamID = scoringTeam->GetID();
      matchData->SetGoalCount(scoringTeamID,
                              matchData->GetGoalCount(scoringTeamID) + 5);
      scoreboard->SetGoalCount(scoringTeamID,
                               matchData->GetGoalCount(scoringTeamID));
      goalScored = true;
      lastGoalTeam = scoringTeam;
      lastGoalScorer = carrier;
      scoringTeam->GetController()->UpdateTactics();
      Vector3 conversionMark = ball->Predict(0);
      conversionMark.coords[0] = scoringTeam->GetDynamicSide() * pitchHalfW;
      conversionMark.coords[2] = 0.0f;
      ResolveRugbyKickAtGoal(scoringTeam, conversionMark, 2, "Conversion");
      rugbyConversionAttempted = true;
      referee->StartRugbyKickoffRestart(
          scoringTeamID,
          "TRY for " + GetLastGoalTeam()->GetTeamData()->GetName() + "! " +
              lastGoalScorer->GetPlayerData()->GetLastName() +
              " grounds the ball!");
      rugbyPendingInitialTry = false;
    }
  }

  Mirror(reverse, !reverse, reverse);
  if (IsInPlay()) {
    DO_VALIDATION;
    CheckBallCollisions();
  }

  // HIJ IS EEN HONDELUUUL
  referee->Process();
  DO_VALIDATION;
  Vector3 previousBallPos = ball->Predict(0);
  Mirror(reverse, !reverse, reverse);
  if (!IsInPlay() && referee->GetBuffer().prepareTime + 10 < GetActualTime_ms()) {
    // Do not do simulation when game is on hold to save CPU.
    BumpActualTime_ms(10);
    return false;
  }
  Mirror(false, false, reverse);
  ball->Process();
  Mirror(false, false, reverse);

  // create mental images for the AI to use
  if (mentalImages.empty() || GetActualTime_ms() % 100 == 0) {
    DO_VALIDATION;
    mentalImages.insert(mentalImages.begin(), MentalImage(this));
    if (mentalImages.size() > 3) {
      DO_VALIDATION;
      mentalImages.pop_back();
    }
  }

  // obvious
  teams[first_team]->UpdateSwitch();
  teams[second_team]->UpdateSwitch();

  Mirror(first_team == 1, first_team == 0, first_team == 1);
  teams[first_team]->Process();
  Mirror(true, true, true);
  teams[second_team]->Process();
  Mirror(first_team == 0, first_team == 1, first_team == 0);

  Mirror(reverse, !reverse, reverse);
  officials->Process();
  Mirror(reverse, !reverse, reverse);

  Mirror(first_team == 1, first_team == 0, first_team == 1);
  teams[first_team]->UpdatePossessionStats();
  Mirror(true, true, true);
  teams[second_team]->UpdatePossessionStats();
  Mirror(first_team == 0, first_team == 1, first_team == 0);

  CalculateBestPossessionTeamID();

  if (GetBallRetainer() == 0) {
    DO_VALIDATION;
    if (GetBestPossessionTeam()) {
      DO_VALIDATION;
      Player *candidate = GetBestPossessionTeam()->GetDesignatedTeamPossessionPlayer();
      if (candidate != GetDesignatedPossessionPlayer()) {
        DO_VALIDATION;
        unsigned int designatedTime = GetDesignatedPossessionPlayer()->GetTimeNeededToGetToBall_ms();
        unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
        float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
        if (timeRating < 0.85f) designatedPossessionPlayer = candidate;
      }
    } else {
      // just stick with current team
      designatedPossessionPlayer = GetDesignatedPossessionPlayer()->GetTeam()->GetDesignatedTeamPossessionPlayer();
    }
  } else {
    designatedPossessionPlayer = GetBallRetainer();
  }

  Mirror(reverse, !reverse, reverse);
  CheckHumanoidCollisions();
  EnforceRugbyBreakdownOffside();
  const bool rugbyBootstrapWindow =
      rugbyPendingInitialBreakdown
          ? (actualTime_ms >= 1900 && actualTime_ms <= 2300)
          : (actualTime_ms <= 500);
  if (IsRugbyScenario() && !rugbyBreakdownActive && !goalScored &&
      rugbyBootstrapWindow) {
    Player *carrier = GetBallRetainer();
    if (carrier == nullptr && rugbyPendingInitialBreakdown) {
      carrier = designatedPossessionPlayer;
      if (GetBestPossessionTeam() != nullptr) {
        carrier = GetBestPossessionTeam()->GetDesignatedTeamPossessionPlayer();
      }
    }
    if (carrier != nullptr) {
      Team *carrier_team = carrier->GetTeam();
      Team *defending_team = carrier_team->Opponent();
      Player *nearest_defender = AI_GetClosestPlayer(
          defending_team, carrier->GetPosition(), false);
      const float bootstrapDistance =
          rugbyPendingInitialBreakdown ? 1.5f : 0.05f;
      if (nearest_defender != nullptr &&
          (nearest_defender->GetPosition() - carrier->GetPosition())
                  .GetLength() <
              bootstrapDistance) {
        RegisterRugbyTackle(carrier, nearest_defender,
                            rugbyPendingInitialBreakdown);
      }
    }
  }
  UpdateRugbyPhase();
  RunRugbyAI();

  BumpActualTime_ms(10);

  // check for goals
  bool first_team_goal = false;
  bool second_team_goal = false;
  bool rugby_score = false;
  int rugby_scoring_team = -1;
  bool rugby_drop_goal = false;
  int rugby_drop_goal_team = -1;
  if (IsInPlay()) {
    if (IsRugbyScenario()) {
      rugby_score = CheckForRugbyScore(rugby_scoring_team);
      if (!rugby_score) {
        rugby_drop_goal = CheckForRugbyDropGoal(rugby_drop_goal_team);
        if (rugby_drop_goal) {
          rugby_score = true;
          rugby_scoring_team = rugby_drop_goal_team;
        }
      }
    } else {
      first_team_goal =
          CheckForGoal(teams[first_team]->GetDynamicSide(), previousBallPos);
      second_team_goal =
          CheckForGoal(teams[second_team]->GetDynamicSide(), previousBallPos);
    }
  }
  bool goal = first_team_goal | second_team_goal | rugby_score;
  ballIsInGoal |= goal;
  Mirror(reverse, !reverse, reverse);
  if (IsInPlay()) {
    DO_VALIDATION;
    if (goal) {
      int team = rugby_score
                     ? rugby_scoring_team
                     : (first_team_goal ? second_team : first_team);
      DO_VALIDATION;
      const int awardedPoints =
          rugby_drop_goal ? 3 : (rugby_score ? 5 : 1);
      matchData->SetGoalCount(teams[team]->GetID(),
                              matchData->GetGoalCount(team) + awardedPoints);
      scoreboard->SetGoalCount(team, matchData->GetGoalCount(team));
      goalScored = true;
      lastGoalTeam = teams[team];
      teams[team]->GetController()->UpdateTactics();
    }
    if (rugby_drop_goal) {
      lastGoalScorer = 0;
      SpamMessage("DROP GOAL for " +
                      GetLastGoalTeam()->GetTeamData()->GetName() + "! +3",
                  4000);
    } else if (rugby_score) {
      lastGoalScorer = GetBallRetainer();
      if (lastGoalScorer) {
        SpamMessage("TRY for " + GetLastGoalTeam()->GetTeamData()->GetName() +
                        "! " +
                        lastGoalScorer->GetPlayerData()->GetLastName() +
                        " grounds the ball!",
                    4000);
      } else {
        SpamMessage("TRY!", 4000);
      }
      if (!rugbyConversionAttempted) {
        Vector3 conversionMark = ball->Predict(0);
        conversionMark.coords[0] =
            teams[rugby_scoring_team]->GetDynamicSide() * pitchHalfW;
        conversionMark.coords[2] = 0.0f;
        ResolveRugbyKickAtGoal(teams[rugby_scoring_team], conversionMark, 2,
                               "Conversion");
        rugbyConversionAttempted = true;
      }
    } else if (first_team_goal || second_team_goal) {
      DO_VALIDATION;

      // find out who scored
      bool ownGoal = true;
      if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == GetLastGoalTeam()->GetID() || GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == GetLastGoalTeam()->GetID()) ownGoal = false;

      if (!ownGoal) {
        DO_VALIDATION;
        lastGoalScorer = GetLastGoalTeam()->GetLastTouchPlayer();
        if (lastGoalScorer) {
          DO_VALIDATION;
          SpamMessage("GOAL for " + GetLastGoalTeam()->GetTeamData()->GetName() + "! " + lastGoalScorer->GetPlayerData()->GetLastName() + " scores!", 4000);
        } else {
          SpamMessage("GOAL!!!", 4000);
        }
      }

      else {  // own goal
        lastGoalScorer = teams[abs(GetLastGoalTeam()->GetID() - 1)]->GetLastTouchPlayer();
        if (lastGoalScorer) {
          DO_VALIDATION;
          SpamMessage("OWN GOAL! " + lastGoalScorer->GetPlayerData()->GetLastName() + " is so unlucky!", 4000);
        } else {
          SpamMessage("It's an OWN GOAL! oh noes!", 4000);
        }
      }
    }
  }
  // average possession side

   if (IsInPlay()) {
     DO_VALIDATION;
     if (GetBestPossessionTeam()) {
       DO_VALIDATION;
       float sideValue = 0;
       sideValue += (GetTeam(0)->GetFadingTeamPossessionAmount() - 0.5f) *
           GetTeam(0)->GetDynamicSide();
       sideValue += (GetTeam(1)->GetFadingTeamPossessionAmount() - 0.5f) *
           GetTeam(1)->GetDynamicSide();
       possessionSideHistory.Insert(sideValue);
     }
   }

   if (GetReferee()->GetBuffer().active == true &&
       (GetReferee()->GetCurrentFoulType() == 2 ||
           GetReferee()->GetCurrentFoulType() == 3) &&
           GetReferee()->GetBuffer().stopTime < GetActualTime_ms() - 1000) {
     DO_VALIDATION;

     if (GetReferee()->GetBuffer().prepareTime > GetActualTime_ms()) {
       DO_VALIDATION;  // FOUL, film referee
       SetAutoUpdateIngameCamera(false);
       Vector3 referee_pos = officials->GetReferee()->GetPosition();
       if (reverse) {
         referee_pos.Mirror();
       }
       FollowCamera(cameraOrientation, cameraNodeOrientation,
                    cameraNodePosition, cameraFOV,
                    referee_pos + Vector3(0, 0, 0.8f), 1.5f);
       cameraNearCap = 1;
       cameraFarCap = 220;
       if (officials->GetReferee()->GetCurrentFunctionType() == e_FunctionType_Special) referee->AlterSetPiecePrepareTime(GetActualTime_ms() + 1000);
     } else {  // back to normal
       SetAutoUpdateIngameCamera(true);
     }
  }
  return true;
}

void Match::UpdateCamera() {
  if (autoUpdateIngameCamera) {
    DO_VALIDATION;
    Mirror(false, true, false);
    UpdateIngameCamera();
    Mirror(false, true, false);
  }

  DO_VALIDATION;
  unsigned int zoomTime = 2000;
  unsigned int startTime = 0;
  if (actualTime_ms < zoomTime + startTime) {
    DO_VALIDATION;  // nice effect at the start

    Quaternion initialOrientation = QUATERNION_IDENTITY;
    initialOrientation.SetAngleAxis(0.0f * pi, Vector3(1, 0, 0));
    Quaternion zOrientation = QUATERNION_IDENTITY;
    initialOrientation = zOrientation * initialOrientation;

    Vector3 initialPosition = Vector3(0.0f, 0.0f, 60.0);

    int subTime = clamp(actualTime_ms - startTime, 0, zoomTime);
    float bias = subTime / (float)(zoomTime);
    bias *= pi;
    bias = std::sin(bias - 0.5f * pi) * -0.5f + 0.5f;

    cameraOrientation = cameraOrientation.GetSlerped(bias, QUATERNION_IDENTITY);
    cameraNodeOrientation = cameraNodeOrientation.GetSlerped(bias, initialOrientation);
    cameraNodePosition = cameraNodePosition * (1.0f - bias) + initialPosition * bias;
    cameraFOV = cameraFOV * (1.0f - bias) + 40 * bias;
    cameraNearCap = cameraNearCap * (1.0f - bias) + 2.0f * bias;
  }
}

void Match::PreparePutBuffers() {
  DO_VALIDATION;
  Mirror(false, false, first_team == 1);
  teams[first_team]->PreparePutBuffers();
  Mirror(false, false, true);
  teams[second_team]->PreparePutBuffers();
  Mirror(false, false, first_team == 0);
}

void Match::FetchPutBuffers() {
  DO_VALIDATION;
  DO_VALIDATION;
  teams[first_team]->FetchPutBuffers();
  teams[second_team]->FetchPutBuffers();
  officials->FetchPutBuffers();
}

void Match::Put() {
  DO_VALIDATION;
  bool reverse = GetScenarioConfig().reverse_team_processing;

  DO_VALIDATION;
  ball->Put();
  teams[first_team]->Put(reverse);
  teams[second_team]->Put(!reverse);
  officials->Put(reverse);

  camera->SetPosition(Vector3(0, 0, 0), false);
  camera->SetRotation(cameraOrientation, false);
  cameraNode->SetPosition(cameraNodePosition, false);
  cameraNode->SetRotation(cameraNodeOrientation, false);
  camera->SetFOV(cameraFOV);
  camera->SetCapping(cameraNearCap, cameraFarCap);

  GetDynamicNode()->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  DO_VALIDATION;
  teams[first_team]->Put2D(reverse);
  teams[second_team]->Put2D(!reverse);

  // if (buf_actualTime_ms % 100 == 0) { DO_VALIDATION; // a better way would
  // be to count iterations (this modulo is irregular since not all process
  // runs are put)
  // clock

  int seconds = (int)(matchTime_ms / 1000.0) % 60;
  int minutes = (int)(matchTime_ms / 60000.0);

  std::string timeStr = "";
  if (minutes < 10) timeStr += "0";
  timeStr += int_to_str(minutes);
  timeStr += ":";
  if (seconds < 10) timeStr += "0";
  timeStr += int_to_str(seconds);
  scoreboard->SetTimeStr(timeStr);
  if (messageCaptionRemoveTime_ms <= actualTime_ms) messageCaption->Hide();
  radar->Put();
  UpdateGoalNetting(GetBall()->BallTouchesNet());
}

boost::intrusive_ptr<Node> Match::GetDynamicNode() {
  DO_VALIDATION;
  return dynamicNode;
}

bool Match::CheckForGoal(signed int side, const Vector3 &previousBallPos) {
  DO_VALIDATION;
  if (fabs(ball->Predict(10).coords[0]) < pitchHalfW - 1.0) return false;

  Line line;
  line.SetVertex(0, previousBallPos);
  line.SetVertex(1, ball->Predict(0));

  Triangle goal1;
  goal1.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 0));
  goal1.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal1.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal1.SetNormals(Vector3(-side, 0, 0));
  Triangle goal2;
  goal2.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal2.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 2.5f));
  goal2.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal2.SetNormals(Vector3(-side, 0, 0));

  Vector3 intersectVec;
  GetTracker()->setDisabled(true);
  bool intersect1 = goal1.IntersectsLine(line, intersectVec);
  bool intersect2 = goal2.IntersectsLine(line, intersectVec);
  GetTracker()->setDisabled(false);
  // extra check: ball could have gone 'in' via the side netting, if line begin
  // == inside pitch, but outside of post, and line end == in goal. disallow!
  if (fabs(previousBallPos.coords[1]) > 3.7 &&
      fabs(previousBallPos.coords[0]) > pitchHalfW - lineHalfW - 0.11) {
    DO_VALIDATION;
    return false;
  }
  if (intersect1 || intersect2) {
    DO_VALIDATION;
    return true;
  }
  return false;
}

void Match::CalculateBestPossessionTeamID() {
  DO_VALIDATION;
  if (GetBallRetainer() != 0) {
    DO_VALIDATION;
    bestPossessionTeam = GetBallRetainer()->GetTeam();
  } else {
    int bestTime_ms[2] = { 100000, 100000 };
    bestTime_ms[0] = teams[first_team]->GetTimeNeededToGetToBall_ms();
    bestTime_ms[1] = teams[second_team]->GetTimeNeededToGetToBall_ms();
    if (bestTime_ms[0] < bestTime_ms[1])
      bestPossessionTeam = teams[first_team];
    else if (bestTime_ms[0] > bestTime_ms[1])
      bestPossessionTeam = teams[second_team];
    else {
      assert(bestTime_ms[0] == bestTime_ms[1]);
      bestPossessionTeam = 0;
    }
  }
}

void Match::CheckHumanoidCollisions() {
  DO_VALIDATION;
  // During rugby set pieces (scrum / lineout / kickoff) the pack is
  // deliberately bound together — the bounce-apart physics that works for
  // open play wrecks the formation, leaving a loose puddle of players
  // instead of a tight pack. Skip the collision pass in that window.
  if (IsRugbyScenario() && IsInSetPiece()) return;

  std::vector<Player*> players;

  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);

  // outer vectors index == players[] index
  std::vector < std::vector<PlayerBounce> > playerBounces;

  // insert an empty entry for every player
  playerBounces.resize(players.size());

  // check each combination of humanoids once
  for (unsigned int i1 = 0; i1 < players.size() - 1; i1++) {
    DO_VALIDATION;
    for (unsigned int i2 = i1 + 1; i2 < players.size(); i2++) {
      DO_VALIDATION;
      CheckHumanoidCollision(players.at(i1), players.at(i2), playerBounces.at(i1), playerBounces.at(i2));
    }
  }

  // do bouncy magic
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {
    DO_VALIDATION;

    float totalForce = 0.0f;

    for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
      DO_VALIDATION;

      const PlayerBounce &bounce = playerBounces.at(i1).at(i2);
      totalForce += bounce.force;
    }

    if (totalForce > 0.0f) {
      DO_VALIDATION;

      Vector3 bounceVec;
      for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
        DO_VALIDATION;

        const PlayerBounce &bounce = playerBounces.at(i1).at(i2);
        bounceVec += (bounce.opp->GetMovement() - players.at(i1)->GetMovement()) * bounce.force * (bounce.force / totalForce);
      }

      // okay, accumulated all, now distribute them in normalized fashion
      players.at(i1)->OffsetPosition(bounceVec * 0.01f * 1.0f);
    }
  }
}

void Match::CheckHumanoidCollision(Player *p1, Player *p2,
                                   std::vector<PlayerBounce> &p1Bounce,
                                   std::vector<PlayerBounce> &p2Bounce) {
  DO_VALIDATION;
  constexpr float distanceFactor = 0.72f;
  constexpr float bouncePlayerRadius = 0.5f * distanceFactor;
  constexpr float similarPlayerRadius = 0.8f * distanceFactor;
  constexpr float similarExp = 0.2f;//0.8f;
  constexpr float similarForceFactor = 0.25f; // 0.5f would be the full effect

  Vector3 p1pos = p1->GetPosition();
  Vector3 p2pos = p2->GetPosition();

  float distance = (p1pos - p2pos).GetLength();

  Vector3 p1movement = p1->GetMovement();
  Vector3 p2movement = p2->GetMovement();
  assert(p1movement.coords[2] == 0.0f);
  assert(p2movement.coords[2] == 0.0f);

  float bounceBias = 0.0f;
  Vector3 bounceVec;
  float p1backFacing = 0.5f;
  float p2backFacing = 0.5f;

  if (distance < bouncePlayerRadius * 2.0f ||
      distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
    DO_VALIDATION;

    bounceVec = (p1pos - p2pos).GetNormalized(Vector3(0, -1, 0));

    // back facing
    Vector3 p1facing = p1->GetDirectionVec().GetRotated2D(p1->GetRelBodyAngle() * 0.7f);
    Vector3 p2facing = p2->GetDirectionVec().GetRotated2D(p2->GetRelBodyAngle() * 0.7f);
    p1backFacing = clamp(p1facing.GetDotProduct( bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f); // 0 .. 1 == worst .. best
    p2backFacing = clamp(p2facing.GetDotProduct(-bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);

    if (distance < bouncePlayerRadius * 2.0f) {
      DO_VALIDATION;

      bounceBias += p1backFacing * 0.8f;
      bounceBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      bounceBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1->TouchPending() && p1->GetCurrentFunctionType() == e_FunctionType_Interfere) bounceBias += 0.1f + 0.4f * p1->GetStat(technical_standingtackle);
      if (p1->TouchPending() && p1->GetCurrentFunctionType() == e_FunctionType_Sliding)   bounceBias += 0.1f + 0.4f * p1->GetStat(technical_slidingtackle);
      if (p2->TouchPending() && p2->GetCurrentFunctionType() == e_FunctionType_Interfere) bounceBias -= 0.1f + 0.4f * p2->GetStat(technical_standingtackle);
      if (p2->TouchPending() && p2->GetCurrentFunctionType() == e_FunctionType_Sliding)   bounceBias -= 0.1f + 0.4f * p2->GetStat(technical_slidingtackle);

      // problem is, once possession is lost (usually directly after ball is touched), bias may turn around the other way. (well, maybe that's not a problem. dunno.)
      // if (p1->HasPossession() == true) bounceBias -= 0.3f;
      // if (p2->HasPossession() == true) bounceBias += 0.3f;

      if (p1 == GetDesignatedPossessionPlayer()) bounceBias += 0.4f;
      if (p2 == GetDesignatedPossessionPlayer()) bounceBias -= 0.4f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        DO_VALIDATION;
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor = clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) * 1.0f; // std::min is cap so difference won't matter if ball is far away (so only used in battles about the ball)
        bounceBias += ballDistanceDiffFactor;
      }

      bounceBias += p1->GetStat(physical_balance) * 1.0f;
      bounceBias -= p2->GetStat(physical_balance) * 1.0f;

      bounceBias = clamp(bounceBias, -1.0f, 1.0f);
      bounceBias *= 0.5f;

      // convert bounceBias to 0 .. 1 instead of -1 .. 1
      float bounceBias0to1 = bounceBias * 0.5f + 0.5f;
      //bounceBias0to1 = curve(bounceBias0to1, 0.5f); // more binary

      Vector3 offset1 = (p1pos - p2pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) * (1.0f - bounceBias0to1) * 2.0f;
      Vector3 offset2 = (p2pos - p1pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) * bounceBias0to1 * 2.0f;

      // slow down on contact
      /*
      Vector3 averageMomentum = (p1movement + p2movement) * 0.5f;
      offset1 -= averageMomentum * 0.001f;
      offset2 -= averageMomentum * 0.001f;
      */

      // make players snap to the side of opponents (rather, just a bit in front of them too)


      if (GetDesignatedPossessionPlayer() == p2 && p2->HasPossession()) {
        DO_VALIDATION;
        Vector3 p2_leftside = p2pos + p2->GetDirectionVec().GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p2_rightside = p2pos + p2->GetDirectionVec().GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p1_to_p2_left = (p1pos - p2_leftside).GetLength();
        float p1_to_p2_right = (p1pos - p2_rightside).GetLength();
        Vector3 p2side = p1_to_p2_left < p1_to_p2_right ? p2_leftside : p2_rightside;
        // SetYellowDebugPilon(p2side);
        offset1 += (p2side - p1pos).GetNormalizedMax(0.01f) * p1->GetStat(physical_balance) * 0.3f;
      }

      else if (GetDesignatedPossessionPlayer() == p1 && p1->HasPossession()) {
        DO_VALIDATION;
        Vector3 p1_leftside = p1pos + p1->GetDirectionVec().GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p1_rightside = p1pos + p1->GetDirectionVec().GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p2_to_p1_left = (p2pos - p1_leftside).GetLength();
        float p2_to_p1_right = (p2pos - p1_rightside).GetLength();
        Vector3 p1side = p2_to_p1_left < p2_to_p1_right ? p1_leftside : p1_rightside;
        // SetRedDebugPilon(p1side);
        offset2 += (p1side - p2pos).GetNormalizedMax(0.01f) * p2->GetStat(physical_balance) * 0.3f;
      }

      // can not bump faster than sprint
      offset1.NormalizeMax(sprintVelocity * 0.01f);
      offset2.NormalizeMax(sprintVelocity * 0.01f);


      p1->OffsetPosition(offset1);
      p2->OffsetPosition(offset2);
    }

    // take over each others movement a bit (precalc phase)

    float similarBias = 0.0f;

    if (similarForceFactor > 0.0f &&
        distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
      DO_VALIDATION;
      float shellDistance = std::max(0.0f, distance - bouncePlayerRadius * 2.0f);

      similarBias += p1backFacing * 0.8f;
      similarBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      similarBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1 == GetDesignatedPossessionPlayer()) similarBias += 0.6f;
      if (p2 == GetDesignatedPossessionPlayer()) similarBias -= 0.6f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        DO_VALIDATION;
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor = clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) * 1.0f; // std::min is cap so difference won't matter if ball is far away (so only used in battles about the ball)
        similarBias += ballDistanceDiffFactor;
      }

      similarBias += p1->GetStat(physical_balance) * 1.0f;
      similarBias -= p2->GetStat(physical_balance) * 1.0f;

      similarBias = clamp(similarBias, -1.0f, 1.0f);
      similarBias *= 0.9f;

      float similarForce = clamp(1.0f - (shellDistance / (similarPlayerRadius * 2.0f)), 0.0f, 1.0f);
      similarForce = std::pow(similarForce, similarExp);
      similarForce *= similarForceFactor;

      assert(similarForce >= 0.0f && similarForce <= 1.0f);

      float similarBias0to1 = similarBias * 0.5f + 0.5f;


      PlayerBounce player1Bounce;
      player1Bounce.opp = p2;
      player1Bounce.force = similarForce * (1.0f - similarBias0to1);
      p1Bounce.push_back(player1Bounce);

      PlayerBounce player2Bounce;
      player2Bounce.opp = p1;
      player2Bounce.force = similarForce * similarBias0to1;
      p2Bounce.push_back(player2Bounce);
    }

    // u b trippin?

    if (distance < bouncePlayerRadius * 2.0f) {
      DO_VALIDATION;

      float p1sensitivity = 0.0f;
      float p2sensitivity = 0.0f;

      p1sensitivity += (1.0f - p1backFacing) * 1.0f;
      p2sensitivity += (1.0f - p2backFacing) * 1.0f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      p1sensitivity += NormalizedClamp(p1velocity, idleVelocity, sprintVelocity) * 1.0f;
      p2sensitivity += NormalizedClamp(p2velocity, idleVelocity, sprintVelocity) * 1.0f;

      if (p1->HasBestPossession() == true) p1sensitivity += 1.0f;
      if (p2->HasBestPossession() == true) p2sensitivity += 1.0f;

      float balanceWeight = 3.0f;
      p1sensitivity += (1.0f - p1->GetStat(physical_balance) * 1.0f) * balanceWeight;
      p2sensitivity += (1.0f - p2->GetStat(physical_balance) * 1.0f) * balanceWeight;

      p1sensitivity += clamp(p1->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);
      p2sensitivity += clamp(p2->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);

      // penetration
      float penetrationWeight = 6.0f;
      float penetration = ( (p1->GetPosition() + p1->GetMovement() * 0.03f) - (p2->GetPosition() + p2->GetMovement() * 0.03f) ).GetLength();
      //if (p1->GetDebug() || p2->GetDebug()) printf("penetration: %f\n", pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f));
      p1sensitivity +=
          std::pow(1.0f - NormalizedClamp(penetration, 0.0f,
                                          bouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;
      p2sensitivity +=
          std::pow(1.0f - NormalizedClamp(penetration, 0.0f,
                                          bouncePlayerRadius * 2.0f),
                   0.4f) *
          penetrationWeight;

      // ball proximity (usually means: stability is less because we sacrifice balance to control the ball)
      float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
      float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
      p1sensitivity += 1.0f - NormalizedClamp(p1BallDistance, 0.0f, 0.7f);
      p2sensitivity += 1.0f - NormalizedClamp(p2BallDistance, 0.0f, 0.7f);

      // divided by elements active
      p1sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      p2sensitivity /= 5.0f + balanceWeight + penetrationWeight;

      float trip0threshold = 0.38f;
      float trip1threshold = 0.48f;
      float trip2threshold = 0.58f;

      if (p1sensitivity > trip0threshold) {
        DO_VALIDATION;
        int tripType = 0;
        if (p1sensitivity > trip1threshold) tripType = 1;
        if (p1sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          DO_VALIDATION;
          p1->TripMe((p1->GetMovement() * 0.1f + p2->GetMovement() * 0.06f + bounceVec * 1.0f).GetNormalized(bounceVec), tripType);
          referee->TripNotice(p1, p2, tripType);
        }
      }
      if (p2sensitivity > trip0threshold) {
        DO_VALIDATION;
        int tripType = 0;
        if (p2sensitivity > trip1threshold) tripType = 1;
        if (p2sensitivity > trip2threshold) tripType = 2;
        if (tripType > 0) {
          DO_VALIDATION;
          p2->TripMe((p2->GetMovement() * 0.1f + p1->GetMovement() * 0.06f - bounceVec * 1.0f).GetNormalized(-bounceVec), tripType);
          referee->TripNotice(p2, p1, tripType);
        }
      }

    }  // within either bump, similar or trip range
  }

  // check for tackling collisions

  int tackle = 0;
  if ((p1->GetCurrentFunctionType() == e_FunctionType_Sliding || p1->GetCurrentFunctionType() == e_FunctionType_Interfere) && p1->GetFrameNum() > 5 && p1->GetFrameNum() < 28) tackle += 1;
  if ((p2->GetCurrentFunctionType() == e_FunctionType_Sliding || p2->GetCurrentFunctionType() == e_FunctionType_Interfere) && p2->GetFrameNum() > 5 && p2->GetFrameNum() < 28) tackle += 2;
  if (distance < 2.0f && tackle > 0 && tackle < 3) {
    DO_VALIDATION;  // if tackle is 3, ignore both
    std::list < boost::intrusive_ptr<Geometry> > tacklerObjectList;
    std::list < boost::intrusive_ptr<Geometry> > victimObjectList;
    /*
    if (tackle == 0) { DO_VALIDATION;
    advantage if (p1->GetCurrentFunctionType() == e_FunctionType_Trap ||
          p1->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_LongPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_HighPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_Shot ||
          p1->GetCurrentFunctionType() == e_FunctionType_Interfere) {
    DO_VALIDATION; p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry,
    tacklerObjectList); p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry,
    victimObjectList); p1action = true;
      }
      else if (p2->GetCurrentFunctionType() == e_FunctionType_Trap ||
               p2->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_LongPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_HighPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_Shot ||
               p2->GetCurrentFunctionType() == e_FunctionType_Interfere) {
    DO_VALIDATION; p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry,
    tacklerObjectList); p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry,
    victimObjectList); p2action = true;
      }
    }
    */
    if (tackle == 1) {
      DO_VALIDATION;
      p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
      p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
    }
    if (tackle == 2) {
      DO_VALIDATION;
      p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
      p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
    }

    // iterate through all body parts of tackler
    std::list < boost::intrusive_ptr<Geometry> >::iterator objIter = tacklerObjectList.begin();
    while (objIter != tacklerObjectList.end()) {
      DO_VALIDATION;

      AABB objAABB = (*objIter)->GetAABB();

      // make a tad smaller: AABBs are usually too large.
      objAABB.minxyz += 0.1f;
      objAABB.maxxyz -= 0.1f;

      std::list < boost::intrusive_ptr<Geometry> >::iterator victimIter = victimObjectList.begin();
      while (victimIter != victimObjectList.end()) {
        DO_VALIDATION;

        std::string bodyPartName = (*victimIter)->GetName();
        if (bodyPartName.compare("left_foot") == 0 || bodyPartName.compare("right_foot") == 0 ||
            bodyPartName.compare("left_lowerleg") == 0 || bodyPartName.compare("right_lowerleg") == 0
            /*bodyPartName == "left_upperleg" || bodyPartName == "right_upperleg"*/) {
          DO_VALIDATION;
          if (objAABB.Intersects((*victimIter)->GetAABB())) {
            DO_VALIDATION;
            //printf("HIT: %s hits %s\n", (*objIter)->GetName().c_str(), (*victimIter)->GetName().c_str());

            if (tackle == 1) {
              DO_VALIDATION;
              if (p1->GetFrameNum() > 10 &&
                  p1->GetFrameNum() < p1->GetFrameCount() - 6) {
                DO_VALIDATION;
                Vector3 tripVec = p2->GetDirectionVec();
                int tripType = 3; // sliding
                if (p1->GetCurrentFunctionType() == e_FunctionType_Interfere) tripType = 1; // was 2
                p2->TripMe(tripVec, tripType);
                referee->TripNotice(p2, p1, tripType);
              }
            }
            if (tackle == 2) {
              DO_VALIDATION;
              if (p2->GetFrameNum() > 10 &&
                  p2->GetFrameNum() < p2->GetFrameCount() - 6) {
                DO_VALIDATION;
                Vector3 tripVec = p1->GetDirectionVec();
                int tripType = 3; // sliding
                if (p2->GetCurrentFunctionType() == e_FunctionType_Interfere) tripType = 1; // was 2
                p1->TripMe(tripVec, tripType);
                referee->TripNotice(p1, p2, tripType);
              }
            }
            break;
          }
        }

        victimIter++;
      }

      objIter++;
    }
  }
}

void Match::CheckBallCollisions() {
  DO_VALIDATION;



  //printf("%i - %i hihi\n", actualTime_ms, lastBodyBallCollisionTime_ms + 150);
  if (actualTime_ms <= lastBodyBallCollisionTime_ms + 150) return;

  std::vector<Player*> players;
  GetTeam(first_team)->GetActivePlayers(players);
  GetTeam(second_team)->GetActivePlayers(players);

  // Rugby auto-pickup: a loose ball on the pitch snaps to the closest player
  // within 0.7m when there is no current retainer, the ball is low enough to
  // be reached, and the protection window for the opposite team is over.
  // Rugby is a hands game — players shouldn't be kicking balls at their
  // feet like a football.
  if (IsRugbyScenario() && GetBallRetainer() == nullptr &&
      IsInPlay() && !IsInSetPiece() && !goalScored) {
    const Vector3 ballPos = ball->Predict(0);
    if (ballPos.coords[2] < 1.6f) {
      Player *closest = nullptr;
      float closestDistance = 1.2f;
      // While a rugby pass is in flight only the intended receiver is
      // allowed to auto-pickup. This prevents a running teammate ahead of
      // the thrower from snatching the ball, which the referee would
      // otherwise flag as a forward pass.
      const bool passInFlight =
          rugbyPendingPassReceiver != nullptr &&
          actualTime_ms < rugbyPassInFlightUntil_ms;
      for (Player *p : players) {
        if (p == nullptr || !p->IsActive()) continue;
        if (IsRugbyRetainBlockedFor(p->GetTeam())) continue;
        // Don't let the passer re-absorb their own pass before it has time
        // to reach the target.
        if (p == rugbyLastPasser &&
            actualTime_ms < rugbyPasserPickupLockUntil_ms) {
          continue;
        }
        if (passInFlight && p != rugbyPendingPassReceiver) continue;
        // Ground-plane distance: the ball can be anywhere in the player's
        // reach envelope (ankle to chest). We compare only X/Y.
        const Vector3 delta2D =
            Vector3(p->GetPosition().coords[0] - ballPos.coords[0],
                    p->GetPosition().coords[1] - ballPos.coords[1], 0.0f);
        const float distance = delta2D.GetLength();
        if (distance < closestDistance) {
          closestDistance = distance;
          closest = p;
        }
      }
      if (closest != nullptr) {
        SetBallRetainer(closest);
        ball->Touch(Vector3(0));
        ball->SetMomentum(Vector3(0));
        SetLastTouchTeamID(closest->GetTeam()->GetID(),
                           e_TouchType_Intentional_Nonkicked);
        if (closest == rugbyPendingPassReceiver) {
          rugbyPendingPassReceiver = 0;
          rugbyPassInFlightUntil_ms = 0;
        }
      }
    }
  }

  Vector3 bounceVec;
  float bias = 0.0;
  int bounceCount = 0; // this shit is shit, average properly in combination with bias or something like that

  //printf("lasttouchbias: %f, isnul?: %s\n", GetLastTouchBias(200), GetLastTouchBias(200) == 0.0f ? "true" : "false");
  for (int i = 0; i < (signed int)players.size(); i++) {
    DO_VALIDATION;

    bool biggestRatio = false;
    int teamID = players[i]->GetTeam()->GetID();

    int touchTimeThreshold_ms = 200;//700;
    float oppLastTouchBias = GetTeam(abs(teamID - 1))->GetLastTouchBias(touchTimeThreshold_ms);
    float lastTouchBias = players[i]->GetLastTouchBias(touchTimeThreshold_ms);
    float oppLastTouchBiasLong = GetTeam(abs(teamID - 1))->GetLastTouchBias(1600);

    if (lastTouchBias <= 0.01f &&
        oppLastTouchBias > 0.01f /* && ballTowardsPlayer*/) {
      DO_VALIDATION;  // cannot collide if opp didn't recently touch ball (we
                      // would be able to predict ball by then), or if player
                      // itself already did (to overcome the 'perpetuum
                      // collision' problem, and to allow for 'controlled ball
                      // collisions' in humanoid class)

      bool collisionAnim = false;
      if (players[i]->GetCurrentFunctionType() == e_FunctionType_Movement || players[i]->GetCurrentFunctionType() == e_FunctionType_Trip || players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding || players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere || players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) collisionAnim = true;
      bool onlyWhenDirectionChangedUnexpectedly = false;
      if (players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere || players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) onlyWhenDirectionChangedUnexpectedly = true;

      bool directionChangedUnexpectedly = false;
      if (onlyWhenDirectionChangedUnexpectedly) {
        DO_VALIDATION;
        float unexpectedDistance = (GetMentalImage(players[i]->GetController()->GetReactionTime_ms() + players[i]->GetFrameNum() * 10)->GetBallPrediction(1000) - GetBall()->Predict(1000)).GetLength(); // mental image from when the anim began
        if (unexpectedDistance > 0.5f) directionChangedUnexpectedly = true;
      }


      if (collisionAnim && !players[i]->HasUniquePossession() &&
          (onlyWhenDirectionChangedUnexpectedly ==
           directionChangedUnexpectedly)) {
        DO_VALIDATION;

        float boundingBoxSizeOffset = -0.1f; // fake a big AABB for more blocking fun, or a small one for less bouncy bounce
        if (!players[i]->HasPossession()) boundingBoxSizeOffset += 0.03f; else
                                          boundingBoxSizeOffset -= 0.03f;

        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding ||
            players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere) {
          DO_VALIDATION;
          boundingBoxSizeOffset += 0.1f;
        }
        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) {
          DO_VALIDATION;
          boundingBoxSizeOffset += 0.2f;
        }

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) -
             ball->Predict(0))
                .GetLength() < 2.5f) {
          DO_VALIDATION;  // premature optimization is the root of all evil :D
          std::list < boost::intrusive_ptr<Geometry> > objectList;
          players[i]->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, objectList);
          std::list < boost::intrusive_ptr<Geometry> >::iterator objIter = objectList.begin();
          while (objIter != objectList.end()) {
            DO_VALIDATION;

            AABB objAABB = (*objIter)->GetAABB();
            float ballRadius = 0.11f + boundingBoxSizeOffset;
            if (objAABB.Intersects(ball->Predict(0), ballRadius)) {
              DO_VALIDATION;
              if (players[i] == players[i]
                                    ->GetTeam()
                                    ->GetDesignatedTeamPossessionPlayer() &&
                  GetLastTouchBias(200) < 0.01f) {
                DO_VALIDATION;

                players[i]->TriggerControlledBallCollision();

              } else {

                float movementBias = oppLastTouchBias * 0.8f + 0.2f;
                bounceVec += (ball->Predict(0) - (*objIter)->GetDerivedPosition()).GetNormalized(Vector3(0)) * movementBias + players[i]->GetMovement() * (1.0f - movementBias);
                bounceCount++;
                players[i]->GetTeam()->SetLastTouchPlayer(players[i], e_TouchType_Accidental);
                Vector3 aabbCenter;
                objAABB.GetCenter(aabbCenter);
                bias += (1.0f - clamp(((ball->Predict(0) - aabbCenter).GetLength() - ballRadius) / objAABB.GetRadius(), 0.0f, 1.0f)) * 0.9f + 0.1f;
              }
            }

            objIter++;
          }
        }
      }
    }
  }

  if (bias > 0.0f) {
    DO_VALIDATION;
    bounceVec /= (bounceCount * 1.0f);
    bounceVec.coords[2] *= 0.6f;
    bounceVec.Normalize();
    Vector3 currentMovement = ball->GetMovement();
    Vector3 fullCollisionVec = (bounceVec * 6.0f) + (bounceVec * currentMovement.GetLength() * 0.6f) + (currentMovement * -0.2f);
    bias = clamp(bias, 0.0f, 1.0f);
    bias = bias * 0.5f + 0.5f;
    Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
    if (resultVector.GetLength() > currentMovement.GetLength()) resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
    //resultVector = resultVector.GetNormalized(0) * (currentMovement.GetLength() * 0.7f + resultVector.GetLength() * 0.3f); // EXPERIMENT!
    resultVector *= 0.7f;

    ball->Touch(resultVector);
    ball->SetRotation(boostrandom(-30, 30), boostrandom(-30, 30),
                      boostrandom(-30, 30), 0.5f * bias);
    lastBodyBallCollisionTime_ms = actualTime_ms;
  }
}

void Match::FollowCamera(Quaternion &orientation, Quaternion &nodeOrientation,
                         Vector3 &position, float &FOV,
                         const Vector3 &targetPosition, float zoom) {
  DO_VALIDATION;
  orientation.SetAngleAxis(0.4f * pi, Vector3(1, 0, 0));
  nodeOrientation.SetAngleAxis(targetPosition.GetAngle2D() + 1.5 * pi, Vector3(0, 0, 1));
  position = targetPosition - targetPosition.Get2D().GetNormalized(Vector3(0, -1, 0)) * 10 * (1.0f / zoom) + Vector3(0, 0, 3);
  FOV = 60.0f;
}

int Match::GetReplaySize_ms() {
  DO_VALIDATION;
  return replaySize_ms;
}

void Match::PrepareGoalNetting() {
  DO_VALIDATION;

  // collect vertices into nettingMeshes[0..1]
  std::vector < MaterializedTriangleMesh > &triangleMesh = boost::static_pointer_cast<Geometry>(GetContext().goalsNode->GetObject("goals"))->GetGeometryData()->GetResource()->GetTriangleMeshesRef();

  for (unsigned int m = 0; m < triangleMesh.size(); m++) {
    DO_VALIDATION;
    for (int i = 0; i < triangleMesh.at(m).verticesDataSize /
                            GetTriangleMeshElementCount();
         i += 3) {
      DO_VALIDATION;
      int goalID = -1;
      if (triangleMesh.at(m).vertices[i + 0] < -pitchHalfW - 0.06f) goalID = 0; // don't catch woodwork, only netting.. DIRTY HAXX
      if (triangleMesh.at(m).vertices[i + 0] >  pitchHalfW + 0.06f) goalID = 1;
      if (goalID >= 0) {
        DO_VALIDATION;
        nettingMeshesSrc[goalID].push_back(Vector3(triangleMesh.at(m).vertices[i + 0], triangleMesh.at(m).vertices[i + 1], triangleMesh.at(m).vertices[i + 2]));
        nettingMeshes[goalID].push_back(&(triangleMesh.at(m).vertices[i]));
      }
    }
  }
}

void Match::UpdateGoalNetting(bool ballTouchesNet) {
  DO_VALIDATION;

  // Rugby has no goal netting — the H-posts are rigid, so skip the
  // football-era deformation pass entirely.
  if (IsRugbyScenario()) {
    nettingHasChanged = false;
    return;
  }

  nettingHasChanged = false;
  int sideID = (ball->GetBallGeom()->GetPosition().coords[0] < 0) ? 0 : 1;
  if (ballTouchesNet) {
    DO_VALIDATION;
    // find vertex closest to ball
    float shortestDistance = 100000.0f;
    //int shortestDistanceID = 0;
    for (unsigned int i = 0; i < nettingMeshes[sideID].size(); i++) {
      DO_VALIDATION;
      Vector3 vertex = nettingMeshesSrc[sideID][i];
      float distance = vertex.GetDistance(ball->GetBallGeom()->GetPosition());
      if (distance < shortestDistance) {
        DO_VALIDATION;
        shortestDistance = distance;
        //shortestDistanceID = i;
      }
    }

    // pull vertices towards ball - the closer, the more intense
    for (unsigned int i = 0; i < nettingMeshes[sideID].size(); i++) {
      DO_VALIDATION;
      const Vector3 &vertex = nettingMeshesSrc[sideID][i];
      float falloffDistance = 4.0f;
      //float influenceBias = clamp(1.0f - (vertex.GetDistance(ball->GetBallGeom()->GetPosition()) - shortestDistance) / falloffDistance, 0.0f, 1.0f);
      float influenceBias = std::pow(
          clamp((shortestDistance + 0.0001f) /
                    (vertex.GetDistance(ball->GetBallGeom()->GetPosition()) +
                     0.0001f),
                0.0f, 1.0f),
          1.5f);
      // net is stuck to woodwork so lay off there
      float woodworkTensionBiasInv = clamp((fabs(ball->GetBallGeom()->GetPosition().coords[0]) - pitchHalfW) * 2.0f, 0.0f, 1.0f);
      influenceBias *= woodworkTensionBiasInv;
      // http://www.wolframalpha.com/input/?i=sin%28x+*+pi+-+0.5+*+pi%29+*+0.5+%2B+0.5+from+x+%3D+0+to+1
      influenceBias = std::sin(influenceBias * pi - 0.5f * pi) * 0.5f + 0.5f;
      if (influenceBias > 0.0f) {
        DO_VALIDATION;
        Vector3 result = vertex * (1.0f - influenceBias) + ball->GetBallGeom()->GetPosition() * influenceBias;
        static_cast<float*>(nettingMeshes[sideID][i])[0] = result.coords[0];
        static_cast<float*>(nettingMeshes[sideID][i])[1] = result.coords[1];
        static_cast<float*>(nettingMeshes[sideID][i])[2] = result.coords[2];
      }
    }
    resetNetting = true; // make sure to reset next time
    nettingHasChanged = true;
    DO_VALIDATION;

  } else if (resetNetting) {
    DO_VALIDATION;  // ball doesn't touch net (anymore), reset
    for (int sideID = 0; sideID < 2; sideID++) {
      DO_VALIDATION;
      for (unsigned int i = 0; i < nettingMeshes[sideID].size(); i++) {
        DO_VALIDATION;
        static_cast<float*>(nettingMeshes[sideID][i])[0] = nettingMeshesSrc[sideID][i].coords[0];
        static_cast<float*>(nettingMeshes[sideID][i])[1] = nettingMeshesSrc[sideID][i].coords[1];
        static_cast<float*>(nettingMeshes[sideID][i])[2] = nettingMeshesSrc[sideID][i].coords[2];
      }
    }
    resetNetting = false;
    nettingHasChanged = true;
    DO_VALIDATION;
  }
  DO_VALIDATION;
}

void Match::UploadGoalNetting() {
  DO_VALIDATION;
  if (nettingHasChanged) {
    DO_VALIDATION;
    boost::static_pointer_cast<Geometry>(GetContext().goalsNode->GetObject("goals"))->OnUpdateGeometryData(false);
  }
}

void Match::BumpActualTime_ms(unsigned long time) {
  if (IsInPlay()) {
    DO_VALIDATION;
    matchTime_ms += time * (1.0f / matchDurationFactor);
  }
  actualTime_ms += time;
  if (IsGoalScored()) goalScoredTimer += time; else goalScoredTimer = 0;

  if (IsInPlay() && !IsInSetPiece()) {
    DO_VALIDATION;
    GetMatchData()->AddPossessionTime(teams[0] == designatedPossessionPlayer->GetTeam() ? 0 : 1, time);
  }
}
