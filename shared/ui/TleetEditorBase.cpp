#include "TleetEditorBase.h"

#ifndef TLEET_PLUGIN_DISPLAY_NAME
 #define TLEET_PLUGIN_DISPLAY_NAME ""
#endif

namespace tleet::ui
{
    static constexpr float minScale = 0.75f, maxScale = 2.0f;

    TleetEditorBase::TleetEditorBase (TleetProcessorBase& p, int w, int h)
        : juce::AudioProcessorEditor (p),
          baseProcessor (p),
          toolbar (p),
          about (p.getPluginId(), TLEET_PLUGIN_DISPLAY_NAME),
          baseW (w), baseH (h)
    {
        setLookAndFeel (&lookAndFeel);
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        content.setSize (baseW, baseH);
        content.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
        addAndMakeVisible (content);
        content.addAndMakeVisible (toolbar);
        content.addChildComponent (about);

        toolbar.onAbout = [this] { about.setVisible (true); about.toFront (false); };
        toolbar.onScaleChosen = [this] (float s) { applyScale (s); };
    }

    TleetEditorBase::~TleetEditorBase()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void TleetEditorBase::finishSetup()
    {
        // Read the saved scale first: setting resize limits triggers resized(),
        // which would otherwise overwrite it with the minimum size.
        const float scale = juce::jlimit (minScale, maxScale, (float) (double) baseProcessor.getUiProperty ("scale", 1.0));

        setResizable (true, true);
        setResizeLimits (juce::roundToInt (baseW * minScale), juce::roundToInt (baseH * minScale),
                         juce::roundToInt (baseW * maxScale), juce::roundToInt (baseH * maxScale));
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio ((double) baseW / (double) baseH);

        setupDone = true;
        setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
        resized();
        content.resized();
    }

    void TleetEditorBase::applyScale (float scale)
    {
        scale = juce::jlimit (minScale, maxScale, scale);
        setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
    }

    void TleetEditorBase::resized()
    {
        const float scale = (float) getWidth() / (float) baseW;
        content.setTransform (juce::AffineTransform::scale (scale));
        if (setupDone)
            baseProcessor.setUiProperty ("scale", std::round (scale * 100.0f) / 100.0f);
    }

    void TleetEditorBase::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);
    }

    void TleetEditorBase::Content::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);
        owner.paintPanel (g, getLocalBounds().withTrimmedTop (toolbarHeight));
    }

    void TleetEditorBase::Content::resized()
    {
        owner.toolbar.setBounds (getLocalBounds().removeFromTop (toolbarHeight));
        owner.about.setBounds (getLocalBounds());
        owner.layoutPanel (getLocalBounds().withTrimmedTop (toolbarHeight));
    }
}
