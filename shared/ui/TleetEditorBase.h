#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "core/TleetProcessorBase.h"
#include "TleetLookAndFeel.h"
#include "Toolbar.h"
#include "AboutPanel.h"

namespace tleet::ui
{
    // Editor shell shared by all plugins. The plugin UI is laid out once at
    // its base size inside `content`; the whole content is scaled with an
    // affine transform, so vector graphics and text re-render sharply at any
    // size from 75 % to 200 % (aspect ratio locked). The chosen scale is
    // stored per plugin instance in the project.
    class TleetEditorBase : public juce::AudioProcessorEditor
    {
    public:
        static constexpr int toolbarHeight = 40;

        TleetEditorBase (TleetProcessorBase& processor, int baseWidth, int baseHeight);
        ~TleetEditorBase() override;

        void resized() override;
        void paint (juce::Graphics&) override;

    protected:
        // Plugin panel drawing and layout, in base (unscaled) coordinates,
        // for the area below the toolbar.
        virtual void paintPanel (juce::Graphics&, juce::Rectangle<int>) {}
        virtual void layoutPanel (juce::Rectangle<int>) {}

        void addToPanel (juce::Component& c) { content.addAndMakeVisible (c); }
        void addToToolbar (juce::Component& c, int width) { toolbar.addExtra (c, width); }
        void finishSetup(); // call at the end of the derived constructor
        Toolbar& getToolbar() noexcept { return toolbar; }

        TleetProcessorBase& baseProcessor;

    private:
        struct Content : juce::Component
        {
            explicit Content (TleetEditorBase& o) : owner (o) {}
            void paint (juce::Graphics& g) override;
            void resized() override;
            TleetEditorBase& owner;
        };

        void applyScale (float scale);

        TleetLookAndFeel lookAndFeel;
        Content content { *this };
        Toolbar toolbar;
        AboutPanel about;
        juce::TooltipWindow tooltips { this, 600 };
        const int baseW, baseH;
        bool setupDone = false;
    };
}
