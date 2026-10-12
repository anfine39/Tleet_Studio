#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "PluginProcessor.h"

namespace tleet::glue
{
    // A number on a knob scale, drawn live at the angle where the parameter
    // really has that value (reference pixels).
    struct ScaleLabel
    {
        float value;                // plain parameter value
        juce::String text;
        float angleDegrees;         // 0 = 12 o'clock, clockwise
        juce::Rectangle<float> box; // reference pixels
    };

    // One dot per real position of a stepped knob (reference pixels).
    struct StepDot
    {
        juce::Point<float> centre;
        float radius;
        float angleDegrees;
    };

    // Central LED gain-reduction meter. One segment per dB from 0 to 20 dB,
    // each spanning the printed scale between its two dB marks, so the lit
    // bar ends at the printed number of the actual gain reduction (rounded to
    // the nearest dB). The value comes from the processor's grMeter, which is
    // fed with the gain the VCA really applies; the only UI processing is the
    // display's fall-back (instant rise, 120 ms fall).
    class GrLedMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Geometry
        {
            std::vector<std::pair<float, float>> scale; // (dB, x) in component coordinates, sorted
            float rowTop = 0, rowBottom = 0, gap = 2;
        };

        static constexpr int numSegments = 20;
        static constexpr float fallSeconds = 0.12f;

        explicit GrLedMeter (Geometry);

        // Returns the deepest gain reduction (dB, >= 0) since the last call, or -1 if none.
        void setSupplier (std::function<float()> f) { supplier = std::move (f); }

        // Pull from the supplier and apply the display ballistics (timer; public for tests).
        void refresh (float seconds = 1.0f / 30.0f);

        float getDisplayedDb() const noexcept { return displayed; }
        int getLitSegments() const noexcept { return litSegmentsFor (displayed); }
        static int litSegmentsFor (float grDb) noexcept;
        float xForDb (float db) const noexcept;
        juce::Rectangle<float> getSegmentBounds (int index) const noexcept;

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override { refresh(); }

        Geometry geo;
        std::function<float()> supplier;
        float displayed = 0.0f;
        int paintedLit = -1;
    };

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
