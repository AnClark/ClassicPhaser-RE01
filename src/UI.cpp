#include "UI.h"

#include "CenteredSeparatorText.hpp"
#include "imgui-knobs.h"
#include "HardwareButton.hpp"

#include "config.h"

// -----------------------------------------------------------------------
// Configurations

// -----------------------------------------------------------------------
// Scale marks for the Phaser knobs (values live in parameter space)

// Stages knob: _addKnob() runs this knob in HALF-stage space (shadow value
// in [2,6] with integer dragging — see the Stages special case there), so the
// mark values are half-stages while the labels show the real stage count.
// Each integer detent = a step of 2 in parameter space -> {4,6,8,10,12}.
static const ImGuiKnobs_Mod::KnobScaleMark kStagesMarks[] = {
    {   2.0f, "4"    },
    {   3.0f, "6"    },
    {   4.0f, "8"    },
    {   5.0f, "10"   },
    {   6.0f, "12"   },
};

static const ImGuiKnobs_Mod::KnobScaleMark kUpperMarks[] = {
    {   50.0f, "50"   },
    {  100.0f, nullptr },
    {  200.0f, nullptr },
    {  500.0f, nullptr },
    { 1000.0f, "1k"   },
    { 2000.0f, nullptr },
    { 5000.0f, nullptr },
    {10000.0f, nullptr },
    {20000.0f, "20k"  },
};

// 11 marks for the Rate knob: geometrically spaced (equal UI distance on
// the log-mapped knob), symmetric around 1.0 — 5 marks per side,
// ratio 10^0.2 per step; each left mark is the reciprocal of its
// right-side counterpart (0.1×10 = ⋯ = 0.631×1.585 = 1).
static const ImGuiKnobs_Mod::KnobScaleMark kRateMarks[] = {
    {  0.1f,        "0.1" },
    {  0.15848932f, nullptr },
    {  0.25118864f, nullptr },
    {  0.39810717f, nullptr },
    {  0.63095734f, nullptr },
    {  1.0f,        "1.0" },
    {  1.58489319f, nullptr },
    {  2.51188643f, nullptr },
    {  3.98107171f, nullptr },
    {  6.30957344f, nullptr },
    { 10.0f,        "10"  },
};

static const ImGuiKnobs_Mod::KnobScaleMark kSwitchMarks[] = {
    { 0.0f, "OFF" },
    { 1.0f, "ON"  },
};

static const ImGuiKnobs_Mod::KnobScaleMark kPercentMarks[] = {
    {   0.0f, "0" },
    { 10.0f, nullptr },
    {  20.0f, nullptr },
    {  30.0f, nullptr },
    {  40.0f, nullptr },
    {  50.0f, "50" },
    {  60.0f, nullptr },
    {  70.0f, nullptr },
    {  80.0f, nullptr },
    {  90.0f, nullptr },
    { 100.0f, "100" },
};

static const ImGuiKnobs_Mod::KnobScaleMark kMixMarks[] = {
    {   0.0f, "DIR." },
    {  12.5f, nullptr },
    {  25.0f, nullptr },
    {  37.5f, nullptr },
    {  50.0f, "1:1" },
    {  62.5f, nullptr },
    {  75.0f, nullptr },
    {  87.5f, nullptr },
    { 100.0f, "EFF." },
};

static const ImGuiKnobs_Mod::KnobScaleMark kLevelMarks[] = {
    { -24.0f, "-24" },
    { -21.0f, nullptr },
    { -18.0f, nullptr },
    { -15.0f, nullptr },
    { -12.0f, nullptr },
    { -9.0f, "-9" },
    {  -6.0f, nullptr },
    {  -3.0f, nullptr },
    {   0.0f, nullptr  },
    {   3.0f, nullptr },
    {   6.0f, "+6"   },
};

// -----------------------------------------------------------------------
// Constructor and UI callbacks

