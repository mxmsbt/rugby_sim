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

#include "proceduralpitch.hpp"

#include <cmath>
#include <vector>

#include "../misc/perlin.h"

#include "../gamedefines.hpp"

#include "../types/resource.hpp"
#include "../systems/graphics/resources/texture.hpp"

#include "../main.hpp" // for getconfig

constexpr int perlinTexW = 1600;
constexpr int perlinTexH = 1000;
constexpr int seamlessTexW = 1200;
constexpr int seamlessTexH = 1200;
constexpr int overlayTexW = 4096;
constexpr int overlayTexH = 2048;

namespace {

float PitchCoordToOverlayX(float xCoord) {
  return ((xCoord / pitchFullHalfW) * 0.5f + 0.5f) * overlayTexW;
}

float PitchCoordToOverlayY(float yCoord) {
  return ((yCoord / pitchFullHalfH) * 0.5f + 0.5f) * overlayTexH;
}

void BlendOverlayPixel(Vector3 *overlayTex, float *overlayAlphaTex, int x, int y,
                       const Vector3 &color, float alpha) {
  if (x < 0 || x >= overlayTexW || y < 0 || y >= overlayTexH) return;
  const int index = y * overlayTexW + x;
  const float existingAlpha = overlayAlphaTex[index];
  const float compositeAlpha = alpha + existingAlpha * (1.0f - alpha);
  if (compositeAlpha <= 0.0001f) return;
  overlayTex[index] =
      (color * alpha + overlayTex[index] * existingAlpha * (1.0f - alpha)) /
      compositeAlpha;
  overlayAlphaTex[index] = compositeAlpha;
}

void DrawOverlayRect(Vector3 *overlayTex, float *overlayAlphaTex, float x1,
                     float y1, float x2, float y2, const Vector3 &color,
                     float alpha = 1.0f) {
  const int minX = std::max(0, static_cast<int>(std::floor(std::min(x1, x2))));
  const int maxX = std::min(overlayTexW - 1,
                            static_cast<int>(std::ceil(std::max(x1, x2))));
  const int minY = std::max(0, static_cast<int>(std::floor(std::min(y1, y2))));
  const int maxY = std::min(overlayTexH - 1,
                            static_cast<int>(std::ceil(std::max(y1, y2))));
  for (int x = minX; x <= maxX; ++x) {
    for (int y = minY; y <= maxY; ++y) {
      BlendOverlayPixel(overlayTex, overlayAlphaTex, x, y, color, alpha);
    }
  }
}

void DrawPitchLine(Vector3 *overlayTex, float *overlayAlphaTex, float x1,
                   float y1, float x2, float y2, float thickness,
                   const Vector3 &color, bool dashed = false,
                   float dashLength = 2.0f, float gapLength = 1.4f) {
  if (fabs(x1 - x2) >= fabs(y1 - y2)) {
    const float minX = std::min(x1, x2);
    const float maxX = std::max(x1, x2);
    const float yMin = PitchCoordToOverlayY(y1 + thickness * 0.5f);
    const float yMax = PitchCoordToOverlayY(y1 - thickness * 0.5f);
    if (!dashed) {
      DrawOverlayRect(overlayTex, overlayAlphaTex, PitchCoordToOverlayX(minX),
                      yMin, PitchCoordToOverlayX(maxX), yMax, color);
      return;
    }
    float segmentStart = minX;
    while (segmentStart < maxX) {
      const float segmentEnd = std::min(segmentStart + dashLength, maxX);
      DrawOverlayRect(overlayTex, overlayAlphaTex,
                      PitchCoordToOverlayX(segmentStart), yMin,
                      PitchCoordToOverlayX(segmentEnd), yMax, color);
      segmentStart += dashLength + gapLength;
    }
  } else {
    const float minY = std::min(y1, y2);
    const float maxY = std::max(y1, y2);
    const float xMin = PitchCoordToOverlayX(x1 - thickness * 0.5f);
    const float xMax = PitchCoordToOverlayX(x1 + thickness * 0.5f);
    if (!dashed) {
      DrawOverlayRect(overlayTex, overlayAlphaTex, xMin,
                      PitchCoordToOverlayY(minY), xMax,
                      PitchCoordToOverlayY(maxY), color);
      return;
    }
    float segmentStart = minY;
    while (segmentStart < maxY) {
      const float segmentEnd = std::min(segmentStart + dashLength, maxY);
      DrawOverlayRect(overlayTex, overlayAlphaTex, xMin,
                      PitchCoordToOverlayY(segmentStart), xMax,
                      PitchCoordToOverlayY(segmentEnd), color);
      segmentStart += dashLength + gapLength;
    }
  }
}

// Draws a short "+" tick at (x, y) with given arm length (metres).
void DrawPitchTickCross(Vector3 *overlayTex, float *overlayAlphaTex, float x,
                        float y, float arm, float thickness,
                        const Vector3 &color) {
  DrawPitchLine(overlayTex, overlayAlphaTex, x - arm, y, x + arm, y, thickness,
                color);
  DrawPitchLine(overlayTex, overlayAlphaTex, x, y - arm, x, y + arm, thickness,
                color);
}

// Draws a short perpendicular tick extending from (x0, y) by tickLength in
// the signed direction (+1 inward from +y touchline, -1 from -y touchline).
void DrawTouchlineTick(Vector3 *overlayTex, float *overlayAlphaTex, float x,
                       float y_touch, float inward_sign, float tickLength,
                       float thickness, const Vector3 &color) {
  DrawPitchLine(overlayTex, overlayAlphaTex, x, y_touch, x,
                y_touch + inward_sign * tickLength, thickness, color);
}

void GenerateRugbyOverlay(Vector3 *overlayTex, float *overlayAlphaTex) {
  for (int i = 0; i < overlayTexW * overlayTexH; ++i) {
    overlayTex[i] = Vector3(0, 0, 0);
    overlayAlphaTex[i] = 0.0f;
  }
  const Vector3 lineColor(240, 240, 236);
  const float lineThickness = 0.18f;
  const float tickThick = lineThickness * 0.9f;

  // In-goal (try zone) shading — subtle darker green between the try line
  // and the dead-ball line on each end. Sits under the markings so lines
  // show on top.
  const Vector3 tryZoneColor(20, 72, 22);
  const float tryZoneAlpha = 0.70f;
  DrawOverlayRect(overlayTex, overlayAlphaTex,
                  PitchCoordToOverlayX(-pitchFullHalfW),
                  PitchCoordToOverlayY(-pitchHalfH),
                  PitchCoordToOverlayX(-pitchHalfW),
                  PitchCoordToOverlayY(pitchHalfH), tryZoneColor, tryZoneAlpha);
  DrawOverlayRect(overlayTex, overlayAlphaTex,
                  PitchCoordToOverlayX(pitchHalfW),
                  PitchCoordToOverlayY(-pitchHalfH),
                  PitchCoordToOverlayX(pitchFullHalfW),
                  PitchCoordToOverlayY(pitchHalfH), tryZoneColor, tryZoneAlpha);

  // Sidelines (touchlines) + try lines (solid, thicker) + dead-ball lines.
  DrawPitchLine(overlayTex, overlayAlphaTex, -pitchHalfW, -pitchHalfH,
                pitchHalfW, -pitchHalfH, lineThickness, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, -pitchHalfW, pitchHalfH,
                pitchHalfW, pitchHalfH, lineThickness, lineColor);
  // Try lines slightly thicker than touchlines, as on real pitches.
  DrawPitchLine(overlayTex, overlayAlphaTex, -pitchHalfW, -pitchHalfH,
                -pitchHalfW, pitchHalfH, lineThickness * 1.15f, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, pitchHalfW, -pitchHalfH,
                pitchHalfW, pitchHalfH, lineThickness * 1.15f, lineColor);
  // Halfway line (solid).
  DrawPitchLine(overlayTex, overlayAlphaTex, 0.0f, -pitchHalfH, 0.0f,
                pitchHalfH, lineThickness, lineColor);

  // 10 m lines (short dashes, as in the real pitch).
  const float tenMeter = 10.0f;
  for (float x : {-tenMeter, tenMeter}) {
    DrawPitchLine(overlayTex, overlayAlphaTex, x, -pitchHalfH, x, pitchHalfH,
                  tickThick, lineColor, true, 1.2f, 1.2f);
  }

  // 22 m lines (solid).
  const float twentyTwo = 22.0f;
  DrawPitchLine(overlayTex, overlayAlphaTex, -twentyTwo, -pitchHalfH,
                -twentyTwo, pitchHalfH, lineThickness, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, twentyTwo, -pitchHalfH, twentyTwo,
                pitchHalfH, lineThickness, lineColor);

  // 5 m lines (dashed, close to each try line).
  const float fiveMeter = 5.0f;
  for (float x : {-pitchHalfW + fiveMeter, pitchHalfW - fiveMeter}) {
    DrawPitchLine(overlayTex, overlayAlphaTex, x, -pitchHalfH, x, pitchHalfH,
                  tickThick, lineColor, true, 0.9f, 1.1f);
  }

  // 15 m inset dashed lines parallel to touch (mark the lineout alley
  // boundary).
  const float fifteenInset = 15.0f;
  for (float y : {-fifteenInset, fifteenInset}) {
    DrawPitchLine(overlayTex, overlayAlphaTex, -pitchHalfW, y, pitchHalfW, y,
                  tickThick * 0.95f, lineColor, true, 1.8f, 1.2f);
  }

  // 5 m inset dashed lines parallel to touch (for the 5 m lineout alley).
  const float fiveInset = 5.0f;
  for (float y : {-pitchHalfH + fiveInset, pitchHalfH - fiveInset}) {
    DrawPitchLine(overlayTex, overlayAlphaTex, -pitchHalfW, y, pitchHalfW, y,
                  tickThick * 0.85f, lineColor, true, 0.9f, 1.0f);
  }

  // Perpendicular tick crosses at the intersections of the 5 m and 15 m
  // inset lines with the 22 m / 10 m / halfway / 5 m-from-try lines. This
  // is the distinctive dotted-cross pattern seen on real rugby pitches.
  const float crossArm = 0.45f;
  std::vector<float> crossXs = {-(pitchHalfW - fiveMeter), -twentyTwo,
                                -tenMeter, 0.0f, tenMeter, twentyTwo,
                                pitchHalfW - fiveMeter};
  std::vector<float> crossYs = {-fifteenInset, -fiveInset,
                                -(pitchHalfH - fiveInset),
                                pitchHalfH - fiveInset, fiveInset,
                                fifteenInset};
  for (float x : crossXs) {
    for (float y : crossYs) {
      DrawPitchTickCross(overlayTex, overlayAlphaTex, x, y, crossArm,
                         tickThick, lineColor);
    }
  }

  // Touchline hash marks: short perpendicular ticks every 10 m along each
  // sideline, protruding 0.6 m inward. Gives the pitch its "measured"
  // look without being noisy.
  const float hashLen = 0.6f;
  for (float x = -pitchHalfW + 10.0f; x <= pitchHalfW - 10.0f; x += 10.0f) {
    if (std::fabs(x) < 0.5f) continue;  // skip halfway (already a line)
    DrawTouchlineTick(overlayTex, overlayAlphaTex, x, -pitchHalfH, 1.0f,
                      hashLen, tickThick, lineColor);
    DrawTouchlineTick(overlayTex, overlayAlphaTex, x, pitchHalfH, -1.0f,
                      hashLen, tickThick, lineColor);
  }

  // Goal-line hash marks on each try line: 5 m and 15 m ticks into the
  // field so the lineout-throw channel is visually marked.
  for (float x : {-pitchHalfW, pitchHalfW}) {
    const float sign = (x > 0.0f) ? -1.0f : 1.0f;
    for (float y : {-fifteenInset, -fiveInset, fiveInset, fifteenInset}) {
      DrawPitchLine(overlayTex, overlayAlphaTex, x, y,
                    x + sign * hashLen * 1.4f, y, tickThick, lineColor);
    }
  }

  // Dead-ball lines at the back of each in-goal area.
  DrawPitchLine(overlayTex, overlayAlphaTex, -pitchFullHalfW, -pitchHalfH,
                -pitchFullHalfW, pitchHalfH, lineThickness, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, pitchFullHalfW, -pitchHalfH,
                pitchFullHalfW, pitchHalfH, lineThickness, lineColor);
  // In-goal sidelines connecting try line to dead-ball line.
  DrawPitchLine(overlayTex, overlayAlphaTex, -pitchFullHalfW, -pitchHalfH,
                -pitchHalfW, -pitchHalfH, lineThickness, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, -pitchFullHalfW, pitchHalfH,
                -pitchHalfW, pitchHalfH, lineThickness, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, pitchHalfW, -pitchHalfH,
                pitchFullHalfW, -pitchHalfH, lineThickness, lineColor);
  DrawPitchLine(overlayTex, overlayAlphaTex, pitchHalfW, pitchHalfH,
                pitchFullHalfW, pitchHalfH, lineThickness, lineColor);
}

}  // namespace

