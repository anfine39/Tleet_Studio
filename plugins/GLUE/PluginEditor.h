#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "PluginProcessor.h"

namespace tleet::glue
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;

    // Central LED gain-reduction meter: one segment per dB from 0 to 20 dB,
    // fed with the gain the VCA really applies (ui::skin::LedGainMeter).
    using GrLedMeter = ui::skin::LedGainMeter;

    // The lamp on the Character arc, lit at the selected position.
    class ArcLamp : public juce::Component
    {
    public:
        ArcLamp (juce::RangedAudioParameter& p, juce::Point<float> centre, float radius,
                 float startDegrees, float endDegrees, float scale);

        float getLampAngleDegrees() const noexcept;
        juce::Point<float> getLampCentre() const noexcept; // component coordinates
        void paint (juce::Graphics&) override;
        bool hitTest (int, int) override { return false; }

    private:
        juce::RangedAudioParameter& param;
        juce::ParameterAttachment attachment;
        juce::Point<float> centre;
        float radius, startDeg, endDeg, s;
    };

    // Toolbar switch for the external sidechain. Its lamp is lit only while
    // the detector is really listening to the sidechain input.
    class SidechainButton : public juce::TextButton, private juce::Timer
    {
    public:
        SidechainButton (GlueProcessor&, juce::RangedAudioParameter&);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool isLampLit() const noexcept { return lampLit; }
        void refresh();

    private:
        void timerCallback() override { refresh(); }
        GlueProcessor& proc;
        juce::ButtonParameterAttachment attachment;
        bool lampLit = false;
    };

    // Editor drawn from the approved GLUE mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class GlueEditor : public ui::TleetEditorBase
    {
    public:
        explicit GlueEditor (GlueProcessor&);

        static constexpr int referenceWidth = 1448, referenceHeight = 1086;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1086, panelHeight = 815;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        GrLedMeter& getMeter() { return *meter; }
        ArcLamp& getArcLamp() { return *arcLamp; }
        SidechainButton& getSidechainButton() { return *scButton; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& parameterId) const;

        static juce::var loadUiMap();
        static juce::String labelText (const juce::String& parameterId, float value, const juce::String& stepName);
        static std::vector<ScaleLabel> makeScaleLabels (const juce::var& control, juce::RangedAudioParameter&);
        static std::vector<StepDot> makeStepDots (const juce::var& control, juce::RangedAudioParameter&);

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;

        GlueProcessor& proc;
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
            float capHeight = 19.0f;
        };
        std::vector<KnobEntry> knobs;

        std::unique_ptr<GrLedMeter> meter;
        juce::Rectangle<float> meterBounds;
        std::unique_ptr<ArcLamp> arcLamp;
        juce::Rectangle<float> arcBounds;
        std::unique_ptr<SidechainButton> scButton;
    };
}
