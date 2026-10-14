#include "AboutPanel.h"
#include "TleetLookAndFeel.h"
#include "core/Config.h"

#ifndef TLEET_VERSION_STRING
 #define TLEET_VERSION_STRING "dev"
#endif
#ifndef TLEET_BUILD_ID
 #define TLEET_BUILD_ID "local"
#endif

namespace tleet::ui
{
    static void setupLink (juce::TextButton& b, const char* url)
    {
        const juce::String u (url);
        b.setEnabled (u.isNotEmpty());
        b.setTooltip (u.isNotEmpty() ? u : juce::String ("Link not configured yet"));
        b.onClick = [u] { if (u.isNotEmpty()) juce::URL (u).launchInDefaultBrowser(); };
    }

    AboutPanel::AboutPanel (juce::String name, juce::String display)
        : pluginName (std::move (name)), displayName (std::move (display))
    {
        setupLink (vkButton, config::vkDonutUrl);
        setupLink (boostyButton, config::boostyUrl);
        setupLink (sourceButton, config::sourceCodeUrl);
        closeButton.onClick = [this] { setVisible (false); };

        for (auto* b : { &vkButton, &boostyButton, &sourceButton, &closeButton })
            addAndMakeVisible (b);
    }

    void AboutPanel::resized()
    {
        card = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 40), juce::jmin (430, getHeight() - 40));
        auto r = card.reduced (24);
        auto buttons = r.removeFromBottom (32);
        closeButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (10);
        sourceButton.setBounds (buttons.removeFromLeft (120));

        auto donate = r.removeFromBottom (70).removeFromBottom (32);
        vkButton.setBounds (donate.removeFromLeft (120));
        donate.removeFromLeft (10);
        boostyButton.setBounds (donate.removeFromLeft (120));
    }

    void AboutPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.7f));
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card.toFloat(), 8.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (card.toFloat(), 8.0f, 1.5f);

        auto r = card.reduced (24);
        g.setFont (TleetLookAndFeel::labelFont (26.0f));
        g.drawText ("TLEET // " + pluginName, r.removeFromTop (34), juce::Justification::centredLeft);

        g.setColour (colours::text);
        g.setFont (TleetLookAndFeel::labelFont (14.0f, false));
        g.drawText (displayName, r.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.drawText (juce::String ("Version ") + TLEET_VERSION_STRING + "  ·  build " + TLEET_BUILD_ID,
                    r.removeFromTop (22), juce::Justification::centredLeft);
        r.removeFromTop (10);

        g.setColour (colours::text);
        g.setFont (TleetLookAndFeel::labelFont (13.0f, false));
        const juce::String notices =
            "TLEET Audio plugins are free software under the GNU AGPL v3.\n"
            "Built with JUCE (AGPLv3). VST is a registered trademark of Steinberg Media Technologies GmbH.\n"
            "Hardware names mentioned in documentation belong to their owners and are used only to "
            "describe the sound being modelled; TLEET is not affiliated with them.";
        g.drawFittedText (notices, r.removeFromTop (110), juce::Justification::topLeft, 8);

        g.setColour (colours::accent);
        g.setFont (TleetLookAndFeel::labelFont (15.0f));
        g.drawText ("SUPPORT TLEET AUDIO", r.removeFromTop (24), juce::Justification::centredLeft);
    }

    void AboutPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (! card.contains (e.getPosition()))
            setVisible (false);
    }
}
