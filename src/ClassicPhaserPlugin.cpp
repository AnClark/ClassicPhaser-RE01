#include "ClassicPhaserPlugin.h"
#include "Defines.h"

#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Classic Phaser plugin class
// ─────────────────────────────────────────────────────────────────────────────

ClassicPhaserPlugin::ClassicPhaserPlugin()
    : DISTRHO::Plugin(kParamCount, 0, 3)  // 3 states: preset_name, preset_modified, preset_type
{
    // Default parameter values – physical units matching initParameter() ranges
    for (uint32_t i = 0; i < kParamCount; ++i)
        fParams[i] = kParamRanges[i].def;

    // Call sampleRateChanged() to reset the DSP state and prime the coefficient
    // block based on the initial sample rate, so the plugin is ready to process
    // audio immediately after construction.
    sampleRateChanged(getSampleRate());
}

// ── Parameters ────────────────────────────────────────────────────────
// Names/symbols follow getParamName() of the original DLL (indices 0..8):
//   Stages, Upper, Rate, Sync., Depth, Spread, Feedback, Mix, Level
void ClassicPhaserPlugin::initParameter(uint32_t index, Parameter& param)
{
    param.hints = kParameterIsAutomatable;
    param.ranges = DISTRHO::ParameterRanges(kParamRanges[index]);

    switch (index)
    {
    case kParamStages:
        param.name = "Stages";
        param.symbol = "stages";
        // Discrete stage count: the original maps norm → 2·(int)(4·norm)+4
        param.hints |= kParameterIsInteger;
        break;
    case kParamUpper:
        param.name = "Upper";
        param.symbol = "upper";
        param.unit  = "Hz";
        param.hints |= kParameterIsLogarithmic;
        break;
    case kParamRate:
        param.name = "Rate";
        param.symbol = "rate";
        param.unit  = "Hz";
        param.hints |= kParameterIsLogarithmic;
        break;
    case kParamSync:
        param.name = "Sync.";
        param.symbol = "sync";
        param.hints |= kParameterIsBoolean;
        break;
    case kParamDepth:
        param.name = "Depth";
        param.symbol = "depth";
        param.unit  = "%";
        break;
    case kParamSpread:
        param.name = "Spread";
        param.symbol = "spread";
        param.hints |= kParameterIsBoolean;
        break;
    case kParamFeedback:
        param.name = "Feedback";
        param.symbol = "feedback";
        param.unit  = "%";
        break;
    case kParamMix:
        param.name = "Mix";
        param.symbol = "mix";
        param.unit  = "%";
        break;
    case kParamLevel:
        param.name = "Level";
        param.symbol = "level";
        param.unit  = "dB";
        break;
    }
}

float ClassicPhaserPlugin::getParameterValue(uint32_t index) const
{
    return fParams[index];
}

void ClassicPhaserPlugin::setParameterValue(uint32_t index, float value)
{
    // The original clamps to [0,1] in setParameter (0x483CD0) and does NOT
    // recompute coefficients — that happens once per processReplacing() block
    // in FUN_00483d5c.  run() does the same here.
    fParams[index] = std::clamp(value, kParamRanges[index].min,
                                        kParamRanges[index].max);

    // Special treat of Stages: it has ONLY 5 values - 4, 6, 8, 10, 12.
    // Any values beyond them should be normalized to the nearest value,
    // after initial clamp (above).
    if (index == kParamStages)
    {
        if (value > 4.0f && value < 6.0f) fParams[index] = 4.0f;
        if (value > 6.0f && value < 8.0f) fParams[index] = 6.0f;
        if (value > 8.0f && value < 10.0f) fParams[index] = 8.0f;
        if (value > 10.0f && value < 12.0f) fParams[index] = 10.0f;
    }
}

// ── State ────────────────────────────────────────────────────────────
void ClassicPhaserPlugin::initState(uint32_t index, State& state)
{
    state.hints = kStateIsHostWritable;

    switch (index)
    {
    case 0:
        state.key          = "preset_name";
        state.defaultValue = "";
        state.label        = "Current Preset Name";
        break;
    case 1:
        state.key          = "preset_modified";
        state.defaultValue = "false";
        state.label        = "Preset Modified";
        break;
    case 2:
        state.key          = "preset_type";
        state.defaultValue = "Factory";
        state.label        = "Preset Type";
        break;
    default:
        break;
    }
}

void ClassicPhaserPlugin::setState(const char* /*key*/, const char* /*value*/)
{
    // Preset state is managed by the UI; the DSP side does not need to act on it.
    // DPF will forward state changes to the UI via stateChanged() automatically.
}

// ── Audio processing ──────────────────────────────────────────────────
void ClassicPhaserPlugin::activate()
{
    // NOTICE: No need to invoke sampleRateChanged() here to reset the DSP.
    //         Hosts are expected to call sampleRateChanged() before processing
    //         and on sample rate changes.  If we invoked sampleRateChanged()
    //         here, we may hear a click on play start in hosts (for example,
    //         REAPER).
}

void ClassicPhaserPlugin::sampleRateChanged(double newSampleRate)
{
    fSampleRate = (float)newSampleRate;
    clearDspState();
    updateCoefficients();
}



// ── State reset ───────────────────────────────────────────────────────
// Clears the acoustic state only: chain accumulators, the 12+12 stage delay
// slots and the LFO smoothing counters.  The phase accumulator, the RandExt
// state and the tempo-seed cache are kept (they are position/transport
// related; the original never clears them either).
void ClassicPhaserPlugin::clearDspState()
{
    fChainL = fChainR = 0.0f;
    for (int s = 0; s < kMaxStages; ++s)
    {
        fDelayL[s] = 0.0f;
        fDelayR[s] = 0.0f;
    }
    fLfoStep = 0;
    fLfoPrev = fLfoCur = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

START_NAMESPACE_DISTRHO

Plugin* createPlugin()
{
    return new ClassicPhaserPlugin();
}

END_NAMESPACE_DISTRHO
