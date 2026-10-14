#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "ui/LampToggle.h"
#include "ui/SpectrumDisplay.h"
#include "PluginProcessor.h"

namespace tleet::voidrev
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;
    using LampSprite = ui::skin::LampSprite;
    using LampButton = ui::skin::LampButton;
    using LampGroup = ui::skin::LampGroup;

    // DROWN: the ring around the large knob lights from the start of the
    // travel to the value (the plate shows it dark all round). The cap does
    // not turn; the whole knob takes the mouse like any knob.
    class DrownKnob : public ui::Knob
    {
    public:
        DrownKnob (juce::RangedAudioParameter&, juce::Image ring, juce::Rectangle<float> ringArea,
                   juce::Point<float> centre, float startDeg, float endDeg, float clipRadius);

        float getLitAngleDegrees() const noexcept; // end of the lit arc
        void paint (juce::Graphics&) override;

    private:
        juce::Image ring;
        juce::Rectangle<float> ringArea;
        juce::Point<float> centre;
        float startDeg, endDeg, clipRadius;
    };

    // EARLY / LATE: a bar lit in proportion to the level (dBFS, linear over
    // the range), instant rise and a fall-back (UI ballistics only).
    class LevelBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        LevelBar (juce::Rectangle<float> track, float minDb, float maxDb);
        static constexpr float fallDbPerSecond = 24.0f;

        // linear RMS; negative = nothing measured since the last call
        void push (float level, float seconds);
        float getDisplayedDb() const noexcept { return displayed; }
        float litWidthFor (float db) const noexcept;
        void paint (juce::Graphics&) override;
        bool hitTest (int, int) override { return true; }

    private:
        juce::Rectangle<float> track;
        float minDb, maxDb, displayed = -200.0f, painted = -1.0f;
    };

    // IN / OUT: four peak bars (IN L, IN R, OUT L, OUT R) on the printed
    // piecewise dBFS scale. Instant rise, fall-back (UI ballistics only).
    class BarMeters : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        BarMeters (std::array<juce::Rectangle<float>, 4> bars, std::vector<std::pair<float, float>> scale);
        static constexpr float fallDbPerSecond = 30.0f;

        void push (int bar, float peak, float seconds); // linear peak; negative = none
        float getDisplayedDb (int bar) const noexcept { return displayed[(size_t) bar]; }
        float yForDb (float db) const noexcept;       // component coordinates
        void paint (juce::Graphics&) override;

    private:
        std::array<juce::Rectangle<float>, 4> bars;
        std::vector<std::pair<float, float>> scale; // (dB, y), dB ascending
        std::array<float, 4> displayed { -200.0f, -200.0f, -200.0f, -200.0f };
    };

    // The response of HPF + LPF as they run (the processor's own function),
    // on a log frequency axis with live labels.
    class FilterGraph : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        FilterGraph (std::function<double (double)> response, juce::Rectangle<float> plot,
                     juce::Rectangle<float> labelStrip, float minDb, float maxDb, float labelCapHeight);

        float xForFrequency (float hz) const noexcept;
        float yForDb (float db) const noexcept;
        const juce::Path& getCurve() const noexcept { return curve; }
        void update();                  // re-reads the response when the settings changed
        void paint (juce::Graphics&) override;

    private:
        std::function<double (double)> response;
        juce::Rectangle<float> plot, labelStrip;
        float minDb, maxDb, capHeight;
        juce::Path curve;
        std::vector<float> lastDb;
    };

    // The preset bar printed on the panel: previous / next, the name of the
    // current preset (a click opens the preset list), save (the save menu)
    // and delete (the current user preset, after a confirmation).
    class PresetBar : public juce::Component
    {
    public:
        enum class Part { none, prev, name, next, save, remove };

        PresetBar (juce::Rectangle<float> prev, juce::Rectangle<float> name, juce::Rectangle<float> next,
                   juce::Rectangle<float> save, juce::Rectangle<float> remove, float capHeight);
        std::function<void (Part)> onClick;

        void setText (const juce::String& text, bool canDelete);
        const juce::String& getText() const noexcept { return text; }
        bool isDeleteEnabled() const noexcept { return canDelete; }
        Part partAt (juce::Point<float>) const;
        juce::Rectangle<float> areaOf (Part) const;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        juce::String getTooltipFor (Part) const;

    private:
        juce::Rectangle<float> prev, name, next, save, remove;
        float capHeight;
        juce::String text;
        bool canDelete = false;
        Part hover = Part::none;
    };

    // Editor drawn from the approved VOID mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class VoidEditor : public ui::TleetEditorBase, private juce::Timer
    {
    public:
        explicit VoidEditor (VoidProcessor&);
        ~VoidEditor() override;

        static constexpr int referenceWidth = 1536, referenceHeight = 1024;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1152, panelHeight = 768;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& controlId);
        LampGroup& getLampGroup (const juce::String& groupId); // "algorithmLamps", "color", "filterMode"
        DrownKnob& getDrown() { return *drown; }
        ui::LampToggle& getFreeze() { return *freeze; }
        ui::SpectrumDisplay& getSpectrum() { return *spectrum; }
        LevelBar& getLevelBar (int index) { return *levelBars[(size_t) juce::jlimit (0, 1, index)]; }
        BarMeters& getMeters() { return *meters; }
        FilterGraph& getFilterGraph() { return *filterGraph; }
        PresetBar& getPresetBar() { return *presetBar; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& controlId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& controlId) const;
        juce::Rectangle<int> getControlBounds (const juce::String& controlId) const; // reference pixels -> panel

        // One UI frame (the timer calls this; tests call it directly).
        void refresh (float seconds = 1.0f / 30.0f);

        static juce::var loadUiMap();
        static juce::var loadSpriteMap();
        static juce::String labelText (const juce::String& parameterId, float value, const juce::String& stepName);

    private:
        void timerCallback() override { refresh(); }
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;
        juce::Image spriteImage (const juce::String& name) const;
        juce::Rectangle<float> spriteRect (const juce::String& name) const;
        void place (juce::Component&, juce::Rectangle<float> refBounds);
        void presetClicked (PresetBar::Part);

        VoidProcessor& proc;
        juce::var uiMap, spriteMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry
        {
            juce::String id; // UI_MAP control id
            std::unique_ptr<ui::Knob> knob;
            juce::Rectangle<float> bounds; // reference pixels
            std::vector<ScaleLabel> labels;
            std::vector<StepDot> dots;
            float capHeight = 13.0f;
        };
        std::vector<KnobEntry> knobs;

        struct GroupEntry
        {
            juce::String id;
            std::unique_ptr<LampGroup> group;
            std::vector<juce::Rectangle<float>> bounds; // reference pixels, per lamp
        };
        std::vector<GroupEntry> groups;

        std::vector<std::pair<juce::Component*, juce::Rectangle<float>>> placed; // reference bounds
        std::map<juce::String, juce::Rectangle<float>> controlBounds;           // reference bounds by id
        std::unique_ptr<DrownKnob> drown;
        std::unique_ptr<ui::LampToggle> freeze;
        std::unique_ptr<ui::SpectrumDisplay> spectrum;
        std::unique_ptr<ui::SpectrumQualityButton> qualityButton;
        std::array<std::unique_ptr<LevelBar>, 2> levelBars;
        std::unique_ptr<BarMeters> meters;
        std::unique_ptr<FilterGraph> filterGraph;
        std::unique_ptr<PresetBar> presetBar;
    };
}