template <typename T>
T BilinearSample(T *tex, float x, float y, int w, int h) {
  DO_VALIDATION;
  // nearest neighbor version
  //return tex[int(y) * w + int(x)];

  // actual bilinear version
  int intX1 = int(std::floor(x));
  int intY1 = int(std::floor(y));
  int intX2 = (int(std::floor(x)) + 1) % w;
  int intY2 = (int(std::floor(y)) + 1) % h;
  T x1y1 = tex[intY1 * w + intX1];
  T x2y1 = tex[intY1 * w + intX2];
  T x1y2 = tex[intY2 * w + intX1];
  T x2y2 = tex[intY2 * w + intX2];
  float xBias = x - intX1;
  float yBias = y - intY1;
  T result = (x1y1 * (1.0f - xBias) + x2y1 * xBias) * (1.0f - yBias) +
             (x1y2 * (1.0f - xBias) + x2y2 * xBias) * yBias;

  return result;
}

Uint32 GetPitchDiffuseColor(SDL_Surface *pitchSurf, Vector3 *seamlessTex,
                            float *perlinTex, Vector3 *overlayTex,
                            float *overlay_alphaTex, float xCoord,
                            float yCoord) {
  DO_VALIDATION;

  float texMultiplier = 0.3f;
  float texScale = 0.32f;
  float randomNoiseMultiplier = 0.0f;
  float perlinNoiseMultiplier = 0.4f;
  float brightness = 2.0f;

  float r, g, b;

  float contrast = 0.4f; // g <=> rb contrast. lower = less saturation, higher = greener
  float rToB = GetConfiguration()->GetReal("graphics_pitchredtoblueratio", 0.5f) * 2.0f; // 0 .. 2, higher is more red, lower is more blue
  r = ((35 - contrast * 10) * rToB ) * brightness;
  g = 46 * brightness;
  b = ((25 - contrast * 10) * (2.0f - rToB) ) * brightness;

  float seamlessX = ((xCoord / pitchFullHalfW) * 0.5 + 0.5) * seamlessTexW * 18.0 * texScale;
  float seamlessY = ((yCoord / pitchFullHalfH) * 0.5 + 0.5) * seamlessTexH * 12.0 * texScale;
  seamlessX = std::fmod(seamlessX, seamlessTexW);
  seamlessY = std::fmod(seamlessY, seamlessTexH);
  //printf("%f, %f - ", seamlessX, seamlessY);
  Vector3 tex = BilinearSample(seamlessTex, seamlessX, seamlessY, seamlessTexW, seamlessTexH);
  r = r * (1.0f - texMultiplier) + tex.coords[0] * texMultiplier;
  g = g * (1.0f - texMultiplier) + tex.coords[1] * texMultiplier;
  b = b * (1.0f - texMultiplier) + tex.coords[2] * texMultiplier;

  float perlX = ((xCoord / pitchFullHalfW) * 0.5 + 0.5) * perlinTexW;
  float perlY = ((yCoord / pitchFullHalfH) * 0.5 + 0.5) * perlinTexH;
  float randomSpread = 2.5f;
  float randomX = random_non_determ(-1, 1);
  perlX = clamp(perlX + randomX * randomSpread, 0, perlinTexW - 1);
  float randomY = random_non_determ(-1, 1);
  perlY = clamp(perlY + randomY * randomSpread, 0, perlinTexH - 1);
  float perlinNoise = BilinearSample(perlinTex, perlX, perlY, perlinTexW, perlinTexH) - 0.5f;
  float perlinNoiseR = perlinNoise;
  float perlinNoiseG = perlinNoise;
  float perlinNoiseB = perlinNoise;

  /* mud
    if (noise < 0.1) { DO_VALIDATION;
      float bias = noise / 1.0;
      r = (r * bias) + r * 1.0  * (1 - bias);
      g = (g * bias) + g * 0.9  * (1 - bias);
      b = (b * bias) + b * 0.9  * (1 - bias);
    }
  */

  float randomNoise = 0.0f;
  if (randomNoiseMultiplier > 0.0f) randomNoise = random_non_determ(-1, 1);
  r += ((perlinNoiseR * perlinNoiseMultiplier) + (randomNoise * randomNoiseMultiplier)) * 40.0f;
  g += ((perlinNoiseG * perlinNoiseMultiplier) + (randomNoise * randomNoiseMultiplier)) * 40.0f;
  b += ((perlinNoiseB * perlinNoiseMultiplier) + (randomNoise * randomNoiseMultiplier)) * 40.0f;

  // fake ambient occlusion
  Vector3 lightPos = Vector3(0, 0, 0);
  float darkness =
      1.0f - std::pow(clamp((lightPos - Vector3(xCoord / pitchHalfW,
                                                yCoord / pitchHalfH, 0))
                                    .GetLength() *
                                0.7f,
                            0.0, 1.0),
                      1.5f) *
                 0.18f;
  r *= darkness;
  g *= darkness;
  b *= darkness;

  float overlayX = ((xCoord / pitchFullHalfW) * 0.5 + 0.5) * overlayTexW;
  float overlayY = ((yCoord / pitchFullHalfH) * 0.5 + 0.5) * overlayTexH;
  Vector3 overlay = BilinearSample(overlayTex, overlayX, overlayY, overlayTexW, overlayTexH);
  float overlay_alpha = BilinearSample(overlay_alphaTex, overlayX, overlayY, overlayTexW, overlayTexH);
  r = clamp(r * (1.0 - overlay_alpha) + overlay.coords[0] * overlay_alpha, 0, 255);
  g = clamp(g * (1.0 - overlay_alpha) + overlay.coords[1] * overlay_alpha, 0, 255);
  b = clamp(b * (1.0 - overlay_alpha) + overlay.coords[2] * overlay_alpha, 0, 255);

  Uint32 color = SDL_MapRGB(pitchSurf->format, r, g, b);
  return color;
}

