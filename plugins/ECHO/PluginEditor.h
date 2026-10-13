#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "ui/SkinVU.h"
#include "PluginProcessor.h"

namespace tleet::echo
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using StepDot = ui::skin::StepDot;
    using LampSprite = ui::skin::LampSprite;
    using LampButton = ui::skin::LampButton;
    using LampGroup = ui::skin::LampGroup;

    // MODE lamp: the plate shows every position dark; the lamp of the
    // selected position is lit, brighter while its heads really play signal
    // (peak of the heads in use; in REVERB no head plays, so it stays at its
    // base glow).
    class ModeLamp : public juce::Component
    {
    public:
        ModeLamp (juce::Image lit, juce::Point<float> litCentre, std::vector<juce::Point<float>> positions, float scale);

        // position index, head-activity peak (linear, 0..1+); the editor's timer calls this
        void show (int position, float headPeak, float seconds);
        int getPosition() const noexcept { return position; }
        float getGlow() const noexcept { return glow; }
        static float glowFor (float headPeak) noexcept;

        void paint (juce::Graphics&) override;
        bool hitTest (int, int) override { return false; }

    private:
        juce::Image image;
        juce::Point<float> litCentre;              // sprite pixels
        std::vector<juce::Point<float>> positions; // component coordinates
        float scale;
        int position = -1;
        float glow = 0.0f, shown = -1.0f;
    };

    // A two-position slide switch (TIME MODE, HOST BPM): a sprite per
    // position. A click on the switch toggles it, a click on a caption
    // selects that side.
    class SlideSwitch : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SlideSwitch (juce::RangedAudioParameter&, std::array<juce::Image, 2>, juce::Rectangle<float> housing,
                     std::array<juce::Rectangle<float>, 2> captions, std::function<void()> onEdit);
        int getState() const noexcept { return state; }
        void clickAt (juce::Point<float>);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::ParameterAttachment attachment;
        std::array<juce::Image, 2> images;
        juce::Rectangle<float> housing;
        std::array<juce::Rectangle<float>, 2> captions;
        std::function<void()> onEdit;
        int state = 0;
    };

    // FILTER MODE: three cells, the lit one is the real position. A click
    // on a cell or its caption selects it.
    class CellSwitch : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        CellSwitch (juce::RangedAudioParameter&, juce::Image lit, juce::Rectangle<float> litArea, float pitch,
                    juce::Rectangle<float> housing, std::array<juce::Rectangle<float>, 3> captions, std::function<void()> onEdit);
        int getState() const noexcept { return state; }
        int cellAt (juce::Point<float>) const;
        juce::Rectangle<float> litAreaFor (int cell) const { return litArea.translated (0.0f, (float) (cell - 1) * pitch); }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::ParameterAttachment attachment;
        juce::Image image;
        juce::Rectangle<float> litArea, housing;
        float pitch;
        std::array<juce::Rectangle<float>, 3> captions;
        std::function<void()> onEdit;
        int state = 1;
    };

    // TAP: a click taps the tempo (the processor averages the taps). The lamp
    // flashes once per real head-1 period, so it shows the echo's time.
    class TapButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TapButton (LampSprite lit, juce::Rectangle<float> body);
        std::function<void (double)> onTap;

        // lamp on for the flash at the start of each period (ms)
        static bool lampOnAt (double nowMs, double periodMs) noexcept;
        static constexpr double flashMs = 70.0;
        void setLit (bool);
        bool isLit() const noexcept { return lit; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override { return body.contains ((float) x, (float) y); }

    private:
        LampSprite sprite;
        juce::Rectangle<float> body;
        bool lit = false;
    };

    // BPM: the tempo SYNC uses (the host's, or MANUAL BPM), as LED digits.
    // The arrows step MANUAL BPM and switch the source to Manual. In FREE
    // the display is dimmed (the echo does not follow it); it blinks while
    // SYNC had to clamp the note to the tape's range.
    class BpmDisplay : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        BpmDisplay (juce::Rectangle<float> glass, std::array<float, 4> cellX, float cellWidth, float digitTop,
                    float digitHeight, float slant, juce::Rectangle<float> up, juce::Rectangle<float> down, float scale);
        std::function<void (int)> onStep; // +1 / -1

        void setReading (double bpm, bool dimmed, bool blinkOff);
        const juce::String& getText() const noexcept { return text; }
        static juce::String textFor (double bpm);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void paintDigit (juce::Graphics&, juce::Rectangle<float> cell, int segments, bool dp, juce::Colour);
        juce::Rectangle<float> glass, up, down;
        std::array<float, 4> cellX;
        float cellWidth, digitTop, digitHeight, slant, scale;
        juce::String text;
        bool dimmed = false, blinkOff = false;
    };

    // TAPE CONDITION: the bar lights up to the mark of the real condition
    // (CLEAN, WORN, MAGNETIC DUST). A click picks the nearest mark.
    class TapeBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TapeBar (juce::RangedAudioParameter&, std::vector<float> segments, std::array<float, 3> ticks,
                 float top, float bottom, std::function<void()> onEdit);
        int getState() const noexcept { return state; }
        int litSegmentsFor (int condition) const;
        int conditionAt (float x) const;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::ParameterAttachment attachment;
        std::vector<float> segments;
        std::array<float, 3> ticks;
        float top, bottom;
        std::function<void()> onEdit;
        int state = 0;
    };

    // Editor drawn from the approved ECHO mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class EchoEditor : public ui::TleetEditorBase, private juce::Timer
    {
    public:
        explicit EchoEditor (EchoProcessor&);
        ~EchoEditor() override;

        static constexpr int referenceWidth = 1536, referenceHeight = 1024;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1152, panelHeight = 768;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        LampGroup& getLampGroup (const juce::String& parameterId);
        SlideSwitch& getSlideSwitch (const juce::String& parameterId);
        CellSwitch& getFilterModeSwitch() { return *filterMode; }
        TapButton& getTap() { return *tap; }
        BpmDisplay& getBpmDisplay() { return *bpm; }
        TapeBar& getTapeBar() { return *tapeBar; }
        ModeLamp& getModeLamp() { return *modeLamp; }
        ui::SkinVU& getVu (int index) { return *vus[(size_t) juce::jlimit (0, 1, index)]; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;
        const std::vector<StepDot>& getStepDots (const juce::String& parameterId) const;
        bool isRepeatRateDimmed() const noexcept { return repeatDimmed; }

        // One UI frame (the timer calls this; tests call it directly).
        void refresh (float seconds = 1.0f / 30.0f);

        // VU reading (dB re 0 VU) of an RMS value: a sine at -18 dBFS reads 0 VU.
        static float vuDb (float rms) noexcept;

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

        EchoProcessor& proc;
        juce::var uiMap, spriteMap;
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

        struct GroupEntry
        {
            juce::String id;
            std::unique_ptr<LampGroup> group;
            std::vector<juce::Rectangle<float>> bounds; // reference pixels, per lamp
        };
        std::vector<GroupEntry> groups;

        std::vector<std::pair<juce::Component*, juce::Rectangle<float>>> placed; // reference bounds
        std::map<juce::String, std::unique_ptr<SlideSwitch>> slides;
        std::unique_ptr<CellSwitch> filterMode;
        std::unique_ptr<TapButton> tap;
        std::unique_ptr<BpmDisplay> bpm;
        std::unique_ptr<TapeBar> tapeBar;
        std::unique_ptr<ModeLamp> modeLamp;
        std::array<std::unique_ptr<ui::SkinVU>, 2> vus;
        bool repeatDimmed = false;
    };
}
