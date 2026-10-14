#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "PluginProcessor.h"

namespace tleet::weight
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;

    using LampSprite = ui::skin::LampSprite;
    using LampButton = ui::skin::LampButton;
    using LampGroup = ui::skin::LampGroup;

    // EXTERNAL SC. The lamp is fully lit only while the external key is really
    // used (switch on and the host feeds the Sidechain bus); switched on with
    // no key connected it glows faintly and WEIGHT keeps its internal
    // detector (the same rule as GLUE).
    class SidechainLamp : public juce::Button, private juce::Timer
    {
    public:
        SidechainLamp (WeightProcessor&, juce::RangedAudioParameter&, LampSprite);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool isLampLit() const noexcept { return lampLit; }
        void refresh();

        static constexpr float armedAlpha = 0.22f;

    private:
        void timerCallback() override { refresh(); }

        WeightProcessor& proc;
        juce::ButtonParameterAttachment attachment;
        LampSprite sprite;
        bool lampLit = false;
    };

    // Editor drawn from the approved WEIGHT mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class WeightEditor : public ui::TleetEditorBase
    {
    public:
        explicit WeightEditor (WeightProcessor&);

        static constexpr int referenceWidth = 1448, referenceHeight = 1086;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1086, panelHeight = 815;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        ui::skin::ArcLedMeter& getMeter (int channel) { return *meters[(size_t) juce::jlimit (0, 1, channel)]; }
        LampGroup& getLampGroup (const juce::String& parameterId);
        SidechainLamp& getSidechainLamp() { return *scLamp; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& parameterId) const;

        static juce::var loadUiMap();
        static juce::String labelText (const juce::String& parameterId, float value, const juce::String& stepName);

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;
        // The sprite `name` placed on the lamp `buttonRef` (reference pixels).
        juce::Rectangle<float> spriteArea (const juce::String& name, juce::Rectangle<float> buttonRef) const;
        juce::Image spriteImage (const juce::String& name) const;

        WeightProcessor& proc;
        juce::var uiMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry
        {
            juce::String id;
            std::unique_ptr<ui::Knob> knob;
            juce::Rectangle<float> hitbox;
            std::vector<ScaleLabel> labels;
            std::vector<StepDot> dots;
            float capHeight = 18.0f;
        };
        std::vector<KnobEntry> knobs;

        struct GroupEntry
        {
            juce::String id;
            std::unique_ptr<LampGroup> group;
            std::vector<juce::Rectangle<float>> bounds; // reference pixels, per lamp (click area and glow)
        };
        std::vector<GroupEntry> groups;

        std::unique_ptr<SidechainLamp> scLamp;
        juce::Rectangle<float> scBounds;

        std::array<std::unique_ptr<ui::skin::ArcLedMeter>, 2> meters;
        std::array<juce::Rectangle<float>, 2> meterBounds;
    };
}