inline Uint32 GetPitchSpecularColor(SDL_Surface *pitchSurf, float *perlinTex,
                                    float xCoord, float yCoord) {
  DO_VALIDATION;

  float base = 2.0f;
  float noisefac = 18.0f;

  float perlX = ((xCoord / pitchFullHalfW) * 0.5 + 0.5) * perlinTexW;
  float perlY = ((yCoord / pitchFullHalfH) * 0.5 + 0.5) * perlinTexH;
  float randomSpread = 2.5f;
  float randomX = random_non_determ(-1, 1);
  perlX = clamp(perlX + randomX * randomSpread, 0, perlinTexW - 1);
  float randomY = random_non_determ(-1, 1);
  perlY = clamp(perlY + randomY * randomSpread, 0, perlinTexH - 1);
  float noise = base + BilinearSample(perlinTex, perlX, perlY, perlinTexW, perlinTexH) * noisefac;

  Uint32 color = SDL_MapRGB(pitchSurf->format, noise, noise, noise);
  return color;
}

float GetSmoothGrassDirection(float coord, float repeat,
                              int transitionSharpness = 5) {
  DO_VALIDATION;
  float iteration = std::floor(coord / repeat);
  float bias = coord - repeat * iteration; // goes from 0 to 1 over two bands (since bands are split by being < 0.5 and > 0.5)
  bias = std::sin(bias * 2 * pi) * 0.5f + 0.5f;
  for (int i = 0; i < transitionSharpness; i++) {
    DO_VALIDATION;
    bias = curve(bias, 1.0f);
  }

  // back to -1 .. 1 range
  bias *= 2.0f;
  bias -= 1.0f;
  return bias;
}

