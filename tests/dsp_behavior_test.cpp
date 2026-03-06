// Standalone behavioural test for Classic Phaser RE-01's faithful DSP.
// Compiles the real plugin sources and exercises run() through a harness.
//
// Build & run (from the repository root, after configuring build/):
//   c++ -std=c++17 -O1 -I src -I dpf/distrho -I build \
//       tests/dsp_behavior_test.cpp src/CoreDSP.cpp src/ClassicPhaserPlugin.cpp \
//       dpf/distrho/src/DistrhoPlugin.cpp -o dsp_behavior_test \
//     && ./dsp_behavior_test
#include "ClassicPhaserPlugin.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

static constexpr double kPi = 3.14159265358979323846;

// The DSP state lives in the protected section so the harness can inspect it.
class Harness : public ClassicPhaserPlugin
{
public:
    using ClassicPhaserPlugin::run;
    using ClassicPhaserPlugin::setParameterValue;
    using ClassicPhaserPlugin::getParameterValue;
    using ClassicPhaserPlugin::sampleRateChanged;
    using ClassicPhaserPlugin::activate;

    using ClassicPhaserPlugin::fStages;
    using ClassicPhaserPlugin::fPhaseInc;
    using ClassicPhaserPlugin::fWetGain;
    using ClassicPhaserPlugin::fDryGain;
    using ClassicPhaserPlugin::fTanLow;
    using ClassicPhaserPlugin::fTanHigh;
    using ClassicPhaserPlugin::fPhase;
    using ClassicPhaserPlugin::fLfoStep;
    using ClassicPhaserPlugin::fLfoCur;
};

