#ifndef CLASSIC_PHASER_DEFINES_H_INCLUDED
#define CLASSIC_PHASER_DEFINES_H_INCLUDED

// ─────────────────────────────────────────────────────────────────────────────
// Constants extracted from the original Classic Phaser.dll (PE32, Delphi,
// "Classic Phaser v1.03 by Kjaerhus Audio").
//
// All addresses below are the ones referenced by the two DSP functions:
//   FUN_00483d5c — parameter → coefficient, called once per processReplacing
//                  block
//   FUN_00484158 — per-sample DSP core
//
// Numeric tables are written as C++ hex float/double literals so they round-trip
// bit-exactly to the values stored in the DLL.  Constants marked [t80] were
// stored as x87 80-bit extended values in the code section and are reproduced
// here as doubles (the closest IEEE type on every target; the original's
// 64-bit mantissa rounds to 53 bits, a ~1e-17 relative difference that vanishes
// after the final float32 store).
//
// Verified against the machine code of FUN_00483d5c and FUN_00484158
// (objdump disassembly of the DLL in prototype/).
// ─────────────────────────────────────────────────────────────────────────────

// ── FUN_00483d5c scalars ─────────────────────────────────────────────────────
// Free-run LFO rate:  rateHz = kRateScale * kRateBase ^ RateNorm
// [t80 @0x4840cc = 100.0, t80 @0x4840d8 = 0.1]
static constexpr double kRateBase       = 0x1.9000000000000p+6;    // 100.0
static constexpr double kRateScale      = 0x1.999999999999ap-4;    // 0.1
// Sync. switch threshold: Sync > 0.5 enables tempo sync   [f32 @0x4840e4]
static constexpr float  kSwitchThresh   = 0.5f;
// u = (int)(kSyncTempoDiv / tempo), bucketed on its low 16 bits
//                                                        [f32 @0x4840e8]
static constexpr float  kSyncTempoDiv   = 768.0f;
// di = (int)(2 ^ (int)(RateNorm * kSyncRateMul))          [f32 @0x4840ec]
static constexpr float  kSyncRateMul    = 8.0f;
// Phase accumulator scale: increment = (int)(2^32 * rate / sr)
//                                                        [f32 @0x484100]
static constexpr double kTwoPow32       = 4294967296.0;
// sync rateHz = syncVal * tempo / kSyncDivisor            [f32 @0x4840fc]
static constexpr float  kSyncDivisor    = 7680.0f;
// Sweep upper limit:  fHigh = kFreqScale * kUpperBase ^ UpperNorm
// [t80 @0x484104 = 400.0, f32 @0x484110 = 50.0]
static constexpr double kUpperBase      = 0x1.9000000000000p+8;    // 400.0
static constexpr float  kFreqScale      = 50.0f;
// Literal 1.0 used for (1 - x) coefficient terms [f32 @0x484114 and @0x4844e4]
static constexpr float  kOneF           = 1.0f;
// Sweep lower limit:  fLow = kPiTimes100 * (fHigh/kFreqScale) ^ (1 - Depth)
// (the tan argument becomes π * fLow / sr)          [t80 @0x484118 = 100π]
static constexpr double kPiTimes100     = 0x1.3a28c59d5433bp+8;    // 314.1592653589793
// fHigh angle:  arg = kTwoPi * fHigh * 0.5 / sr  (= π * fHigh / sr)
//                                                        [t80 @0x484124]
static constexpr double kTwoPi          = 0x1.921fb54442d18p+2;    // 6.283185307179586
// stages = 2 * (int)(kStagesMul * StagesNorm) + 4        [f32 @0x484130]
static constexpr float  kStagesMul      = 4.0f;
// feedback gain = kFeedbackScale * FeedbackNorm          [t80 @0x484134]
static constexpr double kFeedbackScale  = 0x1.f5c28f5c28f5cp-1;    // 0.98
// level gain = kMixScale * MixNorm * kLevelBase ^ LevelNorm
// [t80 @0x484140 = 31.622606821351592 (= 10^1.5),
//  t80 @0x48414c = 0.063096]
// Overall output scale: kMixScale * kLevelBase^Level ≈ -24 dB + 30·Level dB.
static constexpr double kLevelBase      = 0x1.f9f63291ff8bbp+4;    // 31.622606821351592
static constexpr double kMixScale       = 0x1.0270f3882278dp-4;    // 0.063096

// ── FUN_00484158 (per-sample) scalars ────────────────────────────────────────
// Literal 0.5 — input dither offset, LFO half-depth, the −0.5·x term of each
// allpass stage, and the Spread/Sync switch thresholds.
// [f32 @0x4844c4; the identical 0.5 at @0x4840e4 is the Sync threshold]
static constexpr float  kHalfF          = 0.5f;
// Input dither: noise = kModAmp * (rand - 0.5), one RandExt call per sample,
// the same value added to both channels.        [t80 @0x4844c8 = 6e-8]
static constexpr double kModAmp         = 0x1.01b2b29a4692bp-24;   // 6e-8
// LFO: cur = 0.5 + 0.5 * sin(kLfoAngleScale * phase32)
// (≈ 2π / 2^32 — the stored 80-bit value differs from 2π/2^32 in the last few
//  mantissa bits; reproduced here as the exact rounded double)
//                                                        [t80 @0x4844d4]
static constexpr double kLfoAngleScale  = 0x1.921fb5a3dfd60p-30;   // ≈1.4629181e-09
// 20-sample linear smoothing of the decimated LFO value [f32 @0x4844e0]
static constexpr float  kLfoSmoothDiv   = 20.0f;
// Literal 2.0 — the ×2 after each allpass stage        [f32 @0x4844e8]
static constexpr float  kTwoF           = 2.0f;

// ── RandExt (0x402C08): state = state * 0x08088405 + 1; rand = state * 2^-32
// (state dword @0x486008, scale double @0x486068 — identical to the
//  Classic Reverb RE-01 implementation)
static constexpr unsigned kRngMul   = 0x08088405u;
static constexpr double   kRngScale = 0x1.0p-32;

// ─────────────────────────────────────────────────────────────────────────────
// Feature flags
// ─────────────────────────────────────────────────────────────────────────────

// Level protection: tanh soft clip at +5 dBFS.  The original has no output
// limiter, so hot material (especially with high Feedback) can trip the DAW's
// overload protection and mute the track.
#define CLASSIC_PHASER_OUTPUT_SOFT_CLIP 1

// Soft-clip ceiling in linear scale (= +5 dBFS), used only when enabled.
static constexpr float kSoftClipCeiling = 1.77827941f;
static constexpr float kSoftClipCeilingInv = 1.0f / kSoftClipCeiling;

#endif // CLASSIC_PHASER_DEFINES_H_INCLUDED
