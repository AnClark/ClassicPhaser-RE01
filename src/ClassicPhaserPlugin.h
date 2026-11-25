#ifndef CLASSIC_PHASER_PLUGIN_H_INCLUDED
#define CLASSIC_PHASER_PLUGIN_H_INCLUDED

#include "DistrhoPlugin.hpp"
#include "Structures.h"
#include "Defines.h"
#include "config.h"

// ─────────────────────────────────────────────────────────────────────────────
// Classic Phaser plugin class — pixel-faithful re-implementation of the
// original Kjaerhus Classic Phaser DSP:
//   FUN_00483d5c  parameter → coefficient (once per block)
//   FUN_00484158  per-sample allpass/LFO core
// ─────────────────────────────────────────────────────────────────────────────
class ClassicPhaserPlugin : public DISTRHO::Plugin
{
public:
    ClassicPhaserPlugin();

protected:
    // ── Plugin metadata ────────────────────────────────────────────────────
    const char* getLabel()   const override { return DISTRHO_PLUGIN_NAME; }
    const char* getMaker()   const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getLicense() const override { return "GPLv3+"; }
    uint32_t    getVersion() const override { return d_version(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH); }

    // ── Parameters ────────────────────────────────────────────────────────
    void initParameter(uint32_t index, Parameter& param) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

    // ── State ──────────────────────────────────────────────────────────────
    void initState(uint32_t index, State& state) override;
    void setState(const char* key, const char* value) override;

    // ── Audio processing ──────────────────────────────────────────────────
    void activate() override;
    void deactivate() override;
    void sampleRateChanged(double newSampleRate) override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

    // ── DSP state ──────────────────────────────────────────────────────────
    // Protected (rather than private) so that tests/dsp_behavior_test.cpp can
    // inspect internal state through a Harness subclass.  Byte offsets in the
    // comments refer to the original DLL's state struct.

    float fParams[kParamCount];
    float fSampleRate = 44100.0f;

    // Coefficient block (FUN_00483d5c), recomputed at every block start
    float fWetGain      = 0.0f;   // 0x104   wet mix gain
    float fDryGain      = 0.0f;   // 0x108   dry mix gain
    float fFeedbackGain = 0.0f;   // 0x120   0.98 * FeedbackNorm
    int   fPhaseInc     = 0;      // 0xE8    (int)(2^32 * rate / sr)
    int   fStages       = 4;      // 0xF0    4..12 allpass stages
    float fTanLow       = 0.0f;   // 0x118   tan(π · fLow  / sr)
    float fTanHigh      = 0.0f;   // 0x11C   tan(π · fHigh / sr)

    // Per-sample state (FUN_00484158)
    float    fChainL = 0.0f, fChainR = 0.0f;              // 0x110 / 0x10C
    float    fDelayL[kMaxStages] = {};                    // 0x148.. (12 slots)
    float    fDelayR[kMaxStages] = {};                    // 0x178.. (12 slots)
    uint16_t fLfoStep  = 0;                               // 0xF4    0..19
    float    fLfoPrev  = 0.0f, fLfoCur = 0.0f;            // 0x128 / 0x12C
    uint32_t fPhase    = 0;                               // 0x1A8   phase acc.
    int      fPhaseSeedCache = 0;                         // 0x1AC   tempo seed

private:
    // ── Internal helpers ────────────────────────────────────────────────────

    // ── State reset ─────────────────────────────────────────────────────────
    void clearDspState();

    // ── Coefficient update ────────────────────────────────────────────────
    // Faithful transcription of FUN_00483d5c.  Called at the start of every
    // run() block — the original calls it once per processReplacing(), so
    // parameter changes take effect with block granularity, exactly like the
    // original (setParameter does NOT recompute coefficients mid-block).
    void updateCoefficients();

    // ── RandExt (0x402C08) input dither ────────────────────────────────────
    //   state = state * 0x08088405 + 1        (uint32, initial state 0)
    //   rand  = state * 2^-32                 (double @ 0x486068)
    //   noise = kModAmp * (rand - 0.5)        (kModAmp @ 0x4844c8, float80)
    uint32_t fRngState = 0u;

    inline float nextNoise() noexcept
    {
        fRngState = fRngState * kRngMul + 1u;
        const double rnd = (double)fRngState * kRngScale;
        return (float)((rnd - (double)kHalfF) * kModAmp);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassicPhaserPlugin)
};

#endif // CLASSIC_PHASER_PLUGIN_H_INCLUDED
