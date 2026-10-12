#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/LampButtonGroup.h"
#include "ui/SkinVU.h"
#include "PluginProcessor.h"

namespace tleet::bite
{
    // VU meter source selection, like the hardware meter switch.
    //  GR : needle = 0 VU minus actual gain reduction of the FET stage
    //  +4 : output level, 0 VU = +4 dBu = -18 dBFS RMS
    //  +8 : output level, 0 VU = +8 dBu = -14 dBFS RMS
    enum class MeterMode { gr = 0, plus4 = 1, plus8 = 2 };

    float meterReadingFor (MeterMode mode, float grDb, float outputRms) noexcept;

    // Printed time scale of a skinned Attack/Release knob. The artwork's
    // numbers are replaced by the parameter's real value at each scale dot,
    // computed from the parameter range, so the scale cannot drift from the DSP.
    struct ScaleLabel
    {
        float value;                 // plain parameter value at the dot
        juce::String text;
        juce::Rectangle<float> box;  // reference pixels
        juce::Justification justification;
    };

    // Editor drawn from the approved BITE mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class BiteEditor : public ui::TleetEditorBase
    {
    public:
        explicit BiteEditor (BiteProcessor&);

        static constexpr int referenceWidth = 1448, referenceHeight = 1086;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1086, panelHeight = 815;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        ui::LampButtonGroup& getRatioButtons() { return *ratio; }
        ui::SkinVU& getVU() { return *vu; }
        MeterMode getMeterMode() const noexcept { return meterMode; }
        void setMeterMode (MeterMode m);
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;

        static juce::var loadUiMap();
        static std::vector<ScaleLabel> makeScaleLabels (const juce::var& control, juce::RangedAudioParameter&);

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;

        BiteProcessor& proc;
        juce::var uiMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry { juce::String id; std::unique_ptr<ui::Knob> knob; juce::Rectangle<float> hitbox; };
        std::vector<KnobEntry> knobs;

        struct TimeScale { juce::String id; std::vector<ScaleLabel> labels; juce::String unit; juce::Point<float> unitAnchor; float textHeight; };
        std::vector<TimeScale> timeScales;

        std::unique_ptr<ui::LampButtonGroup> ratio;
        juce::Rectangle<float> ratioBounds;
        std::unique_ptr<ui::SkinVU> vu;
        juce::Rectangle<float> vuHitbox;
        MeterMode meterMode = MeterMode::gr;
    };
}
