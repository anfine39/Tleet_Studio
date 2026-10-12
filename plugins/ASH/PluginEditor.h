#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/LeverSwitch.h"
#include "ui/SkinVU.h"
#include "PluginProcessor.h"

namespace tleet::ash
{
    // VU meter source selection, like the hardware meter switch.
    //  GR : needle = 0 VU minus actual gain reduction of the optical cell
    //  +4 : output level, 0 VU = +4 dBu = -18 dBFS RMS
    //  +10: output level, 0 VU = +10 dBu = -12 dBFS RMS
    enum class MeterMode { gr = 0, plus4 = 1, plus10 = 2 };

    float meterReadingFor (MeterMode mode, float grDb, float outputRms) noexcept;

    // Editor drawn from the approved ASH mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class AshEditor : public ui::TleetEditorBase
    {
    public:
        explicit AshEditor (AshProcessor&);

        static constexpr int referenceWidth = 1448, referenceHeight = 1086;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1086, panelHeight = 815;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        ui::LeverSwitch& getModeSwitch() { return *mode; }
        ui::SkinVU& getVU() { return *vu; }
        MeterMode getMeterMode() const noexcept { return meterMode; }
        void setMeterMode (MeterMode m);

        static juce::var loadUiMap();

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;

        AshProcessor& proc;
        juce::var uiMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry { juce::String id; std::unique_ptr<ui::Knob> knob; juce::Rectangle<float> hitbox; };
        std::vector<KnobEntry> knobs;
        std::unique_ptr<ui::LeverSwitch> mode;
        juce::Rectangle<float> modeHitbox;
        std::unique_ptr<ui::SkinVU> vu;
        juce::Rectangle<float> vuHitbox;
        MeterMode meterMode = MeterMode::gr;
    };
}
