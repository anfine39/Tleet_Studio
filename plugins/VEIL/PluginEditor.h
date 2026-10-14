#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/SkinKit.h"
#include "ui/SkinVU.h"
#include "ui/SpectrumDisplay.h"
#include "PluginProcessor.h"

namespace tleet::veil
{
    using ScaleLabel = ui::skin::ScaleLabel;
    using LampSprite = ui::skin::LampSprite;
    using LampButton = ui::skin::LampButton;
    using LampGroup = ui::skin::LampGroup;

    // SIDECHAIN INT / EXT. A lamp shows the key the detectors really use:
    // INT is lit while the internal key is in use (also when EXT is chosen
    // but the host feeds no key: the safe fallback), EXT is lit while the
    // external key is used and glows faintly while it is chosen but unused.
    class SidechainLamps : private juce::Timer
    {
    public:
        SidechainLamps (VeilProcessor&, LampGroup&);
        void refresh();
        static constexpr float armedAlpha = 0.22f;

    private:
        void timerCallback() override { refresh(); }
        VeilProcessor& proc;
        LampGroup& group;
    };

    // STEREO LINK lamp and DUAL MONO: one switch. The lamp is lit while the
    // channels are linked, DUAL MONO while they are independent; a click on
    // either toggles.
    class LinkSwitch
    {
    public:
        LinkSwitch (juce::RangedAudioParameter&, std::unique_ptr<LampButton> lamp, std::unique_ptr<LampButton> dualMono,
                    std::function<void()> onEdit);
        LampButton& getLamp() { return *lamp; }
        LampButton& getDualMono() { return *dualMono; }
        bool isLinked() const noexcept { return linked; }

    private:
        void show (float value);
        juce::ParameterAttachment attachment;
        std::unique_ptr<LampButton> lamp, dualMono;
        std::function<void()> onEdit;
        bool linked = true;
    };

    // Editor drawn from the approved VEIL mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class VeilEditor : public ui::TleetEditorBase
    {
    public:
        explicit VeilEditor (VeilProcessor&);
        ~VeilEditor() override;

        static constexpr int referenceWidth = 1536, referenceHeight = 1024;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1152, panelHeight = 768;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        LampGroup& getLampGroup (const juce::String& parameterId);
        LinkSwitch& getLinkSwitch() { return *link; }
        SidechainLamps& getSidechainLamps() { return *scLamps; }
        ui::SkinVU& getVu (int channel) { return *vus[(size_t) juce::jlimit (0, 1, channel)]; }
        ui::SpectrumDisplay& getSpectrum() { return *spectrum; }
        ui::SpectrumQualityButton& getSpectrumQualityButton() { return *qualityButton; }
        const std::vector<ScaleLabel>& getScaleLabels (const juce::String& parameterId) const;

        // What a needle shows in the given mode, from the processor's real
        // measurements (NaN when no new audio arrived).
        static float meterReading (params::MeterMode, float grDb, float inputRms, float outputRms) noexcept;

        static juce::var loadUiMap();
        static juce::String labelText (const juce::String& parameterId, float value);

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;
        LampSprite sprite (const juce::String& name, juce::Rectangle<float> componentRef) const;
        void meterModeChanged (int mode);

        VeilProcessor& proc;
        juce::var uiMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry
        {
            juce::String id;
            std::unique_ptr<ui::Knob> knob;
            juce::Rectangle<float> hitbox;
            std::vector<ScaleLabel> labels;
            float capHeight = 15.0f;
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

        std::unique_ptr<SidechainLamps> scLamps;
        std::unique_ptr<LinkSwitch> link;
        juce::Rectangle<float> linkLampBounds, dualMonoBounds;

        std::array<std::unique_ptr<ui::SkinVU>, 2> vus;
        std::array<juce::Rectangle<float>, 2> vuBounds;

        std::unique_ptr<ui::SpectrumDisplay> spectrum;
        std::unique_ptr<ui::SpectrumQualityButton> qualityButton;
        juce::Rectangle<float> spectrumBounds;
    };
}