Uint32 GetPitchNormalColor(SDL_Surface *pitchSurf, float xCoord, float yCoord,
                           float repeatMultiplier) {
  DO_VALIDATION;
  float noisefac = 0.06f;

  Vector3 normal = Vector3(0, 0, 1);

  if (fabs(xCoord) < pitchHalfW && fabs(yCoord) < pitchHalfH) {
    DO_VALIDATION;

    float xRepeat = 11.0f * repeatMultiplier;
    float yRepeat = 11.0f * repeatMultiplier;
    float xStrength = 0.12;
    float yStrength = 0.1; // i *think* the mowing of the 'lateral' lines may undo the strength of these medial lines. so make this less apparent

    int transitionSharpness = 5;
    if (repeatMultiplier > 0.75f) transitionSharpness = 7; // wider mow lines == more sharpening to correct for upscale
    normal += Vector3(GetSmoothGrassDirection(yCoord / yRepeat, 1.0f, transitionSharpness) * yStrength, GetSmoothGrassDirection(xCoord / xRepeat, 1.0f, transitionSharpness) * xStrength, 0);
  }

  normal.coords[0] += random_non_determ(-1, 1) * noisefac;
  normal.coords[1] += random_non_determ(-1, 1) * noisefac;

  normal.Normalize();

  normal.coords[0] = normal.coords[0] * 0.5f + 0.5f;
  normal.coords[1] = normal.coords[1] * 0.5f + 0.5f;
  normal.coords[2] = normal.coords[2] * 0.5f + 0.5f;

  Uint32 color = SDL_MapRGB(pitchSurf->format, normal.coords[0] * 255, normal.coords[1] * 255, normal.coords[2] * 255);
  return color;
}

