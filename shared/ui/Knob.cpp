#include "Knob.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    static constexpr float startAngle = juce::MathConstants<float>::pi * -0.75f; // 270 degrees of travel
    static constexpr float endAngle   = juce::MathConstants<float>::pi *  0.75f;
    static constexpr float pixelsForFullRange = 250.0f;
    static constexpr float fineFactor = 0.1f;

    Knob::Knob (juce::RangedAudioParameter& p, juce::String l, Size s)
        : param (p), label (std::move (l)), size (s),
          attachment (p, [this] (float v) { valueChangedFromParameter (v); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (label);
        setDescription (param.getName (64));
        attachment.sendInitialUpdate();
    }

    Knob::~Knob() = default;

    void Knob::valueChangedFromParameter (float plainValue)
    {
        normalised = param.convertTo0to1 (plainValue);
        setTooltip (label + ": " + valueText());
        repaint();
        if (auto* h = getAccessibilityHandler())
            h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    juce::String Knob::valueText() const
    {
        auto text = param.getText (normalised, 16);
        if (param.getLabel().isNotEmpty())
            text << " " << param.getLabel();
        return text;
    }

    void Knob::setNormalisedFromUser (float v, bool singleShotGesture)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        const float plain = param.convertFrom0to1 (v);
        if (singleShotGesture)
            attachment.setValueAsCompleteGesture (plain);
        else
            attachment.setValueAsPartOfGesture (plain);
    }

    void Knob::resetToDefault()
    {
        setNormalisedFromUser (param.getDefaultValue(), true);
        if (onEditFinished) onEditFinished();
    }

    // ------------------------------------------------------------------ layout & paint

    void Knob::resized()
    {
        if (hasSkin)
        {
            knobArea = getLocalBounds();
            valueArea = getLocalBounds().removeFromBottom (22).withSizeKeepingCentre (juce::jmin (getWidth(), 110), 22);
            labelArea = {};
            return;
        }

        auto r = getLocalBounds();
        const int textH = size == Size::large ? 20 : 16;
        labelArea = r.removeFromTop (textH);
        valueArea = r.removeFromBottom (textH);
        const int side = juce::jmin (r.getWidth(), r.getHeight());
        knobArea = r.withSizeKeepingCentre (side, side);
    }

    void Knob::paintSkin (juce::Graphics& g)
    {
        const float angle = skin.startAngle + normalised * (skin.endAngle - skin.startAngle);
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        const auto a = skin.centre + dir * skin.innerRadius;
        const auto b = skin.centre + dir * skin.outerRadius;

        // glowing amber pointer, matching the lamp-lit pointers of the artwork
        g.setColour (colours::accent.withAlpha (0.18f));
        g.drawLine ({ a, b }, skin.width * 2.8f);
        g.setColour (colours::accent.withAlpha (0.55f));
        g.drawLine ({ a, b }, skin.width * 1.5f);
        g.setColour (juce::Colour (0xffffd59a));
        g.drawLine ({ a, b }, skin.width * 0.6f);

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::accent.withAlpha (0.5f));
            g.drawEllipse (juce::Rectangle<float> (skin.outerRadius * 2.3f, skin.outerRadius * 2.3f).withCentre (skin.centre), 1.0f);
        }

        if (dragging || hovering)
        {
            auto pill = valueArea.toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.fillRoundedRectangle (pill, 6.0f);
            g.setColour (colours::text);
            g.setFont (TleetLookAndFeel::labelFont (13.0f, false));
            g.drawFittedText (valueText(), valueArea, juce::Justification::centred, 1);
        }
    }

    void Knob::paint (juce::Graphics& g)
    {
        if (hasSkin)
        {
            paintSkin (g);
            return;
        }

        const float labelH = size == Size::large ? 15.0f : 12.0f;

        g.setColour (colours::text);
        g.setFont (TleetLookAndFeel::labelFont (labelH));
        g.drawFittedText (label.toUpperCase(), labelArea, juce::Justification::centred, 1);

        auto area = knobArea.toFloat().reduced (2.0f);
        const auto centre = area.getCentre();
        const float radius = area.getWidth() * 0.5f;
        const float angle = startAngle + normalised * (endAngle - startAngle);

        // scale ticks
        g.setColour (colours::textDim);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const float r1 = radius * 0.93f, r2 = radius * (i % 5 == 0 ? 1.0f : 0.97f);
            g.drawLine (centre.x + r1 * std::sin (a), centre.y - r1 * std::cos (a),
                        centre.x + r2 * std::sin (a), centre.y - r2 * std::cos (a), i % 5 == 0 ? 1.6f : 1.0f);
        }

        // value arc
        const float arcR = radius * 0.86f;
        auto arcPath = [centre, arcR] (float from, float to)
        {
            juce::Path p;
            const int segments = juce::jmax (2, (int) std::ceil (std::abs (to - from) / 0.05f));
            for (int i = 0; i <= segments; ++i)
            {
                const float a = from + (to - from) * (float) i / (float) segments;
                const juce::Point<float> pt (centre.x + arcR * std::sin (a), centre.y - arcR * std::cos (a));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            return p;
        };
        const auto track = arcPath (startAngle, endAngle);
        const auto value = arcPath (startAngle, angle);
        const float arcW = juce::jmax (2.0f, radius * 0.07f);
        g.setColour (colours::background);
        g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::accent);
        g.strokePath (value, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // knob body: brushed dark metal with a soft top-left highlight
        const float bodyR = radius * 0.72f;
        auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (body.translated (0.0f, bodyR * 0.08f).expanded (bodyR * 0.04f));
        juce::ColourGradient grad (colours::panelLight.brighter (0.35f), body.getX(), body.getY(),
                                   colours::background, body.getRight(), body.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (body);

        // knurling
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        const int knurls = size == Size::large ? 36 : 24;
        for (int i = 0; i < knurls; ++i)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) knurls;
            g.drawLine (centre.x + bodyR * 0.9f * std::sin (a), centre.y - bodyR * 0.9f * std::cos (a),
                        centre.x + bodyR * std::sin (a), centre.y - bodyR * std::cos (a), 1.0f);
        }

        g.setColour (colours::edge);
        g.drawEllipse (body, 1.2f);
        auto cap = body.reduced (bodyR * 0.2f);
        g.setGradientFill (juce::ColourGradient (colours::panelLight, cap.getX(), cap.getY(),
                                                 colours::panel.darker (0.3f), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);

        // pointer
        g.setColour (colours::text);
        const float p1 = bodyR * 0.25f, p2 = bodyR * 0.92f;
        g.drawLine (centre.x + p1 * std::sin (angle), centre.y - p1 * std::cos (angle),
                    centre.x + p2 * std::sin (angle), centre.y - p2 * std::cos (angle),
                    juce::jmax (2.0f, bodyR * 0.08f));

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::accent.withAlpha (0.6f));
            g.drawEllipse (area.reduced (1.0f), 1.0f);
        }

        g.setColour (dragging ? colours::accent : colours::textDim);
        g.setFont (TleetLookAndFeel::labelFont (labelH - 1.0f, false));
        g.drawFittedText (valueText(), valueArea, juce::Justification::centred, 1);
    }

    // ------------------------------------------------------------------ mouse

    void Knob::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showContextMenu();
            return;
        }

        if (e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            resetToDefault();
            return;
        }

        dragging = true;
        dragStartY = e.getPosition().y;
        dragStartValue = normalised;
        attachment.beginGesture();
        repaint();
    }

    void Knob::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return;

        // Re-anchor on every move so pressing/releasing Shift mid-drag never jumps.
        const float dy = (float) (dragStartY - e.getPosition().y);
        const float sensitivity = e.mods.isShiftDown() ? fineFactor : 1.0f;
        const float v = juce::jlimit (0.0f, 1.0f, dragStartValue + dy / pixelsForFullRange * sensitivity);
        dragStartY = e.getPosition().y;
        dragStartValue = v;
        setNormalisedFromUser (v, false);
    }

    void Knob::mouseUp (const juce::MouseEvent&)
    {
        if (! dragging)
            return;
        dragging = false;
        attachment.endGesture();
        repaint();
        if (onEditFinished) onEditFinished();
    }

    void Knob::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (valueArea.contains (e.getPosition()))
            showValueEntry();
        else
            resetToDefault();
    }

    void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const float delta = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        if (std::abs (delta) < 1.0e-6f)
            return;

        const int steps = juce::jmax (2, (int) param.getNumSteps());
        float step = steps < 200 ? 1.0f / (float) (steps - 1) : (e.mods.isShiftDown() ? 0.004f : 0.02f);
        if (steps < 200 && std::abs (delta) < 0.05f)
            return;
        setNormalisedFromUser (normalised + (delta > 0 ? step : -step), true);
        if (onEditFinished) onEditFinished();
    }

    bool Knob::keyPressed (const juce::KeyPress& key)
    {
        const bool fine = key.getModifiers().isShiftDown();
        // stepped parameters (choices) move one position per key press
        const int steps = juce::jmax (2, (int) param.getNumSteps());
        const float step = steps < 200 ? 1.0f / (float) (steps - 1) : (fine ? 0.005f : 0.025f);
        const int code = key.getKeyCode();

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
            setNormalisedFromUser (normalised + step, true);
        else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
            setNormalisedFromUser (normalised - step, true);
        else if (code == juce::KeyPress::returnKey)
            showValueEntry();
        else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
            resetToDefault();
        else
            return false;

        if (onEditFinished) onEditFinished();
        return true;
    }

    // ------------------------------------------------------------------ menus

    void Knob::showContextMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (label + ": " + valueText());
        m.addItem (1, "Reset to default");
        m.addItem (2, "Enter value...");
        m.addItem (3, "Copy value");
        m.addItem (4, "Paste value", juce::SystemClipboard::getTextFromClipboard().trim().isNotEmpty());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<Knob> (this)] (int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;

            switch (result)
            {
                case 1: self->resetToDefault(); break;
                case 2: self->showValueEntry(); break;
                case 3: juce::SystemClipboard::copyTextToClipboard (self->param.getText (self->normalised, 32)); break;
                case 4:
                {
                    const auto text = juce::SystemClipboard::getTextFromClipboard().trim();
                    self->setNormalisedFromUser (self->param.getValueForText (text), true);
                    if (self->onEditFinished) self->onEditFinished();
                    break;
                }
                default: break;
            }
        });
    }

    void Knob::showValueEntry()
    {
        valueEditor = std::make_unique<juce::TextEditor>();
        auto* editor = valueEditor.get();
        editor->setText (param.getText (normalised, 32), false);
        editor->selectAll();
        editor->setJustification (juce::Justification::centred);
        editor->setFont (TleetLookAndFeel::labelFont (14.0f, false));
        editor->setBounds (valueArea.expanded (8, 3));
        addAndMakeVisible (editor);
        editor->grabKeyboardFocus();

        auto done = std::make_shared<bool> (false);
        auto finish = [safe = juce::Component::SafePointer<Knob> (this), editor, done] (bool commit)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || *done)
                return;
            *done = true;
            if (commit)
            {
                self->setNormalisedFromUser (self->param.getValueForText (editor->getText().trim()), true);
                if (self->onEditFinished) self->onEditFinished();
            }
            juce::MessageManager::callAsync ([safe]
            {
                if (auto* s = safe.getComponent())
                {
                    s->valueEditor.reset();
                    s->grabKeyboardFocus();
                }
            });
        };

        editor->onReturnKey = [finish] { finish (true); };
        editor->onEscapeKey = [finish] { finish (false); };
        editor->onFocusLost = [finish] { finish (false); };
    }

    // ------------------------------------------------------------------ accessibility

    namespace
    {
        class KnobValueInterface : public juce::AccessibilityRangedNumericValueInterface
        {
        public:
            explicit KnobValueInterface (Knob& k) : knob (k) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return knob.getNormalisedValue(); }
            void setValue (double v) override
            {
                knob.setNormalisedFromUser ((float) v, true);
                if (knob.onEditFinished) knob.onEditFinished();
            }
            AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }

        private:
            Knob& knob;
        };
    }

    std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
            juce::AccessibilityHandler::Interfaces { std::make_unique<KnobValueInterface> (*this) });
    }
}
