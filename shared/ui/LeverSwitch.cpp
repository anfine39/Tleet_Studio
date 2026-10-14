#include "LeverSwitch.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    LeverSwitch::LeverSwitch (juce::RangedAudioParameter& p, juce::String l, juce::StringArray s, Geometry g)
        : label (std::move (l)), states (std::move (s)), geo (std::move (g)),
          attachment (p, [this] (float plain)
          {
              state = juce::jlimit (0, 1, juce::roundToInt (plain));
              shownAngle = state == 0 ? geo.angleState0 : geo.angleState1;
              setTooltip (label + ": " + states[state]);
              repaint();
              if (auto* h = getAccessibilityHandler())
                  h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
          }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (label);
        attachment.sendInitialUpdate();
    }

    void LeverSwitch::setState (int s)
    {
        attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, 1, s));
        if (onEditFinished) onEditFinished();
    }

    void LeverSwitch::paint (juce::Graphics& g)
    {
        if (! geo.lever.isValid())
            return;

        const auto t = juce::AffineTransform::translation (-geo.spritePivot.x, -geo.spritePivot.y)
                           .scaled (geo.spriteScale)
                           .rotated (shownAngle)
                           .translated (geo.pivot.x, geo.pivot.y);

        // soft shadow, then the lever itself
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawImageTransformed (geo.lever, t.translated (2.0f, 3.0f), true);
        g.setOpacity (1.0f);
        g.drawImageTransformed (geo.lever, t, false);

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 1.0f);
        }
    }

    void LeverSwitch::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        setState (e.position.x < geo.pivot.x ? 0 : 1);
    }

    bool LeverSwitch::keyPressed (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::leftKey)       setState (0);
        else if (code == juce::KeyPress::rightKey) setState (1);
        else if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey) setState (1 - state);
        else return false;
        return true;
    }

    namespace
    {
        class LeverValue : public juce::AccessibilityTextValueInterface
        {
        public:
            explicit LeverValue (LeverSwitch& s) : sw (s) {}
            bool isReadOnly() const override { return false; }
            juce::String getCurrentValueAsString() const override { return sw.getStates()[sw.getState()]; }
            void setValueAsString (const juce::String& v) override
            {
                const int idx = sw.getStates().indexOf (v, true);
                if (idx >= 0) sw.setState (idx);
            }
        private:
            LeverSwitch& sw;
        };
    }

    std::unique_ptr<juce::AccessibilityHandler> LeverSwitch::createAccessibilityHandler()
    {
        juce::AccessibilityActions actions;
        actions.addAction (juce::AccessibilityActionType::toggle, [this] { setState (1 - state); });
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::toggleButton, std::move (actions),
            juce::AccessibilityHandler::Interfaces { std::make_unique<LeverValue> (*this) });
    }
}