/* todo
void DrawMud(SDL_PixelFormat *pixelFormat, Uint32 *diffuseBitmap, int resX, int
resY, signed int offsetW, signed int offsetH) { DO_VALIDATION;
}
*/

void CreateChunk(int i, int resX, int resY, int resSpecularX, int resSpecularY,
                 int resNormalX, int resNormalY,
                 float grassNormalRepeatMultiplier, Vector3 *seamlessTex,
                 float *perlinTex, Vector3 *overlayTex,
                 float *overlay_alphaTex) {
  DO_VALIDATION;

  signed int offsetW, offsetH;
  if (i == 1 || i == 3) offsetW = -1; else
                        offsetW = 0;
  if (i == 1 || i == 2) offsetH = -1; else
                        offsetH = 0;

  SDL_Surface *pitchDiffuseSurf = CreateSDLSurface(resX, resY);
  SDL_Surface *pitchSpecularSurf = CreateSDLSurface(resSpecularX, resSpecularY);
  SDL_Surface *pitchNormalSurf = CreateSDLSurface(resNormalX, resNormalY);

  Uint32 *diffuseBitmap;
  diffuseBitmap = new Uint32[resX * resY];
  Uint32 *specularBitmap;
  specularBitmap = new Uint32[resSpecularX * resSpecularY];
  Uint32 *normalBitmap;
  normalBitmap = new Uint32[resNormalX * resNormalY];

  for (int x = 0; x < resX; x++) {
    DO_VALIDATION;
    for (int y = 0; y < resY; y++) {
      DO_VALIDATION;
      float xCoord = x / (resX * 1.0) * pitchFullHalfW + pitchFullHalfW * offsetW;
      float yCoord = y / (resY * 1.0) * pitchFullHalfH + pitchFullHalfH * offsetH;
      diffuseBitmap[y * resX + x] = GetPitchDiffuseColor(pitchDiffuseSurf, seamlessTex, perlinTex, overlayTex, overlay_alphaTex, xCoord, yCoord);
    }
  }
  for (int x = 0; x < resSpecularX; x++) {
    DO_VALIDATION;
    for (int y = 0; y < resSpecularY; y++) {
      DO_VALIDATION;
      float xSpecularCoord = x / (resSpecularX * 1.0) * pitchFullHalfW + pitchFullHalfW * offsetW;
      float ySpecularCoord = y / (resSpecularY * 1.0) * pitchFullHalfH + pitchFullHalfH * offsetH;
      specularBitmap[y * resSpecularX + x] = GetPitchSpecularColor(pitchSpecularSurf, perlinTex, xSpecularCoord, ySpecularCoord);
    }
  }
  for (int x = 0; x < resNormalX; x++) {
    DO_VALIDATION;
    for (int y = 0; y < resNormalY; y++) {
      DO_VALIDATION;
      float xNormalCoord = x / (resNormalX * 1.0) * pitchFullHalfW + pitchFullHalfW * offsetW;
      float yNormalCoord = y / (resNormalY * 1.0) * pitchFullHalfH + pitchFullHalfH * offsetH;
      normalBitmap[y * resNormalX + x] = GetPitchNormalColor(pitchNormalSurf, xNormalCoord, yNormalCoord, grassNormalRepeatMultiplier);
    }
  }

  memcpy(pitchDiffuseSurf->pixels, diffuseBitmap, resX * resY * sizeof(Uint32));
  memcpy(pitchSpecularSurf->pixels, specularBitmap, resSpecularX * resSpecularY * sizeof(Uint32));
  memcpy(pitchNormalSurf->pixels, normalBitmap, resNormalX * resNormalY * sizeof(Uint32));
  delete [] diffuseBitmap;
  delete [] specularBitmap;
  delete [] normalBitmap;

  // find pitch texture

  bool alreadyThere = false;
  boost::intrusive_ptr<Resource<Texture> > pitchDiffuseTex =
      GetContext().texture_manager.Fetch("pitch_0" + int_to_str(i) + ".png",
                                         false, alreadyThere, true);
  assert(alreadyThere);
  boost::intrusive_ptr<Resource<Texture> > pitchSpecularTex =
      GetContext().texture_manager.Fetch(
          "pitch_specular_0" + int_to_str(i) + ".png", false, alreadyThere,
          true);
  assert(alreadyThere);
  boost::intrusive_ptr<Resource<Texture> > pitchNormalTex =
      GetContext().texture_manager.Fetch(
          "pitch_normal_0" + int_to_str(i) + ".png", false, alreadyThere, true);
  assert(alreadyThere);



  // overwrite pitch texture

  pitchDiffuseTex->GetResource()->DeleteTexture();
  pitchDiffuseTex->GetResource()->CreateTexture(e_InternalPixelFormat_SRGB8, e_PixelFormat_RGB, resX, resY, false, true, true, true);
  pitchDiffuseTex->GetResource()->UpdateTexture(pitchDiffuseSurf, false, true);
  SDL_FreeSurface(pitchDiffuseSurf);

  pitchSpecularTex->GetResource()->DeleteTexture();
  pitchSpecularTex->GetResource()->CreateTexture(e_InternalPixelFormat_RGB8, e_PixelFormat_RGB, resSpecularX, resSpecularY, false, true, true, true);
  pitchSpecularTex->GetResource()->UpdateTexture(pitchSpecularSurf, false, true);
  SDL_FreeSurface(pitchSpecularSurf);

  pitchNormalTex->GetResource()->DeleteTexture();
  pitchNormalTex->GetResource()->CreateTexture(e_InternalPixelFormat_RGB8, e_PixelFormat_RGB, resNormalX, resNormalY, false, true, true, true);
  pitchNormalTex->GetResource()->UpdateTexture(pitchNormalSurf, false, true);
  SDL_FreeSurface(pitchNormalSurf);
}

