#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Pieces shared by the skinned editors that read their geometry from an
// embedded UI_MAP.json (reference pixels of the mockup): live scale numbers,
// one dot per real step of a stepped knob, and an LED gain-reduction bar.
namespace tleet::ui::skin
{
    juce::Rectangle<float> rectFrom (const juce::var& v);
    juce::Point<float> pointFrom (const juce::var& v);
    const juce::var* findControl (const juce::var& map, const juce::String& id);

    // 0 degrees = 12 o'clock, clockwise.
    juce::Point<float> polar (juce::Point<float> centre, float radius, float degrees);

    // Pointer angle of a knob control ("travelDegrees") at a normalised value.
    float angleFor (const juce::var& control, float normalised);

    // Font of the live scale numbers; `capHeight` is the printed digit height
    // (the font's cap height is about 0.72 em).
    juce::Font scaleFont (float capHeight);

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

    // Text of a label: (plain value, step name or empty for continuous knobs).
    using LabelTextFn = std::function<juce::String (float value, const juce::String& stepName)>;

    // From a control's "scaleLabels" entry: {"values": [plain values] | "steps",
    // "gap", "capHeight"}, placed just outside the dot circle ("scaleCentre",
    // "dotRadius"). Each label's value goes through the parameter's own range.
    std::vector<ScaleLabel> makeScaleLabels (const juce::var& control, juce::RangedAudioParameter&, const LabelTextFn&);

    // From a control's "stepDots" entry: one dot per parameter step, or
    // "count" evenly spaced dots over the travel.
    std::vector<StepDot> makeStepDots (const juce::var& control, juce::RangedAudioParameter&);

    // Paints dots and labels (given in reference pixels) at `scale` from `origin`.
    void paintScale (juce::Graphics&, const std::vector<StepDot>&, const std::vector<ScaleLabel>&,
                     float capHeight, float scale, juce::Point<float> origin,
                     juce::Colour dotInk, juce::Colour labelInk);

    // LED gain-reduction bar. Segments are dB intervals between consecutive
    // `boundaries`, each spanning the printed scale between its two marks
    // (`scale`: (dB, x) pairs, piecewise linear), so the lit bar ends at the
    // printed value of the actual gain reduction, to the nearest boundary.
    // The value comes from the processor's GR telemetry (the applied gain);
    // the only UI processing is the display's fall-back (instant rise,
    // exponential fall).
    class LedGainMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Geometry
        {
            std::vector<std::pair<float, float>> scale; // (dB, x) in component coordinates
            std::vector<float> boundaries;              // dB; empty = 0, 1, ... 20
            float rowTop = 0, rowBottom = 0, gap = 2;
        };

        static constexpr float fallSeconds = 0.12f;

        explicit LedGainMeter (Geometry);

        // Returns the deepest gain reduction (dB, >= 0) since the last call, or -1 if none.
        void setSupplier (std::function<float()> f) { supplier = std::move (f); }

        // Pull from the supplier and apply the display ballistics (timer; public for tests).
        void refresh (float seconds = 1.0f / 30.0f);

        float getDisplayedDb() const noexcept { return displayed; }
        int getNumSegments() const noexcept { return (int) geo.boundaries.size() - 1; }
        int getLitSegments() const noexcept { return litSegmentsFor (displayed); }
        // A segment lights once the reduction reaches its middle.
        int litSegmentsFor (float grDb) const noexcept;
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

    // LED gain-reduction bar on an arc (a VU-style printed scale): segments
    // are annular sectors around a centre far below the bar. Each segment
    // spans a dB interval of gain reduction and lights once the reduction
    // reaches its middle; decorative segments (where the printed scale goes
    // beyond what a gain reduction can show) are always drawn unlit. Same
    // display ballistics as LedGainMeter: instant rise, exponential fall.
    class ArcLedMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Segment
        {
            float startDeg = 0, endDeg = 0; // around `centre`, 0 = up, clockwise
            float loDb = 0, hiDb = 0;       // gain-reduction interval (lit segments)
            bool decorative = false;        // never lit
        };

