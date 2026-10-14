#pragma once

#include "ui/TleetEditorBase.h"
#include "ui/Knob.h"
#include "ui/LampButtonGroup.h"
#include "ui/LampToggle.h"
#include "PluginProcessor.h"
#include "VoiceDisplay.h"
#include "FilterDisplay.h"

namespace tleet::dbl
{
    // Editor drawn from the approved DOUBLE mockup. Geometry comes from
    // UI_MAP.json (embedded), so the code and the map cannot drift apart.
    class DoubleEditor : public ui::TleetEditorBase
    {
    public:
        explicit DoubleEditor (DoubleProcessor&);

        static constexpr int referenceWidth = 1536, referenceHeight = 1024;
        static constexpr float skinScale = 0.75f; // reference pixels -> editor units at 100 %
        static constexpr int baseWidth = 1152, panelHeight = 768;

        // Exposed for GUI tests.
        ui::Knob& getKnob (const juce::String& parameterId);
        ui::LampButtonGroup& getVoiceButtons() { return *voices; }
        ui::LampButtonGroup& getModeButtons() { return *mode; }
        ui::LampToggle& getToggle (const juce::String& parameterId);
        VoiceDisplay& getVoiceDisplay() { return *voiceDisplay; }
        FilterDisplay& getFilterDisplay() { return *filterDisplay; }

        static juce::var loadUiMap();

    private:
        void paintPanel (juce::Graphics&, juce::Rectangle<int>) override;
        void layoutPanel (juce::Rectangle<int>) override;

        juce::Rectangle<int> refToPanel (juce::Rectangle<float> refRect) const;
        std::unique_ptr<ui::LampButtonGroup> makeGroup (const juce::String& id, const char* param, const juce::StringArray& names,
                                                         std::initializer_list<std::pair<const char*, int>> sprites,
                                                         juce::Rectangle<float>& boundsOut);

        DoubleProcessor& proc;
        juce::var uiMap;
        juce::Image plate;
        juce::Rectangle<int> panelArea;

        struct KnobEntry { juce::String id; std::unique_ptr<ui::Knob> knob; juce::Rectangle<float> hitbox; };
        std::vector<KnobEntry> knobs;
        struct ToggleEntry { juce::String id; std::unique_ptr<ui::LampToggle> toggle; juce::Rectangle<float> bounds; };
        std::vector<ToggleEntry> toggles;

        std::unique_ptr<ui::LampButtonGroup> voices, mode;
        juce::Rectangle<float> voicesBounds, modeBounds;
        std::unique_ptr<VoiceDisplay> voiceDisplay;
        juce::Rectangle<float> voiceDisplayBounds;
        std::unique_ptr<FilterDisplay> filterDisplay;
        juce::Rectangle<float> filterDisplayBounds;
    };
}