void GeneratePitch(int resX, int resY, int resSpecularX, int resSpecularY,
                   int resNormalX, int resNormalY) {
  DO_VALIDATION;
  static bool lastWasRugby = false;
  const bool rugbyFieldRequested =
      GetScenarioConfig().left_team.size() == 15 &&
      GetScenarioConfig().right_team.size() == 15;
  if (GetContext().already_loaded && lastWasRugby == rugbyFieldRequested) {
    DO_VALIDATION;
    return;
  }
  GetContext().already_loaded = true;
  lastWasRugby = rugbyFieldRequested;

  SDL_Surface *seamless = IMG_LoadBmp("media/textures/pitch/seamlessgrass08.png");
  SDL_PixelFormat seamlessFormat = *seamless->format;
  assert(seamlessTexW == seamless->w);
  assert(seamlessTexH == seamless->h);
  Vector3 *seamlessTex = new Vector3[seamlessTexW * seamlessTexH];
  for (int x = 0; x < seamlessTexW; x++) {
    DO_VALIDATION;
    for (int y = 0; y < seamlessTexH; y++) {
      DO_VALIDATION;
      Uint32 pixel = sdl_getpixel(seamless, x, y);
      Uint8 r, g, b;
      SDL_GetRGB(pixel, &seamlessFormat, &r, &g, &b);
      seamlessTex[y * seamless->w + x] = Vector3(r, g, b);
    }
  }
  SDL_FreeSurface(seamless);

  Vector3 *overlayTex = new Vector3[overlayTexW * overlayTexH];
  float* overlay_alphaTex = new float[overlayTexW * overlayTexH];
  const bool rugbyField = rugbyFieldRequested;
  if (rugbyField) {
    GenerateRugbyOverlay(overlayTex, overlay_alphaTex);
  } else {
    SDL_Surface *overlay = IMG_LoadBmp("media/textures/pitch/overlay.png");
    SDL_PixelFormat overlayFormat = *overlay->format;
    assert(overlayTexW == overlay->w);
    assert(overlayTexH == overlay->h);
    for (int x = 0; x < overlayTexW; x++) {
      DO_VALIDATION;
      for (int y = 0; y < overlayTexH; y++) {
        DO_VALIDATION;
        Uint32 pixel = sdl_getpixel(overlay, x, y);
        Uint8 r, g, b, a;
        SDL_GetRGBA(pixel, &overlayFormat, &r, &g, &b, &a);
        overlayTex[y * overlay->w + x] = Vector3(r, g, b);
        overlay_alphaTex[y * overlay->w + x] = a / 256.0f;
      }
    }
    SDL_FreeSurface(overlay);
  }

  float scale = 0.06f;

  Perlin *perlin1 = new Perlin(4, 0.06 * scale, 0.5); // low freq
  Perlin *perlin2 = new Perlin(4, 0.14 * scale, 0.5); // mid freq
//  Perlin *perlin3 = new Perlin(4, 25.4 / 20.0,   3, 423423); // high freq
  float* perlinTex = new float[perlinTexW * perlinTexH];

  // make sure sines are in range -1 to 1
  // generate sine
  float noiseFactor = 0.15; // 'random grid of canals'
  float sinScale = 4.0f; // smaller is larger (heh)
  float ynoise[perlinTexH];
  for (int y = 0; y < perlinTexH; y++) {
    DO_VALIDATION;
    ynoise[y] = (std::sin(y / (float)perlinTexH * 13 * sinScale) +
                 std::sin(y / (float)perlinTexH * 43 * sinScale) +
                 std::sin(y / (float)perlinTexH * 107 * sinScale) +
                 std::sin(y / (float)perlinTexH * 245 * sinScale)) *
                0.25f;
  }

  for (int x = 0; x < perlinTexW; x++) {
    DO_VALIDATION;
    float xnoise = (std::sin(x / (float)perlinTexW * 15 * sinScale) +
                    std::sin(x / (float)perlinTexW * 41 * sinScale) +
                    std::sin(x / (float)perlinTexW * 109 * sinScale) +
                    std::sin(x / (float)perlinTexW * 241 * sinScale)) *
                   0.25f;
    for (int y = 0; y < perlinTexH; y++) {
      DO_VALIDATION;
      float noise = xnoise * 0.65f + ynoise[y] * 0.35f;
      noise = curve(noise * 0.5f + 0.5f, 0.4f) * 2.0f - 1.0f; // compress
      //printf("%f ", perlin1->Get(x, y));
      perlinTex[y * perlinTexW + x] = perlin1->Get(x, y) * 0.4f +
                                      perlin2->Get(x, y) * 0.6f; // + 0.5f to get it from [0..1]; the multiplier is bias between the noises
      perlinTex[y * perlinTexW + x] = perlinTex[y * perlinTexW + x] * 1.7f + 0.5f; // most of perlin noise is well between -0.5 and 0.5, so expand a bit
      perlinTex[y * perlinTexW + x] += (NormalizedClamp(noise, -0.9f, 0.9f) * 2.0f - 1.0f) * noiseFactor; // cut off on both sides, for graphics effect
      perlinTex[y * perlinTexW + x] = clamp(perlinTex[y * perlinTexW + x], 0.2f, 0.8f); // clamp in range; there'll be some clipping otherwise
      perlinTex[y * perlinTexW + x] = curve(perlinTex[y * perlinTexW + x], 0.4f);
    }
  }

//  boost::thread pitchThread[4];
  float grassNormalRepeatMultiplier = (boostrandom(0, 1) > 0.5f) ? 1.0f : 0.5f;
  for (int i = 0; i < 4; i++) {
    DO_VALIDATION;
    CreateChunk(i + 1, resX, resY, resSpecularX, resSpecularY, resNormalX,
                resNormalY, grassNormalRepeatMultiplier, seamlessTex, perlinTex,
                overlayTex, overlay_alphaTex);
  }

  //  for (int i = 0; i < 4; i++) { DO_VALIDATION;
  // pitchThread[i].join();
  //}

  delete perlin1;
  delete perlin2;
  delete [] perlinTex;
  delete [] overlayTex;
  delete [] overlay_alphaTex;
  delete [] seamlessTex;
}