static int gFail = 0;
#define CHECK(cond, msg, ...)                                    \
    do {                                                         \
        if (cond) { printf("PASS: " msg "\n", ##__VA_ARGS__); }  \
        else      { printf("FAIL: " msg "\n", ##__VA_ARGS__); ++gFail; } \
    } while (0)

// Process a whole buffer in 64-sample blocks; returns the peak |sample|.
static float process(Harness& p, const std::vector<float>& inL,
                     const std::vector<float>& inR,
                     std::vector<float>& outL, std::vector<float>& outR)
{
    outL.assign(inL.size(), 0.0f);
    outR.assign(inR.size(), 0.0f);
    float peak = 0.0f;
    for (size_t i = 0; i < inL.size(); i += 64)
    {
        const uint32_t n = (uint32_t)std::min<size_t>(64, inL.size() - i);
        const float* ins[2]  = { inL.data() + i, inR.data() + i };
        float*       outs[2] = { outL.data() + i, outR.data() + i };
        p.run(ins, outs, n);
        for (uint32_t k = 0; k < n; ++k)
            peak = std::max(peak, std::max(std::fabs(outL[i + k]), std::fabs(outR[i + k])));
    }
    return peak;
}

// Output stage replica: the optional tanh soft clip — bit-identical to
// run()'s final expression.
static float outShape(float x)
{
#if CLASSIC_PHASER_OUTPUT_SOFT_CLIP
    return kSoftClipCeiling * std::tanh(x * kSoftClipCeilingInv);
#else
    return x;
#endif
}

int main()
{
    const double sr = 44100.0;

    // ── 1. Stage count: 2·(int)(4·norm) + 4 → {4,6,8,10,12} ────────────────
    {
        struct { float stages; int expect; } cases[] = {
            { 4.0f, 4 }, { 5.0f, 4 }, { 6.0f, 6 }, { 8.0f, 8 },
            { 10.0f, 10 }, { 12.0f, 12 }, { 4.999f, 4 }, { 5.001f, 6 },
        };
        bool ok = true;
        for (auto& c : cases)
        {
            Harness p;
            p.sampleRateChanged(sr);
            p.setParameterValue(kParamStages, c.stages);
            p.sampleRateChanged(sr);            // re-primes the coefficient block
            if (p.fStages != c.expect) {
                printf("  stages(%g) = %d, want %d\n", (double)c.stages, p.fStages, c.expect);
                ok = false;
            }
        }
        CHECK(ok, "stage count mapping 2·(int)(4·norm)+4 -> {4..12}");
    }

    // ── 2. Sweep endpoints: tan(π·f/sr); Depth=0 → both ends equal ─────────
    {
        Harness p;
        p.sampleRateChanged(sr);
        p.setParameterValue(kParamUpper, 2000.0f);
        p.setParameterValue(kParamDepth, 0.0f);
        p.sampleRateChanged(sr);

        const double arg = kPi * 2000.0 / sr;
        const float  want = (float)(std::sin(arg) / std::cos(arg));
        CHECK(std::fabs(p.fTanHigh - want) <= 1e-6f * std::max(1.0f, std::fabs(want)),
              "tanHigh = tan(pi*2000/44100) = %.8f (got %.8f)", (double)want, (double)p.fTanHigh);
        CHECK(std::fabs(p.fTanLow - p.fTanHigh) <= 1e-6f * std::max(1.0f, std::fabs(want)),
              "Depth=0 collapses the sweep (tanLow == tanHigh)");

        // Depth=100% → fLow = 50 Hz
        p.setParameterValue(kParamDepth, 100.0f);
        p.sampleRateChanged(sr);
        const double argLo = kPi * 50.0 / sr;
        const float  wantLo = (float)(std::sin(argLo) / std::cos(argLo));
        CHECK(std::fabs(p.fTanLow - wantLo) <= 1e-6f,
              "Depth=100 lowers the sweep end to 50 Hz (tanLow = %.8f, want %.8f)",
              (double)p.fTanLow, (double)wantLo);
        CHECK(p.fTanLow < p.fTanHigh, "tanLow < tanHigh for a positive sweep range");
    }

    // ── 3. Dry path (Mix=0): out = softclip(in · dryGain) ──────────────────
    {
        Harness p;
        p.sampleRateChanged(sr);
        p.setParameterValue(kParamMix, 0.0f);
        p.setParameterValue(kParamLevel, 0.0f);   // lvN = 0.8
        p.sampleRateChanged(sr);

        // dryGain = 0.063096 · (1−0) · 31.6226^0.8
        const float wantGain = (float)(kMixScale * std::exp(std::log(kLevelBase) * 0.8));
        CHECK(p.fDryGain == wantGain && p.fWetGain == 0.0f,
              "Mix=0 → wetGain=0, dryGain=%.9f (got %.9f)",
              (double)wantGain, (double)p.fDryGain);

        const int N = 1024;
        std::vector<float> inL(N), inR(N), outL, outR;
        for (int i = 0; i < N; ++i) { inL[i] = 0.01f * (float)i - 0.5f; inR[i] = -inL[i]; }
        process(p, inL, inR, outL, outR);

        int mism = 0;
        for (int i = 0; i < N; ++i)
            if (outL[i] != outShape(inL[i] * p.fDryGain) ||
                outR[i] != outShape(inR[i] * p.fDryGain)) ++mism;
        CHECK(mism == 0, "Mix=0 = exact dry path (%d mismatches)", mism);
    }

    // ── 4. Full-wet DC: cascade of even allpass stages has DC gain +1 ──────
    {
        Harness p;
        p.sampleRateChanged(sr);
        p.setParameterValue(kParamMix, 100.0f);
        p.setParameterValue(kParamFeedback, 0.0f);
        p.setParameterValue(kParamDepth, 0.0f);
        p.setParameterValue(kParamUpper, 2000.0f);
        p.sampleRateChanged(sr);

        const int N = 512;
        std::vector<float> inL(N, 1.0f), inR(N, 1.0f), outL, outR;
        process(p, inL, inR, outL, outR);

        const float want = outShape(p.fWetGain * 1.0f);   // DC chain gain = +1
        CHECK(std::fabs(outL[N - 1] - want) < 1e-4f && std::fabs(outR[N - 1] - want) < 1e-4f,
              "full-wet DC gain = 1 after settling (got %.6f, want %.6f)",
              (double)outL[N - 1], (double)want);
    }

    // ── 5. Phase increment + LFO period at Rate = 1 Hz ─────────────────────
    {
        Harness p;
        p.sampleRateChanged(sr);
        p.setParameterValue(kParamRate, 1.0f);       // RateNorm = 0.5 → 1.0 Hz
        p.setParameterValue(kParamSync, 0.0f);
        p.sampleRateChanged(sr);

        const int wantInc = (int)std::llrint(4294967296.0 / sr);   // 97308
        CHECK(p.fPhaseInc == wantInc,
              "phase increment at 1 Hz = %d (got %d)", wantInc, p.fPhaseInc);

        // Count 0.5-crossings of fLfoCur over 3 seconds → ≈ 2 per period.
        const int N = (int)(sr * 3.0);
        std::vector<float> inL(1, 0.0f), inR(1, 0.0f);
        int crossings = 0;
        float prev = p.fLfoCur - 0.5f;
        for (int i = 0; i < N; ++i)
        {
            const float* ins[2]  = { inL.data(), inR.data() };
            float        tmpL[1], tmpR[1];
            float*       outs[2] = { tmpL, tmpR };
            p.run(ins, outs, 1);
            const float cur = p.fLfoCur - 0.5f;
            if ((prev < 0.0f && cur >= 0.0f) || (prev > 0.0f && cur <= 0.0f))
                ++crossings;
            prev = cur;
        }
        CHECK(crossings >= 5 && crossings <= 7,
              "LFO at 1 Hz crosses 0.5 six times in 3 s (got %d)", crossings);
    }

    // ── 6. Sync=1 without host tempo behaves exactly like free-run ─────────
    {
        Harness a, b;
        a.sampleRateChanged(sr); b.sampleRateChanged(sr);
        a.setParameterValue(kParamRate, 2.0f);
        b.setParameterValue(kParamRate, 2.0f);
        b.setParameterValue(kParamSync, 1.0f);       // no host BBT in the test
        a.sampleRateChanged(sr); b.sampleRateChanged(sr);

        CHECK(a.fPhaseInc == b.fPhaseInc && a.fPhaseInc != 0,
              "Sync without BBT keeps the free-run increment (%d)", b.fPhaseInc);

        // Both must produce identical output for identical input.
        const int N = 2048;
        std::vector<float> inL(N), inR(N), oL1, oR1, oL2, oR2;
        for (int i = 0; i < N; ++i) { inL[i] = std::sin(0.05 * i); inR[i] = std::cos(0.03 * i); }
        process(a, inL, inR, oL1, oR1);
        process(b, inL, inR, oL2, oR2);
        int mism = 0;
        for (int i = 0; i < N; ++i)
            if (oL1[i] != oL2[i] || oR1[i] != oR2[i]) ++mism;
        CHECK(mism == 0, "Sync=1 without tempo == free-run output (%d mismatches)", mism);
    }

    // ── 7. Level mapping: −24 dB → wetGain = 0.063096·Mix ──────────────────
    {
        Harness p;
        p.sampleRateChanged(sr);
        p.setParameterValue(kParamMix, 100.0f);
        p.setParameterValue(kParamLevel, -24.0f);    // lvN = 0 → lvlGain = 1
        p.sampleRateChanged(sr);
        CHECK(std::fabs(p.fWetGain - (float)kMixScale) < 1e-7f,
              "Level=-24 dB → wetGain = kMixScale = %.7f (got %.7f)",
              (double)kMixScale, (double)p.fWetGain);

        p.setParameterValue(kParamLevel, 6.0f);      // lvN = 1 → lvlGain = 31.6226
        p.sampleRateChanged(sr);
        const float want6 = (float)(kMixScale * kLevelBase);
        CHECK(std::fabs(p.fWetGain - want6) < 1e-5f,
              "Level=+6 dB → wetGain ≈ %.6f (got %.6f)", (double)want6, (double)p.fWetGain);

        // Zero input + zero state stays silent (feedback cannot self-start).
        const int N = 64;
        std::vector<float> inL(N, 0.0f), inR(N, 0.0f), outL, outR;
        process(p, inL, inR, outL, outR);
        bool quiet = true;
        for (int i = 0; i < N; ++i)
            if (std::fabs(outL[i]) > 1e-6f || std::fabs(outR[i]) > 1e-6f) quiet = false;
        CHECK(quiet, "zero input + zero state stays silent (no self-started oscillation)");
    }

    printf("\n%s (%d failure%s)\n", gFail == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           gFail, gFail == 1 ? "" : "s");
    return gFail == 0 ? 0 : 1;
}
