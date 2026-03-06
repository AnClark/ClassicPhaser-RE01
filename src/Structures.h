#ifndef CLASSIC_PHASER_STRUCTURES_H_INCLUDED
#define CLASSIC_PHASER_STRUCTURES_H_INCLUDED

#include <cstring>
#include "DistrhoDetails.hpp"   // For ParameterRanges

// ─────────────────────────────────────────────────────────────────────────────
// Parameter indices
// ─────────────────────────────────────────────────────────────────────────────
// Order matches getParamName() of the original Classic Phaser.dll (0..8):
//   Stages, Upper, Rate, Sync., Depth, Spread, Feedback, Mix, Level
enum Parameters
{
    kParamStages = 0,
    kParamUpper,
    kParamRate,
    kParamSync,
    kParamDepth,
    kParamSpread,
    kParamFeedback,
    kParamMix,
    kParamLevel,
    kParamCount
};

// Physical-unit ranges (RE-04 style).  The original VST stores every parameter
// normalised 0..1; updateCoefficients() converts back to that form with exact
// inverse mappings before running the original formulas.
constexpr DISTRHO::ParameterRanges kParamRanges[kParamCount] = {
    // def, min, max
    {    8.0f,    4.0f,    12.0f },   // Stages   (allpass stages, even 4..12)
    { 2000.0f,   50.0f, 20000.0f },   // Upper    Hz  (sweep upper limit)
    {    0.5f,    0.1f,    10.0f },   // Rate     Hz  (free-run LFO rate)
    {    0.0f,    0.0f,     1.0f },   // Sync.    switch (0=free, 1=tempo sync)
    {   75.0f,    0.0f,   100.0f },   // Depth    %
    {    0.0f,    0.0f,     1.0f },   // Spread   switch (0=mono LFO, 1=stereo)
    {   50.0f,    0.0f,   100.0f },   // Feedback %
    {   50.0f,    0.0f,   100.0f },   // Mix      %
    {    0.0f,  -24.0f,     6.0f },   // Level    dB
};

// Maximum number of allpass stages: stages = 2·(int)(4·norm) + 4, norm ∈ [0,1]
// → 4..12 (also the size of the original's two delay-state arrays at
// 0x148 / 0x178, which hold exactly 12 floats each).
static constexpr int kMaxStages = 12;

// (The reverb-specific circular-buffer helpers that used to live here were
// removed together with the Reverb DSP; the phaser only needs the fixed
// 12-slot stage arrays declared in ClassicPhaserPlugin.h.)

#endif // CLASSIC_PHASER_STRUCTURES_H_INCLUDED
