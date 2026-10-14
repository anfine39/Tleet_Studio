#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "ui/LampToggle.h"
#include "ui/SpectrumDisplay.h"
#include "PluginProcessor.h"

namespace tleet::steel
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;
    using LampSprite = ui::skin::LampSprite;
    using LampButton = ui::skin::LampButton;
    using LampGroup = ui::skin::LampGroup;

    // The large display: the last `seconds` of the output (peak and RMS
    // envelope, dBFS, rising from the bottom), the gain reduction actually
    // applied (a trace falling from the GR line at the top, same dB per
    // pixel as the level scale), the ceiling, and the TP OVER / HEAVY
    // LIMITING warnings while they are on. Fed with the processor's history
    // slices; nothing moves while no audio runs.
    class HistoryDisplay : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        struct Slice { float peakDb = -200.0f, rmsDb = -200.0f, grDb = 0.0f; };

        struct Geometry // component coordinates
        {
            juce::Rectangle<float> plot;               // level scale: topDb at the top edge, bottomDb at the bottom edge
            float grBaselineY = 0;                     // 0 dB of gain reduction
            float topDb = 0.0f, bottomDb = -48.0f, dbStep = 6.0f;
            juce::Range<float> labelColumn;            // dB numbers left of the plot
            juce::Range<float> leftTicks, rightTicks;  // short marks beside the plot
            float timeLabelY = 0;                      // centre line of the time labels
            float labelCapHeight = 10.0f;
        };

        HistoryDisplay (Geometry, double seconds, double sliceSeconds);

        void push (const Slice*, int numSlices);
        void setCeilingDb (float db);
        void setWarnings (bool truePeakOver, bool heavyLimiting);

        float yForDb (float db) const noexcept;    // level scale
        float yForGrDb (float grDb) const noexcept; // gain-reduction trace (grDb <= 0)
        float xForAge (double secondsAgo) const noexcept;
        int getCapacity() const noexcept { return (int) ring.size(); }
        int getNumFilled() const noexcept { return filled; }
        Slice getSlice (int age) const noexcept;    // 0 = newest
        float getDeepestGrDb (double lastSeconds) const noexcept;
        float getCeilingDb() const noexcept { return ceilingDb; }
        bool isTruePeakOverShown() const noexcept { return tpOver; }
        bool isHeavyLimitingShown() const noexcept { return heavy; }
        static juce::String timeLabel (double secondsAgo);
        static juce::String dbLabel (float db);

        void paint (juce::Graphics&) override;

    private:
        Geometry geo;
        double seconds, sliceSeconds;
        std::vector<Slice> ring;
        int head = 0, filled = 0;
        float ceilingDb = 0.0f;
        bool tpOver = false, heavy = false;
    };

    // IN or OUT: four bars, sample peak L/R and true peak L/R, on the same
    // dB scale as the large display. Instant rise, fall-back (UI ballistics
    // only). OUT also prints the scale beside its bars.
    class PeakBars : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        PeakBars (std::array<juce::Rectangle<float>, 4> bars, float yTop, float yBottom, float topDb, float bottomDb,
                  juce::Range<float> labelColumn, float labelCapHeight);
        static constexpr float fallDbPerSecond = 24.0f;

        void push (int bar, float db, float seconds); // dB; NaN = nothing measured since the last call
        float getDisplayedDb (int bar) const noexcept { return displayed[(size_t) bar]; }
        float yForDb (float db) const noexcept;
        void paint (juce::Graphics&) override;

    private:
        std::array<juce::Rectangle<float>, 4> bars;
        float yTop, yBottom, topDb, bottomDb;
        juce::Range<float> labelColumn;
        float capHeight;
        std::array<float, 4> displayed { -200.0f, -200.0f, -200.0f, -200.0f };
    };

    // LUFS-I, LUFS-S, LRA, TRUE PEAK (highest since reset) and GAIN
    // REDUCTION in the printed readout panel. A click resets the
    // measurement (onReset).
    class LoudnessPanel : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum Field { lufsI = 0, lufsS, lra, truePeak, gainReduction, numFields };

        LoudnessPanel (std::array<juce::Rectangle<float>, numFields> valueRects, float capHeight);
        std::function<void()> onReset;

        void setValues (float lufsI, float lufsS, float lra, float truePeakDb, float grDb, bool truePeakOver);
        const juce::String& getText (Field f) const noexcept { return texts[(size_t) f]; }
        static juce::String format (Field, float value);

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        std::array<juce::Rectangle<float>, numFields> rects;
        std::array<juce::String, numFields> texts;
        float capHeight;
        bool over = false;
    };

    // Editor drawn from the approved STEEL mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class SteelEditor : public ui::TleetEditorBase, private juce::Timer
    {
    public:
        explicit SteelEditor (SteelProcessor&);
        ~SteelEditor() override;

        static constexpr int referenceWidth = 1536, referenceHeight = 1024;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1152, panelHeight = 768;
        static constexpr double historySeconds = 10.0;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& controlId);
        LampGroup& getLampGroup (const juce::String& groupId); // "mode", "releaseMode", "oversampling", "subFilter"
        ui::LampToggle& getToggle (const juce::String& controlId); // "truePeak", "softClip", "delta", "levelMatch"
        HistoryDisplay& getHistory() { return *history; }
        PeakBars& getMeters (bool output) { return output ? *outMeters : *inMeters; }
        LoudnessPanel& getLoudness() { return *loudness; }
        ui::SpectrumDisplay& getSpectrum() { return *spectrum; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& controlId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& controlId) const;
        juce::Rectangle<int> getControlBounds (const juce::String& controlId) const; // reference pixels -> panel

        // One UI frame (the timer calls this; tests call it directly).
        void refresh (float seconds = 1.0f / 30.0f);

        static juce::var loadUiMap();
        static juce::var loadSpriteMap();
        static juce::String labelText (const juce::String& parameterId, float value);

    private:
        void timerCallback() override { refresh(); }
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;
        juce::Image spriteImage (const juce::String& name) const;
        juce::Rectangle<float> spriteRect (const juce::String& name) const;
        void place (juce::Component&, juce::Rectangle<float> refBounds);

        SteelProcessor& proc;
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
            float capHeight = 12.0f;
        };
        std::vector<KnobEntry> knobs;

        struct GroupEntry
        {
            juce::String id;
            std::unique_ptr<LampGroup> group;
            std::vector<juce::Rectangle<float>> bounds; // reference pixels, per lamp
        };
        std::vector<GroupEntry> groups;

        std::vector<std::pair<juce::String, std::unique_ptr<ui::LampToggle>>> toggles;
        std::vector<std::pair<juce::Component*, juce::Rectangle<float>>> placed; // reference bounds
        std::map<juce::String, juce::Rectangle<float>> controlBounds;           // reference bounds by id
        std::unique_ptr<HistoryDisplay> history;
        std::unique_ptr<PeakBars> inMeters, outMeters;
        std::unique_ptr<LoudnessPanel> loudness;
        std::unique_ptr<ui::SpectrumDisplay> spectrum;
        std::unique_ptr<ui::SpectrumQualityButton> qualityButton;
        std::vector<HistoryDisplay::Slice> sliceBuffer;
    };
}
