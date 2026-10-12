#include "LampButtonGroup.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    LampButtonGroup::LampButtonGroup (juce::RangedAudioParameter& p, juce::String l, juce::StringArray s, Geometry g)
        : label (std::move (l)), states (std::move (s)), geo (std::move (g)),
          attachment (p, [this] (float plain)
          {
              selected = juce::jlimit (0, states.size() - 1, juce::roundToInt (plain));
              setTooltip (label + ": " + states[selected]);
              repaint();
              if (auto* h = getAccessibilityHandler())
                  h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
          }, nullptr)
    {
        jassert ((int) geo.hitboxes.size() == states.size() && geo.litBounds.size() == geo.hitboxes.size());
        setWantsKeyboardFocus (true);
        setTitle (label);
        attachment.sendInitialUpdate();
    }

    void LampButtonGroup::select (int index)
    {
        attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, states.size() - 1, index));
        if (onEditFinished) onEditFinished();
    }

    int LampButtonGroup::indexAt (juce::Point<float> p) const noexcept
    {
        for (size_t i = 0; i < geo.hitboxes.size(); ++i)
            if (geo.hitboxes[i].contains (p))
                return (int) i;
        return -1;
    }

    bool LampButtonGroup::hitTest (int x, int y)
    {
        return indexAt ({ (float) x, (float) y }) >= 0;
    }

    void LampButtonGroup::paint (juce::Graphics& g)
    {
        if (geo.lit.isValid() && selected >= 0 && selected < (int) geo.litBounds.size())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (geo.lit, geo.litBounds[(size_t) selected], juce::RectanglePlacement::stretchToFit);
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (geo.hitboxes[(size_t) selected].reduced (2.0f), 5.0f, 1.0f);
        }
    }

    void LampButtonGroup::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        const int i = indexAt (e.position);
        if (i >= 0)
            select (i);
    }

    bool LampButtonGroup::keyPressed (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)       select (selected + 1);
        else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey) select (selected - 1);
        else return false;
        return true;
    }

    namespace
    {
        class GroupValue : public juce::AccessibilityTextValueInterface
        {
        public:
            explicit GroupValue (LampButtonGroup& s) : group (s) {}
            bool isReadOnly() const override { return false; }
            juce::String getCurrentValueAsString() const override { return group.getStates()[group.getSelectedIndex()]; }
            void setValueAsString (const juce::String& v) override
            {
                const int idx = group.getStates().indexOf (v, true);
                if (idx >= 0) group.select (idx);
            }
        private:
            LampButtonGroup& group;
        };
    }

    std::unique_ptr<juce::AccessibilityHandler> LampButtonGroup::createAccessibilityHandler()
    {
        juce::AccessibilityActions actions;
        actions.addAction (juce::AccessibilityActionType::press,
                           [this] { select ((selected + 1) % states.size()); });
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::comboBox, std::move (actions),
            juce::AccessibilityHandler::Interfaces { std::make_unique<GroupValue> (*this) });
    }
}
