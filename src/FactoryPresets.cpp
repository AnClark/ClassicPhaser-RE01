#include "PresetManager.h"

// ── Factory presets ────────────────────────────────────────────────────────
// Name, Stages, Upper(Hz), Rate(Hz), Sync, Depth(%), Spread, Feedback(%),
// Mix(%), Level(dB)
//
// Tuned against the faithful DSP (FUN_00483d5c / FUN_00484158):
//   * sweep range  fHigh = 50·400^UpperNorm
//                  fLow  = 50·(400^UpperNorm)^(1−Depth)   (50 Hz at Depth=100)
//   * stage count  4/6/8/10/12 via the Stages knob
//   * feedback     gain = 0.98 · Feedback/100 (near self-oscillation at 100)
//   * output       = 0.063096 · Mix · 31.6226^((Level+24)/30)
//                    (≈ −24 dB + 30·Level dB)
//   * Sync=1       locks the LFO to the host tempo; the Rate knob then picks
//                  the division 2^(int)(8·RateNorm) → 1,2,4,…,128
//
// The bank opens with genre-inspired settings (one row each, with notes),
// then continues with the base presets in rough order of increasing
// intensity.
extern const Preset kFactoryPresets[] = {
    //  name             stages upper  rate  sync depth spread fb mix  lvl
    // ── Genre-inspired (front of bank) ──────────────────────────────────────────────────────────────
    // 70s rock phase: 4-stage (Phase 90 / Small Stone class), slow full sweep
    {"70s Stomp",          4,  2500,  0.45f,  0,  100,   0,   25, 50,  0.0f}, // guitar/bass, dry-ish classic
    // 70s disco swirl: wide shimmer on strings / clean guitar
    {"Disco Nights",       8,  3500,  0.70f,  0,   90,   1,   45, 45,  0.0f}, // Salsoul-style string sheen
    // J-Pop: bright, shallow, airy top-end movement
    {"Tokyo Shimmer",      8,  9000,  1.40f,  0,   45,   1,   25, 35,  0.0f}, // gentle sparkle on keys/arps
    // EDM: tempo-synced jet sweep for drops and build-ups
    {"Drop Sweep",        12,  5000,  1.00f,  1,  100,   1,   70, 55,  0.0f}, // Rate=1 Hz -> di=16, host-locked
    // Spatial / ASMR-style: very slow full-range wrap around the head
    {"Head Wrap",         12,  3000,  0.10f,  0,  100,   1,   30, 45,  0.0f}, // slow stereo circulation
    // Experimental: deep sweep with feedback at the edge of self-oscillation
    {"Warp Zone",         12,  7000,  2.00f,  0,  100,   1,   92, 50, -3.0f}, // intense, near-resonant
    // ── Base bank (increasing intensity) ─────────────────────────────────────────────────
    {"Soft Vertex",        4,  1500,  0.30f,  0,   60,   0,   40, 50,  0.0f}, // subtle 4-stage
    {"Classic 8",          8,  2000,  0.50f,  0,   75,   0,   50, 50,  0.0f}, // out-of-the-box default
    {"Vocal Air",          8,  3500,  0.15f,  0,   45,   1,   35, 45,  0.0f}, // gentle wide vocal phase
    {"Slow Bloom",         8,  1200,  0.10f,  0,  100,   0,   30, 60,  0.0f}, // slow deep wash, wet-heavy
    {"Deep Sweep",        12,  4000,  0.25f,  0,  100,   0,   55, 55,  0.0f}, // full-range 12-stage sweep
    {"Barbershop",         6,  1200,  0.80f,  0,  100,   1,   70, 50,  0.0f}, // spread + fast, vocal-ish
    {"Sync Drift",        12,  2500,  1.60f,  1,   80,   0,   60, 50,  0.0f}, // tempo-synced (Rate picks the division)
    {"Resonant Orbit",    10,  5000,  0.60f,  0,   90,   1,   85, 45, -3.0f}, // high feedback, near self-oscillation
    {"Jet Stream",        12,  8000,  5.00f,  1,  100,   1,   75, 55,  0.0f}, // fast sync jet with spread
};
extern const int kFactoryPresetsCount = (int)(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));
