#include "ClassicPhaserPlugin.h"
#include "Defines.h"
#include "extra/ScopedDenormalDisable.hpp"

#include <algorithm>
#include <cmath>

// ═════════════════════════════════════════════════════════════════════════════
// Per-sample DSP — faithful transcription of FUN_00484158 ("ProcessSample")
// from the original Classic Phaser.dll (v1.03, Kjaerhus Audio).
//
// Every statement mirrors one float32 store (x87 fstps) of the original, and
// multi-term expressions are evaluated in double so that rounding happens
// once at the store, exactly like the x87 register chain (a plain float chain
// rounds at every step and diverges roughly a quarter of the time).
// True bit-exactness against the x87 build is impossible off-x86 — the
// original's fsin/fcos and 80-bit intermediates have no IEEE double
// equivalent — but formulas, constants, operation order and rounding points
// all match the binary (see the disassembly notes in Defines.h).
//
// One deliberate improvement is enabled on top (see Defines.h):
// OUTPUT_SOFT_CLIP (output level protection).  The original's RandExt input
// dither (6e-8 ≈ −144 dBFS) is implemented faithfully.
// ═════════════════════════════════════════════════════════════════════════════

void ClassicPhaserPlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    const ScopedDenormalDisable denormalDisable;

    // The original calls FUN_00483d5c once at the top of every
    // processReplacing() block — replicate that here, so parameters take
    // effect with block granularity exactly like the original.
    //
    // Performance note — this per-block call is NOT a bottleneck
    // (please do not "optimize" it away without reading):
    //
    //   The coefficient block is O(1) per block, not O(frames):
    //     • ~9 libm calls per invocation: 3 log + 4 exp + 2 sincos
    //       (constant logs are already folded at compile time;
    //        sync mode adds getTimePosition + ldexp)
    //     • ≈ 150–250 ns per call (-O2/arm64)
    //     • ≈ 0.002 % of one core @ 44.1 kHz / 512-sample blocks
    //       ≈ 0.06 % even @ 192 kHz / 64-sample blocks
    //
    //   The real hot spots are O(frames):
    //     • the per-sample allpass chain — the dominant share of the
    //       plugin's own CPU, which is well under 1 % of one core
    //     • the soft clip's tanhf (2 per sample) — measured 3.5–6.4 ns
    //       per sample per channel, ≈ 0.03–0.06 % of one core at
    //       44.1 kHz stereo.  Evaluated for optimization (fast-tanh
    //       approximation vs. a near-full-scale guard) and deliberately
    //       kept as-is: either would save ≤ 0.05 % of a core while
    //       slightly degrading the curve's fidelity.
    //
    //   Caching across blocks would save almost nothing: sync mode
    //   re-derives the position-dependent phase seed every block anyway,
    //   and the original performs this same per-block call.
    updateCoefficients();

    const float* inL  = inputs[0];
    const float* inR  = inputs[1];
    float*       outL = outputs[0];
    float*       outR = outputs[1];

    // Cache the coefficient block (the original re-reads these float32
    // fields from the state struct for every operation).
    const float wet    = fWetGain;       // 0x104
    const float dry    = fDryGain;       // 0x108
    const float fb     = fFeedbackGain;  // 0x120
    const int   stages = fStages;        // 0xF0
    const float tanL   = fTanLow;        // 0x118
    const float tanH   = fTanHigh;       // 0x11C

    for (uint32_t i = 0; i < frames; ++i)
    {
        const float dryL = inL[i];
        const float dryR = inR[i];

        // ── 1. Input dither: noise = kModAmp * (rand - 0.5) ───────────────
        //     RandExt (0x402C08) runs once per frame; the same value is
        //     added to both channels (0xF8 -> 0x110 / 0x10C).
        const float dither = nextNoise();

        // ── 2. Input accumulation:  x = ((in + dither) + state) ────────────
        //     `state` is the feedback-scaled chain output of the previous
        //     sample (0x110 / 0x10C).
        fChainL = (float)((double)dryL + (double)dither + (double)fChainL);
        fChainR = (float)((double)dryR + (double)dither + (double)fChainR);

        // ── 3. LFO: new value once every 20 samples (counter 0xF4) ────────
        if (fLfoStep == 0)
        {
            fLfoPrev = fLfoCur;                       // 0x128 <- 0x12C
            // fild of the 32-bit phase zero-extends → unsigned interpretation
            fLfoCur = (float)(0.5 + 0.5 * std::sin(kLfoAngleScale * (double)fPhase));
        }

        // ── 4. Phase accumulator: 0x1A8 += 0xE8, wraps mod 2^32 ────────────
        fPhase += (uint32_t)fPhaseInc;

        // ── 5. 20-sample linear smoothing of the decimated LFO ─────────────
        //     mod = (f4·cur + (20−f4)·prev) / 20, evaluated with the f4
        //     value from BEFORE the increment (matches the original order).
        const int   step = (int)fLfoStep;
        const float mod  = (float)(((double)step * (double)fLfoCur
                                  + (double)(20 - step) * (double)fLfoPrev)
                                 / (double)kLfoSmoothDiv);
        if (++fLfoStep == 20)
            fLfoStep = 0;

        // ── 6. Per-sample coefficient sets (0x114 / 0x130 / 0x138 / 0x140) ─
        //     a = x/(1+x),  n = −a,  c = (1−x)/(1+x)   with x = interpolated
        //     tan.  Set 0 is always used by the left channel; the right
        //     channel uses set 1 when Spread > 0.5 (modulation reversed).
        const float x0 = (float)((1.0 - (double)mod) * (double)tanL
                               + (double)mod * (double)tanH);
        const float a0 = (float)((double)x0 / (1.0 + (double)x0));
        const float n0 = -a0;
        const float c0 = (float)((1.0 - (double)x0) / (1.0 + (double)x0));

        const bool spread = fParams[kParamSpread] > kHalfF;   // param 0xCC
        float aR = a0, cR = c0;
        if (spread)
        {
            const float x1 = (float)((1.0 - (double)mod) * (double)tanH
                                    + (double)mod * (double)tanL);
            aR = (float)((double)x1 / (1.0 + (double)x1));
            cR = (float)((1.0 - (double)x1) / (1.0 + (double)x1));
        }
        const float nR = -aR;

        // ── 7. Allpass stage chain (delay slots 0x148.. / 0x178..) ────────
        //     v = x − d·c
        //     y = (d·(−a) + v·a) − 0.5·x,  then y *= 2
        //     d ← v
        for (int s = 0; s < stages; ++s)
        {
            // Left — coefficient set 0 (untouched by Spread)
            const float dL = fDelayL[s];
            const float vL = (float)((double)fChainL - (double)dL * (double)c0);
            float yL = (float)(((double)vL * (double)a0 + (double)dL * (double)n0)
                             - 0.5 * (double)fChainL);
            yL *= kTwoF;
            fChainL = yL;
            fDelayL[s] = vL;

            // Right — coefficient set selected by Spread
            const float dR = fDelayR[s];
            const float vR = (float)((double)fChainR - (double)dR * (double)cR);
            float yR = (float)(((double)vR * (double)aR + (double)dR * (double)nR)
                             - 0.5 * (double)fChainR);
            yR *= kTwoF;
            fChainR = yR;
            fDelayR[s] = vR;
        }

        // ── 8. Dry/wet mix ─────────────────────────────────────────────────
        float oL = (float)((double)fChainL * (double)wet + (double)dryL * (double)dry);
        float oR = (float)((double)fChainR * (double)wet + (double)dryR * (double)dry);

#if CLASSIC_PHASER_OUTPUT_SOFT_CLIP
        oL = kSoftClipCeiling * std::tanh(oL * kSoftClipCeilingInv);
        oR = kSoftClipCeiling * std::tanh(oR * kSoftClipCeilingInv);
#endif // CLASSIC_PHASER_OUTPUT_SOFT_CLIP

        outL[i] = oL;
        outR[i] = oR;

        // ── 9. Feedback for the next sample (0x120) ────────────────────────
        fChainL = (float)((double)fChainL * (double)fb);
        fChainR = (float)((double)fChainR * (double)fb);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Coefficient update — faithful transcription of FUN_00483d5c
// (parameters → coefficients / LFO rate / phase seed), called once per block.
//
// The original stores all nine parameters normalised 0..1 (setParameter at
// 0x483CD0 clamps to that range); this implementation keeps them in physical
// units (RE-04 style) and converts them back with exact inverse mappings
// before running the original formulas.
// ═════════════════════════════════════════════════════════════════════════════

// The original's power-of-2 bucket for u = (int)(768 / tempo) (0x483E2C..):
//   [2^k, 2^(k+1)) → shift by k (k = 1..8);  u < 2 and u ≥ 512 → no shift.
static int syncBucketShift(unsigned u)
{
    if (u <   2u) return 0;
    if (u <   4u) return 1;
    if (u <   8u) return 2;
    if (u <  16u) return 3;
    if (u <  32u) return 4;
    if (u <  64u) return 5;
    if (u < 128u) return 6;
    if (u < 256u) return 7;
    if (u < 512u) return 8;
    return 0;
}

void ClassicPhaserPlugin::updateCoefficients()
{
    const double sr = (double)fSampleRate;
    if (sr <= 0.0) return;

    // ── Normalised parameters (the DLL's 0..1 storage) ──────────────────────
    const float stN = std::clamp((fParams[kParamStages] - 4.0f) / 8.0f, 0.0f, 1.0f);
    float upN = (float)(std::log((double)fParams[kParamUpper] / 50.0)
                        / std::log(kUpperBase));                 // 50·400^up = Hz
    float rtN = (float)(std::log((double)fParams[kParamRate] / 0.1)
                        / std::log(kRateBase));                  // 0.1·100^rt = Hz
    upN = std::clamp(upN, 0.0f, 1.0f);
    rtN = std::clamp(rtN, 0.0f, 1.0f);
    const float dpN = std::clamp(fParams[kParamDepth]    * 0.01f, 0.0f, 1.0f);
    const float fbN = std::clamp(fParams[kParamFeedback] * 0.01f, 0.0f, 1.0f);
    const float mxN = std::clamp(fParams[kParamMix]      * 0.01f, 0.0f, 1.0f);
    const float lvN = std::clamp((fParams[kParamLevel] + 24.0f) / 30.0f, 0.0f, 1.0f);

    // ── LFO rate: free-run vs. tempo sync ──────────────────────────────────
    // rate = 0.1 * 100^RateNorm                      (0x483D64..0x483D83)
    float rate = (float)(kRateScale * std::exp(std::log(kRateBase) * (double)rtN));

    if (fParams[kParamSync] > kSwitchThresh)
    {
        const TimePosition& tp = getTimePosition();
        // The original calls a virtual method with EDX=0x400 (request
        // kVSTTempoValid) and gates on flags & 0x400.  DPF equivalent: a
        // valid BBT carrying beatsPerMinute.
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0)
        {
            const double tempo = (double)tp.bbt.beatsPerMinute;   // [esi+0x20]

            // u = (int)(768 / tempo), bucketed on its low 16 bits
            const unsigned u = (unsigned)(int)std::lrint((double)kSyncTempoDiv / tempo)
                               & 0xffffu;

            // di = (int)(2 ^ (int)(RateNorm * 8))
            const int n  = (int)std::lrint((double)rtN * (double)kSyncRateMul);
            const int di = (int)std::lrint(std::pow(2.0, (double)n));

            // syncVal = (uint16)(di << bucket) — the original stores a word,
            // so the 65536 → 0 overflow at Rate=1/high bucket is preserved.
            const int syncVal = (int)(uint16_t)((unsigned)di << syncBucketShift(u));

            // sync rateHz = syncVal * tempo / 7680    (0x483EF2..0x483F13)
            rate = (float)((double)syncVal * tempo / (double)kSyncDivisor);

            // Phase seed = increment × samplePos: the LFO locks to the song
            // position whenever the tempo-derived value changes (0x483F14..).
            const double seedD = ((kTwoPow32 * (double)rate) * (double)tp.frame) / sr;
            const int seed = (int)(int64_t)std::llrint(seedD);
            if (seed != fPhaseSeedCache)
                fPhase = (uint32_t)seed;
            fPhaseSeedCache = seed;
        }
        // No tempo info → keep the free-run rate ([esp] is left untouched in
        // the original as well).
    }

    // Phase increment: (int)(2^32 * rate / sr)        (0x483F3E..0x483F52)
    fPhaseInc = (int)(int64_t)std::llrint((kTwoPow32 * (double)rate) / sr);

    // ── Sweep endpoints ────────────────────────────────────────────────────
    // fHigh = 50 * 400^UpperNorm                      (0x483F58..0x483F75)
    const float fHigh = (float)(kFreqScale * std::exp(std::log(kUpperBase) * (double)upN));

    // fB = 100π · (fHigh/50)^(1−Depth)   →  its tan argument is
    // π · [50·(400^Upper)^(1−Depth)] / sr = the sweep's lower limit.
    // (the original recomputes 400^Upper as the float fHigh / 50 first)
    const double ratio = (double)fHigh / (double)kFreqScale;
    const float  fB    = (float)(kPiTimes100
                                 * std::exp((1.0 - (double)dpN) * std::log(ratio)));
    const float  fA2   = (float)(kTwoPi * (double)fHigh);

    // tan computed as sin/cos — the original calls fcos@0x402984 and
    // fsin@0x4029ac separately, then divides sin/cos (0x483FB9..0x484030).
    const double argLow  = ((double)fB  * 0.5) / sr;
    const double argHigh = ((double)fA2 * 0.5) / sr;
    fTanLow  = (float)(std::sin(argLow)  / std::cos(argLow));   // 0x118
    fTanHigh = (float)(std::sin(argHigh) / std::cos(argHigh));  // 0x11C

    // ── Stage count: 2·(int)(4·StagesNorm) + 4       (0x484031..0x484048)
    fStages = 2 * (int)std::lrint((double)kStagesMul * (double)stN) + 4;

    // ── Feedback: 0.98 * FeedbackNorm                (0x48404F..0x484061)
    fFeedbackGain = (float)(kFeedbackScale * (double)fbN);

    // ── Mix + Level (0x484062..0x4840C1):
    //   lvlGain = 31.6226^LevelNorm
    //   wet     = 0.063096 · Mix       · lvlGain
    //   dry     = 0.063096 · (1 − Mix) · lvlGain          (≈ −24 dB + 30·L dB)
    const float lvlGain = (float)std::exp(std::log(kLevelBase) * (double)lvN);
    fWetGain = (float)(kMixScale * (double)mxN * (double)lvlGain);
    fDryGain = (float)(kMixScale * (1.0 - (double)mxN) * (double)lvlGain);
}


