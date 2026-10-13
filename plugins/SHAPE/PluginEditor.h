#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "PluginProcessor.h"

namespace tleet::shape
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;
    using LampSprite = ui::skin::LampSprite;

    // EQ IN: the lamp is lit while the passive EQ is in the signal path; a
    // click toggles it. Only the lamp's bezel takes the mouse.
    class EqInLamp : public juce::Button
    {
    public:
        EqInLamp (juce::RangedAudioParameter&, LampSprite, juce::Rectangle<float> clickArea);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool hitTest (int x, int y) override { return clickArea.contains ((float) x, (float) y); }

    private:
        juce::ButtonParameterAttachment attachment;
        LampSprite sprite;
        juce::Rectangle<float> clickArea;
    };

    // Editor drawn from the approved SHAPE mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class ShapeEditor : public ui::TleetEditorBase
    {
    public:
        explicit ShapeEditor (ShapeProcessor&);

        static constexpr int referenceWidth = 1448, referenceHeight = 1086;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1086, panelHeight = 815;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        EqInLamp& getEqInLamp() { return *eqIn; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& parameterId) const;

        static juce::var loadUiMap();
        static juce::String labelText (const juce::String& parameterId, float value, const juce::String& stepName);

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;
        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;

        ShapeProcessor& proc;
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
            float capHeight = 16.0f;
            juce::Colour ink;
        };
        std::vector<KnobEntry> knobs;

        std::unique_ptr<EqInLamp> eqIn;
        juce::Rectangle<float> eqInBounds;
    };
}