        struct Geometry
        {
            juce::Point<float> centre;     // component coordinates
            float outerRadius = 0, innerRadius = 0;
            float gap = 2;                 // between segments, along the arc
            std::vector<Segment> segments; // lit segments first, from 0 dB outwards
        };

        static constexpr float fallSeconds = 0.12f;

        explicit ArcLedMeter (Geometry);

        // Returns the gain reduction to show (dB, >= 0), or -1 if none.
        void setSupplier (std::function<float()> f) { supplier = std::move (f); }
        void refresh (float seconds = 1.0f / 30.0f);

        float getDisplayedDb() const noexcept { return displayed; }
        int getNumLitCapableSegments() const noexcept;
        int getLitSegments() const noexcept { return litSegmentsFor (displayed); }
        int litSegmentsFor (float grDb) const noexcept;
        const Geometry& getGeometry() const noexcept { return geo; }

        // The segment's outline (component coordinates), gap applied.
        juce::Path getSegmentPath (int index) const;

        void paint (juce::Graphics&) override;
        bool hitTest (int, int) override { return false; }

    private:
        void timerCallback() override { refresh(); }

        Geometry geo;
        std::function<float()> supplier;
        float displayed = 0.0f;
        int paintedLit = -1;
    };

    // Reads an ARC_LED_METER control: "arc" {centre, railRadius,
    // segmentDepth [outer, inner] below the rail}, "scaleDegrees" {"dB": angle},
    // "segmentBoundaries" (dB), "unlitDegrees" [[from, to], ...], "segmentGap",
    // into a geometry relative to `bounds` (reference pixels) at `scale`.
    ArcLedMeter::Geometry arcMeterGeometry (const juce::var& control, juce::Rectangle<float> bounds, float scale);

    // Reads an LED_METER control ("scale": {"dB": x}, "segmentRow", "segmentGap",
    // optional "segmentBoundaries") into a geometry relative to `bounds`
    // (reference pixels), at `scale` editor units per reference pixel.
    LedGainMeter::Geometry meterGeometry (const juce::var& control, juce::Rectangle<float> bounds, float scale);
    // A lit lamp cut from the mockup, drawn over a dark lamp of the plate.
    struct LampSprite
    {
        juce::Image image;
        juce::Rectangle<float> area; // component coordinates
    };

    // One lamp of a lamp group. The plate shows every lamp dark; a lamp is
    // lit only while its position is the parameter's real value. A click on
    // the lamp or its caption selects it. The component also covers the
    // lamp's glow, which may reach past the click area; only the click area
    // takes the mouse.
    class LampButton : public juce::Button
    {
    public:
        LampButton (const juce::String& name, LampSprite, juce::Rectangle<float> clickArea);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool hitTest (int x, int y) override { return clickArea.contains ((float) x, (float) y); }
        juce::Rectangle<float> getClickArea() const noexcept { return clickArea; }

        // How strongly the lit sprite is drawn while toggled on (a lamp that
        // is armed but not in use glows faintly).
        void setLampAlpha (float a) { if (a != alpha) { alpha = a; repaint(); } }
        float getLampAlpha() const noexcept { return alpha; }

    private:
        LampSprite sprite;
        juce::Rectangle<float> clickArea; // component coordinates
        float alpha = 1.0f;
    };

    // The lamps of one choice (or bool) parameter, kept in step with it both ways.
    class LampGroup
    {
    public:
        LampGroup (juce::RangedAudioParameter&, std::function<void()> onEdit);
        void add (std::unique_ptr<LampButton>);
        int getNumLamps() const noexcept { return (int) lamps.size(); }
        LampButton& getLamp (int i) { return *lamps[(size_t) i]; }
        int getSelected() const noexcept { return selected; }

        // Called after the lamps were updated from the parameter.
        std::function<void (int)> onShow;

    private:
        void show (float value);

        juce::ParameterAttachment attachment;
        std::vector<std::unique_ptr<LampButton>> lamps;
        std::function<void()> onEdit;
        int selected = -1;
    };
}
