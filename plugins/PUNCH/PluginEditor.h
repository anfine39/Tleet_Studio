#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "PluginProcessor.h"

namespace tleet::punch
{
    using ScaleLabel = ui::skin::ScaleLabel;

    // The OverEasy rocker. Its lamp (cut from the mockup) is lit only while
    // OverEasy is really on; a click anywhere on the switch or its OFF / ON
    // captions toggles the parameter.
    class OverEasySwitch : public juce::Button
    {
    public:
        OverEasySwitch (juce::RangedAudioParameter&, juce::Image lampLit, juce::Rectangle<float> lampArea);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::ButtonParameterAttachment attachment;
        juce::Image lamp;
        juce::Rectangle<float> lampArea; // component coordinates
    };

    // Editor drawn from the approved PUNCH mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class PunchEditor : public ui::TleetEditorBase
    {
    public:
        explicit PunchEditor (PunchProcessor&);

        static constexpr int referenceWidth = 1448, referenceHeight = 1086;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1086, panelHeight = 815;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        ui::skin::LedGainMeter& getMeter() { return *meter; }
        OverEasySwitch& getOverEasySwitch() { return *overEasy; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;

        static juce::var loadUiMap();
        static juce::String labelText (const juce::String& parameterId, float value);
        static std::vector<ScaleLabel> makeScaleLabels (const juce::var& control, juce::RangedAudioParameter&);

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;

        PunchProcessor& proc;
        juce::var uiMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry
        {
            juce::String id;
            std::unique_ptr<ui::Knob> knob;
            juce::Rectangle<float> hitbox;
            std::vector<ScaleLabel> labels;
            float capHeight = 18.0f;
        };
        std::vector<KnobEntry> knobs;

        std::unique_ptr<ui::skin::LedGainMeter> meter;
        juce::Rectangle<float> meterBounds;
        std::unique_ptr<OverEasySwitch> overEasy;
        juce::Rectangle<float> overEasyBounds;
    };
}