ClassicPhaserUI::ClassicPhaserUI()
    : DISTRHO::UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
{
    // Fetch current scale factor
    this->fScaleFactor = getScaleFactor();

    // Initialize parameters to default values (optional)
    std::memset(fParams, 0, sizeof(fParams));

    // Initialize preset manager and load persisted user presets from disk
    fPresetManager = new PresetManager(this);
    const bool presetsLoaded = fPresetManager->loadUserPresetsFromDisk();
    if (!presetsLoaded) {
        // NOTE: _showMessageBox() can be used here because it pushes the message into a queue and doesn't require an active ImGui context at this point.
        //       The message will be displayed as a popup when the UI is rendered.
        //       @see _showMessageBox() and _handleMessageBoxIdle() in UI.h/UI.cpp
        _showMessageBox("WARNING: could not load user presets from disk. Presets will not be saved.");
    }

    // Load fonts for ImGui
    _loadFonts();

    // Set the flag to track if the "About" window is open
    fAboutWindowOpened = false;
}

void ClassicPhaserUI::parameterChanged(uint32_t index, float value)
{
    DISTRHO_SAFE_ASSERT_RETURN(index < kParamCount, )

    fParams[index] = value;
}

void ClassicPhaserUI::onImGuiDisplay()
{
    const float margin  = SCALE(4.0f); //* scale;

    // ── Main viewport (fullscreen, no decoration) ────────────────────────────
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    static constexpr auto window_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollWithMouse;

    // White background for the host window
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::Begin("Main Window", nullptr, window_flags))
    {
        const float  rounding = SCALE(10.0f); //* scale;
        const ImVec2 winSize = ImGui::GetWindowSize();

        // ── Draw the plugin chassis background directly onto the main viewport ──
        _drawChassisBackground(margin, rounding);

        // ── Child window (transparent overlay for placing controls) ──────────
        ImGui::SetCursorPos(ImVec2(margin, margin));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);

        const ImVec2 childSize = ImVec2(winSize.x - 2.0f * margin, winSize.y - 2.0f * margin);
        if (ImGui::BeginChild("BackgroundPanel", childSize, false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse))
        {
            // ── UI controls will go here ─────────────────────────────────────
            
            // Left margin
            ImGui::Dummy(ImVec2(SCALE(2), 0));
            ImGui::SameLine();

            if (_BeginSection("FILTER", SCALE(80.0f * 2)))
            {
                // Add an extra left margin to the first knob so its leftmost scale mark doesn't get cut off.
                ImGui::Dummy(ImVec2(SCALE(8), 0));
                ImGui::SameLine();

                // NOTICE: "Stages" has ONLY 5 possible values - 4, 6, 8, 10, 12 (the DSP
                //         quantises in FUN_00483d5c). _addKnob() drags a half-stage shadow
                //         value in [2,6] with integer rounding so each detent is one step
                //         of 2; the format passed here is overridden to "%.0f" there.
                _addKnob(kParamStages, "STAGES", kStagesMarks, IM_ARRAYSIZE(kStagesMarks), false, false, 0.0f, "%.0f",
                        IMGUIKNOBS_PI * (200.0f / 180.0f),
                        IMGUIKNOBS_PI * (340.0f / 180.0f)
                    );

                ImGui::SameLine(0, SCALE(30 + 4));
                _addKnob(kParamUpper, "UPPER (Hz)", kUpperMarks, IM_ARRAYSIZE(kUpperMarks), true, false, 0.0f, "%.2f");

                _EndSection();
            }

            ImGui::SameLine(0, SCALE(10));

            if (_BeginSection("MODULATION", SCALE((80.0f - 8.0f) * 5 - 2.0f)))
            {
                // Add an extra left margin
                ImGui::Dummy(ImVec2(SCALE(2), 0));
                ImGui::SameLine();

                // NOTICE: Use "%.2f" format (acts as accuracy hint) for the logarithmic Delay knob so the 0.1..1.0 ms range has
                //         enough quantization steps to feel smooth while dragging.
                //         See _addKnob() for more details.
                _addKnob(kParamRate, "RATE (Hz)", kRateMarks, IM_ARRAYSIZE(kRateMarks), true, false, 0.0f, "%.2f");

                ImGui::SameLine(0, SCALE(20));
                _addBinaryStateSwitch(kParamSync, "   SYNC", "OFF", "HOST", SCALE(2.0f), SCALE(0.5f));

                ImGui::SameLine(0, SCALE(20));
                _addKnob(kParamDepth, "  DEPTH (%)", kPercentMarks, IM_ARRAYSIZE(kPercentMarks));


                ImGui::SameLine(0.0f, SCALE(20));
                _addBinaryStateSwitch(kParamSpread, " SPREAD", "OFF", "ON", SCALE(6.0f), SCALE(1.0f));

                ImGui::SameLine(0.0f, SCALE(20));
                _addKnob(kParamFeedback, "  FEEDBACK (%)", kPercentMarks, IM_ARRAYSIZE(kPercentMarks));

                _EndSection();
            }

            ImGui::SameLine(0, SCALE(10));

            if (_BeginSection("OUTPUT", SCALE((80.0f - 4.0f) * 2)))
            {
                // Add an extra left margin
                ImGui::Dummy(ImVec2(SCALE(2 + 2), 0));
                ImGui::SameLine();

                _addKnob(kParamMix, "MIX", kMixMarks, IM_ARRAYSIZE(kMixMarks));

                ImGui::SameLine(0, SCALE(30));
                _addKnob(kParamLevel, "LEVEL (dB)", kLevelMarks, IM_ARRAYSIZE(kLevelMarks), false, false, 0.0f, "%.2f");

                _EndSection();
            }

            ImGui::SameLine(0, SCALE(10.0f + 2.0f));

            // Right panel (Logo, config buttons, etc.)
            {
                ImGui::BeginGroup();

                // Add an extra top margin
                ImGui::Dummy(ImVec2(0, SCALE(2)));

                _drawKjaerhusLogo(ImVec2(SCALE(100), SCALE(50)));

#if 1   // Extra controls.
                // Preset button — label shows the current preset name
                {
                    ImGui::BeginGroup();
                    ImGui::AlignTextToFramePadding();

                    ImGui::Dummy(ImVec2(SCALE(2), 0));
                    ImGui::SameLine(0.0f, 0.0f);

                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);   // Use the smaller font for the preset button

                    {
                        const Preset* curPreset = fPresetManager->currentPreset();
                        std::string   btnLabel;
                        if (curPreset) {
                            btnLabel = curPreset->name;
                            if (fPresetManager->isModified()) btnLabel += " *";
                        } else {
                            btnLabel = "Select Preset...";
                        }
                        btnLabel += "##Preset";
                        if (ImGuiExt::HardwareButton(btnLabel.c_str(),
                                           ImVec2(SCALE(100 - 3), ImGui::GetFrameHeight()),
                                           ImVec4(0x2f / 255.0f, 0x4d / 255.0f, 0x44 / 255.0f, 1.0f)))
                        {
                            fPresetManagerOpened = !fPresetManagerOpened;
                        }
                    }
                    ImGui::SameLine(0.0f, SCALE(5.0f));
                    ImGui::Text("PRESET");

                    ImGui::PopFont();

                    ImGui::EndGroup();
                }

                ImGui::Dummy(ImVec2(0, SCALE(0.5f)));

#else   // No extra controls, just a placeholder
                ImGui::Dummy(ImVec2(0, SCALE(23)));     // TODO: This is a placeholder. I will add extra controls here in future.
#endif

                _drawPluginName();

                ImGui::EndGroup();
            }

        }
        ImGui::EndChild();

        ImGui::PopStyleVar();   // ChildRounding
        ImGui::PopStyleColor(); // ChildBg

        ImGui::End();
    }
    ImGui::PopStyleColor(); // WindowBg

    // Update the OS mouse cursor based on the current ImGui mouse cursor state
    _UpdateMouseCursor();

    // Poll the native file browser dialog (Import/Export). Must be called every frame.
    _handleFileBrowserIdle();

    // Draw the preset manager overlay (renders nothing when fPresetManagerOpened == false)
    _drawPresetManager();

    // Draw the About window overlay (renders nothing when fAboutWindowOpened == false)
    _drawAboutWindow();

    // Handle message box display
    _handleMessageBoxIdle();
}

// -----------------------------------------------------------------------
// State callbacks

void ClassicPhaserUI::stateChanged(const char* key, const char* value)
{
    // Buffer each restored value; rebuild state after all three arrive.
    if (std::strcmp(key, STATE_PRESET_TYPE) == 0)
        fRestoredPresetType = value;
    else if (std::strcmp(key, STATE_PRESET_NAME) == 0)
        fRestoredPresetName = value;
    else if (std::strcmp(key, STATE_PRESET_MODIFIED) == 0)
        fRestoredModified = (std::strcmp(value, "true") == 0);

    _applyRestoredPresetState();
}

// -----------------------------------------------------------------------

START_NAMESPACE_DISTRHO

UI* createUI()
{
    return new ClassicPhaserUI();
}

END_NAMESPACE_DISTRHO
