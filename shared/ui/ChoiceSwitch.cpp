#include "ChoiceSwitch.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    ChoiceSwitch::ChoiceSwitch (juce::RangedAudioParameter& p, juce::String l, juce::StringArray opts)
        : label (std::move (l)), options (std::move (opts))
    {
        setWantsKeyboardFocus (true);
        setTitle (label);
        attachment = std::make_unique<juce::ParameterAttachment> (
            p, [this] (float plain) { showSelection (juce::roundToInt (plain)); }, nullptr);
        attachment->sendInitialUpdate();
    }

    ChoiceSwitch::ChoiceSwitch (juce::String l, juce::StringArray opts, int initialIndex, std::function<void (int)> cb)
        : label (std::move (l)), options (std::move (opts)), onSelect (std::move (cb))
    {
        setWantsKeyboardFocus (true);
        setTitle (label);
        showSelection (initialIndex);
    }

    void ChoiceSwitch::showSelection (int index)
    {
        selected = juce::jlimit (0, juce::jmax (0, options.size() - 1), index);
        setTooltip (label + ": " + options[selected]);
        repaint();
        if (auto* h = getAccessibilityHandler())
            h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    void ChoiceSwitch::select (int index)
    {
        index = juce::jlimit (0, options.size() - 1, index);
        if (attachment != nullptr)
        {
            attachment->setValueAsCompleteGesture ((float) index);
        }
        else
        {
            showSelection (index);
            if (onSelect) onSelect (index);
        }
        if (onEditFinished) onEditFinished();
    }

    juce::Rectangle<int> ChoiceSwitch::segmentBounds (int index) const
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        const int w = r.getWidth() / juce::jmax (1, options.size());
        return { r.getX() + index * w, r.getY(), w, r.getHeight() };
    }

    void ChoiceSwitch::paint (juce::Graphics& g)
    {
        g.setColour (colours::text);
        g.setFont (TleetLookAndFeel::labelFont (12.0f));
        g.drawFittedText (label.toUpperCase(), getLocalBounds().removeFromTop (16), juce::Justification::centred, 1);

        auto all = getLocalBounds().withTrimmedTop (18).toFloat().reduced (1.0f);
        g.setColour (colours::background);
        g.fillRoundedRectangle (all, 4.0f);
        g.setColour (hasKeyboardFocus (false) ? colours::accent.withAlpha (0.6f) : colours::edge);
        g.drawRoundedRectangle (all, 4.0f, 1.0f);

        for (int i = 0; i < options.size(); ++i)
        {
            auto seg = segmentBounds (i).toFloat().reduced (3.0f);
            const bool on = i == selected;
            if (on)
            {
                g.setColour (colours::accentDim);
                g.fillRoundedRectangle (seg, 3.0f);
            }

            auto led = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ seg.getX() + 9.0f, seg.getCentreY() });
            g.setColour (on ? colours::accent : colours::panelLight);
            g.fillEllipse (led);
            if (on)
            {
                g.setColour (colours::accent.withAlpha (0.3f));
                g.fillEllipse (led.expanded (3.0f));
            }

            g.setColour (on ? colours::text : colours::textDim);
            g.setFont (TleetLookAndFeel::labelFont (12.0f));
            g.drawFittedText (options[i].toUpperCase(), seg.withTrimmedLeft (16.0f).toNearestInt(),
                              juce::Justification::centred, 1);
        }
    }

    void ChoiceSwitch::mouseDown (const juce::MouseEvent& e)
    {
        for (int i = 0; i < options.size(); ++i)
            if (segmentBounds (i).contains (e.getPosition()))
            {
                select (i);
                return;
            }
        select ((selected + 1) % options.size()); // click on the label cycles
    }

    bool ChoiceSwitch::keyPressed (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)        select (selected + 1);
        else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)  select (selected - 1);
        else if (code == juce::KeyPress::spaceKey) select ((selected + 1) % options.size());
        else return false;
        return true;
    }

    namespace
    {
        class SwitchValueInterface : public juce::AccessibilityTextValueInterface
        {
        public:
            explicit SwitchValueInterface (ChoiceSwitch& s) : sw (s) {}
            bool isReadOnly() const override { return false; }
            juce::String getCurrentValueAsString() const override { return sw.getOptions()[sw.getSelectedIndex()]; }
            void setValueAsString (const juce::String& v) override
            {
                const int idx = sw.getOptions().indexOf (v, true);
                if (idx >= 0) sw.select (idx);
            }
        private:
            ChoiceSwitch& sw;
        };
    }

    std::unique_ptr<juce::AccessibilityHandler> ChoiceSwitch::createAccessibilityHandler()
    {
        juce::AccessibilityActions actions;
        actions.addAction (juce::AccessibilityActionType::press,
                           [this] { select ((selected + 1) % options.size()); });
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::comboBox, std::move (actions),
            juce::AccessibilityHandler::Interfaces { std::make_unique<SwitchValueInterface> (*this) });
    }
}
