#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "ui/LampToggle.h"
#include "ui/SkinVU.h"
#include "ui/SpectrumDisplay.h"
#include "PluginProcessor.h"

namespace tleet::tape
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;
    using LampSprite = ui::skin::LampSprite;
    using LampButton = ui::skin::LampButton;
    using LampGroup = ui::skin::LampGroup;

    // One reel in the transport window, turning while the transport runs.
    // The reel picture (a three-window NAB reel rebuilt from the artwork,
    // see tools/skinprep/prep_tape.py) turns; the light that falls on it and
    // the parts of the window that hide it stay put: `light` holds the light
    // per channel (RGB = factor x 128) and, in alpha, where the reel shows.
    // UI only: nothing here touches the audio, and nothing runs while the
    // editor is closed.
    class TapeReel : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TapeReel (juce::Image reel, juce::Image light);

        // Reel angle (radians, clockwise on screen).
        void setAngle (float radians);
        float getAngle() const noexcept { return angle; }

        // Turns per second of a reel at this tape speed (IPS): the tape
        // leaves the pack at its circumference, pack radius packInches.
        static constexpr double packInches = 3.5;
        static double turnsPerSecond (double ips) noexcept { return ips / (juce::MathConstants<double>::twoPi * packInches); }

        void paint (juce::Graphics&) override;
        bool hitTest (int, int) override { return false; }

    private:
        juce::Image reel, light, scaledLight, frame;
        float angle = 0.0f;
    };

    // SATURATION: 24 LED segments, lit from the left by the deepest
    // saturation the magnetic layer reached since the last frame
    // (TapeProcessor::getSaturationActivity, 0..1: round(24 x activity)
    // segments). White segments, the last nine red. Display ballistics only:
    // instant rise, exponential fall.
    class SaturationBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int numSegments = 24;
        static constexpr float fallSeconds = 0.2f;

        struct Geometry
        {
            std::vector<float> segmentX;     // left edge of each segment, component coordinates
            float top = 0;                   // segment top
            int redFrom = 15;
            juce::Image white, red;          // lit sprites
            juce::Rectangle<float> spriteBox; // a sprite's box relative to its segment's left edge / top
        };

        explicit SaturationBar (Geometry);

        void push (float activity, float seconds); // activity 0..1
        static int litFor (float activity) noexcept { return juce::jlimit (0, numSegments, juce::roundToInt (activity * (float) numSegments)); }
        float getDisplayed() const noexcept { return displayed; }
        int getLitSegments() const noexcept { return litFor (displayed); }

        void paint (juce::Graphics&) override;

    private:
        Geometry geo;
        float displayed = 0.0f;
        int paintedLit = 0;
    };

    // TAPE CONDITION's second layer, over the output spectrum: the
    // machine's small-signal frequency response as it runs
    // (TapeProcessor::tapeResponseDb, every control and the AGE wear
    // included), on the spectrum's frequency axis, +-responseRangeDb around
    // a dashed 0 dB line.
    class ResponseCurve : public juce::Component
    {
    public:
        static constexpr float responseRangeDb = 12.0f;

        ResponseCurve (TapeProcessor&, const ui::SpectrumDisplay&, juce::Rectangle<float> screen);

        // Recomputes the curve (on the editor's timer, only when a setting
        // changed); true if it changed.
        bool update();
        float yForResponseDb (float db) const noexcept;
        const std::vector<float>& getResponseDb() const noexcept { return response; }
        const std::vector<float>& getFrequencies() const noexcept { return hz; }

        void paint (juce::Graphics&) override;
        bool hitTest (int, int) override { return false; }

    private:
        TapeProcessor& proc;
        const ui::SpectrumDisplay& spectrum;
        juce::Rectangle<float> screen;
        std::vector<float> hz, response;
        std::vector<double> hzD, outD;
        juce::Path path;
    };

    // An image drawn over other components (the VU pivot caps over the
    // needles).
    class SpriteCover : public juce::Component
    {
    public:
        explicit SpriteCover (juce::Image i) : image (std::move (i)) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
        }

    private:
        juce::Image image;
    };

    // Editor drawn from the approved TAPE mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class TapeEditor : public ui::TleetEditorBase, private juce::Timer
    {
    public:
        explicit TapeEditor (TapeProcessor&);
        ~TapeEditor() override;

        static constexpr int referenceWidth = 1536, referenceHeight = 1024;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1152, panelHeight = 768;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& controlId);
        LampGroup& getLampGroup (const juce::String& groupId); // "tapeType", "speed", "calibration", "noise"
        ui::LampToggle& getAutoGain() { return *autoGain; }
        ui::SkinVU& getVu (bool repro, int channel) { return *vus[(size_t) ((repro ? 2 : 0) + channel)]; }
        SaturationBar& getSaturationBar() { return *saturation; }
        ui::SpectrumDisplay& getSpectrum() { return *spectrum; }
        ResponseCurve& getResponseCurve() { return *response; }
        TapeReel& getReel (int index) { return *reels[(size_t) index]; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& controlId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& controlId) const;
        juce::Rectangle<int> getControlBounds (const juce::String& controlId) const; // reference pixels -> panel

        // One UI frame (the timer calls this; tests call it directly).
        void refresh (float seconds = 1.0f / 30.0f);

        static juce::var loadUiMap();
        static juce::var loadSpriteMap();
        static juce::String labelText (const juce::String& parameterId, float value);

    private:
        void timerCallback() override;
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;
        juce::Image spriteImage (const juce::String& name) const;
        juce::Rectangle<float> spriteRect (const juce::String& name) const;
        void place (juce::Component&, juce::Rectangle<float> refBounds);

        TapeProcessor& proc;
        juce::var uiMap, spriteMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;
        double lastTick = 0.0;

        struct KnobEntry
        {
            juce::String id; // UI_MAP control id
            std::unique_ptr<ui::Knob> knob;
            juce::Rectangle<float> bounds; // reference pixels
            std::vector<ScaleLabel> labels;
            std::vector<StepDot> dots;
            float capHeight = 9.0f;
        };
        std::vector<KnobEntry> knobs;

        struct GroupEntry
        {
            juce::String id;
            std::unique_ptr<LampGroup> group;
            std::vector<juce::Rectangle<float>> bounds; // reference pixels, per lamp
        };
        std::vector<GroupEntry> groups;

        std::unique_ptr<ui::LampToggle> autoGain;
        std::array<std::unique_ptr<ui::SkinVU>, 4> vus; // RECORD L, R; REPRO L, R
        std::array<std::unique_ptr<SpriteCover>, 2> vuCaps;
        std::unique_ptr<SaturationBar> saturation;
        std::unique_ptr<ui::SpectrumDisplay> spectrum;
        std::unique_ptr<ui::SpectrumQualityButton> qualityButton;
        std::unique_ptr<ResponseCurve> response;
        std::array<std::unique_ptr<TapeReel>, 2> reels;
        std::vector<std::pair<juce::Component*, juce::Rectangle<float>>> placed; // reference bounds
        std::map<juce::String, juce::Rectangle<float>> controlBounds;           // reference bounds by id
    };
}
