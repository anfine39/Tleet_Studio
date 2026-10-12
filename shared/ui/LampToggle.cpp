#include "LampToggle.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    LampToggle::LampToggle (juce::RangedAudioParameter& p, juce::String l, juce::Image sprite,
                            juce::Rectangle<float> bounds, juce::Rectangle<float> hit)
        : label (std::move (l)), lit (std::move (sprite)), litBounds (bounds), hitArea (hit),
          attachment (p, [this] (float plain)
          {
              on = plain >= 0.5f;
              setTooltip (label + ": " + (on ? "On" : "Off"));
              repaint();
              if (auto* h = getAccessibilityHandler())
                  h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
          }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (label);
        attachment.sendInitialUpdate();
    }

    void LampToggle::setOn (bool shouldBeOn)
    {
        attachment.setValueAsCompleteGesture (shouldBeOn ? 1.0f : 0.0f);
        if (onEditFinished) onEditFinished();
    }

    void LampToggle::paint (juce::Graphics& g)
    {
        if (on && lit.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (lit, litBounds, juce::RectanglePlacement::stretchToFit);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (hitArea.reduced (2.0f), 5.0f, 1.0f);
        }
    }

    void LampToggle::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu())
            setOn (! on);
    }

    bool LampToggle::keyPressed (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey)
        {
            setOn (! on);
            return true;
        }
        return false;
    }

    namespace
    {
        class ToggleHandler : public juce::AccessibilityHandler
        {
        public:
            explicit ToggleHandler (LampToggle& t)
                : juce::AccessibilityHandler (t, juce::AccessibilityRole::toggleButton,
                                              juce::AccessibilityActions()
                                                  .addAction (juce::AccessibilityActionType::toggle, [&t] { t.setOn (! t.isOn()); })
                                                  .addAction (juce::AccessibilityActionType::press, [&t] { t.setOn (! t.isOn()); })),
                  toggle (t) {}

            juce::AccessibleState getCurrentState() const override
            {
                auto s = juce::AccessibilityHandler::getCurrentState().withCheckable();
                return toggle.isOn() ? s.withChecked() : s;
            }

        private:
            LampToggle& toggle;
        };
    }

    std::unique_ptr<juce::AccessibilityHandler> LampToggle::createAccessibilityHandler()
    {
        return std::make_unique<ToggleHandler> (*this);
    }
}
