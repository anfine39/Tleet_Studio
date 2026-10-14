#include "Toolbar.h"
#include "TleetLookAndFeel.h"

namespace tleet::ui
{
    Toolbar::Toolbar (TleetProcessorBase& p) : processor (p)
    {
        for (auto* b : { &prevButton, &nextButton, &presetButton, &saveButton, &aButton, &bButton,
                         &copyAbButton, &undoButton, &redoButton, &bypassButton, &scaleButton, &aboutButton })
            addAndMakeVisible (b);

        prevButton.setTooltip ("Previous preset");
        nextButton.setTooltip ("Next preset");
        presetButton.setTooltip ("Browse presets");
        saveButton.setTooltip ("Save, rename, delete presets; copy/paste state");
        aButton.setTooltip ("Switch to setting A");
        bButton.setTooltip ("Switch to setting B");
        copyAbButton.setTooltip ("Copy the active setting to the other slot");
        undoButton.setTooltip ("Undo");
        redoButton.setTooltip ("Redo");
        bypassButton.setTooltip ("Bypass the whole plugin (latency-compensated)");
        scaleButton.setTooltip ("Window size");
        aboutButton.setTooltip ("About TLEET Audio");

        aButton.setClickingTogglesState (false);
        bButton.setClickingTogglesState (false);

        prevButton.onClick = [this] { processor.stepPreset (-1); refresh(); };
        nextButton.onClick = [this] { processor.stepPreset (+1); refresh(); };
        presetButton.onClick = [this] { showPresetMenu(); };
        saveButton.onClick = [this] { showSaveMenu(); };
        aButton.onClick = [this] { processor.switchToSlot (0); refresh(); };
        bButton.onClick = [this] { processor.switchToSlot (1); refresh(); };
        copyAbButton.onClick = [this] { processor.copyActiveSlotToOther(); refresh(); };
        undoButton.onClick = [this] { processor.undo(); refresh(); };
        redoButton.onClick = [this] { processor.redo(); refresh(); };
        scaleButton.onClick = [this] { showScaleMenu(); };
        aboutButton.onClick = [this] { if (onAbout) onAbout(); };

        if (auto* bypass = processor.apvts.getParameter ("bypass"))
        {
            bypassButton.setClickingTogglesState (true);
            bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*bypass, bypassButton);
        }

        processor.stateChanged.addChangeListener (this);
        processor.presets.addChangeListener (this);
        startTimerHz (5);
        refresh();
    }

    Toolbar::~Toolbar()
    {
        processor.stateChanged.removeChangeListener (this);
        processor.presets.removeChangeListener (this);
    }

    void Toolbar::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }
    void Toolbar::timerCallback() { refresh(); }

    void Toolbar::refresh()
    {
        auto name = processor.presets.getCurrentName();
        if (processor.isPresetModified())
            name << " *";
        presetButton.setButtonText (name);

        aButton.setToggleState (processor.getActiveSlot() == 0, juce::dontSendNotification);
        bButton.setToggleState (processor.getActiveSlot() == 1, juce::dontSendNotification);
        copyAbButton.setButtonText (processor.getActiveSlot() == 0 ? "A>B" : "B>A");
        undoButton.setEnabled (processor.canUndo());
        redoButton.setEnabled (processor.canRedo());

        const double scale = processor.getUiProperty ("scale", 1.0);
        scaleButton.setButtonText (juce::String (juce::roundToInt (scale * 100.0)) + "%");
    }

    void Toolbar::paint (juce::Graphics& g)
    {
        g.setColour (colours::background.withAlpha (0.92f));
        g.fillRect (getLocalBounds());
        g.setColour (colours::edge);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        g.setColour (colours::accent);
        g.setFont (TleetLookAndFeel::labelFont (15.0f));
        g.drawText ("TLEET // " + processor.getPluginId(), getLocalBounds().removeFromLeft (150).withTrimmedLeft (12),
                    juce::Justification::centredLeft);
    }

    void Toolbar::resized()
    {
        auto r = getLocalBounds().reduced (6, 5);
        r.removeFromLeft (150);

        auto place = [&r] (juce::Component& c, int w, int gapAfter = 4)
        {
            c.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (gapAfter);
        };

        place (prevButton, 26, 2);
        place (presetButton, 210, 2);
        place (nextButton, 26);
        place (saveButton, 54, 14);
        place (aButton, 28, 2);
        place (bButton, 28, 2);
        place (copyAbButton, 44, 14);
        place (undoButton, 54, 2);
        place (redoButton, 54, 14);

        aboutButton.setBounds (r.removeFromRight (28));
        r.removeFromRight (4);
        scaleButton.setBounds (r.removeFromRight (56));
        r.removeFromRight (4);
        bypassButton.setBounds (r.removeFromRight (74));
        for (auto& [c, w] : extras)
        {
            r.removeFromRight (6);
            c->setBounds (r.removeFromRight (w));
        }
    }

    void Toolbar::addExtra (juce::Component& c, int width)
    {
        extras.emplace_back (&c, width);
        addAndMakeVisible (c);
        resized();
    }

    // ------------------------------------------------------------------ menus

    void Toolbar::showPresetMenu (juce::Component* target)
    {
        juce::PopupMenu m;
        const auto& list = processor.presets.getPresets();
        const int current = processor.presets.getCurrentIndex();

        m.addItem (1, "Init (default settings)");
        m.addSectionHeader ("Factory");
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].isFactory)
                m.addItem (100 + (int) i, list[i].name, true, (int) i == current);

        m.addSectionHeader ("User");
        bool anyUser = false;
        for (size_t i = 0; i < list.size(); ++i)
            if (! list[i].isFactory)
            {
                m.addItem (100 + (int) i, list[i].name, true, (int) i == current);
                anyUser = true;
            }
        if (! anyUser)
            m.addItem (2, "(no user presets yet)", false);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target != nullptr ? target : &presetButton),
                         [safe = juce::Component::SafePointer<Toolbar> (this)] (int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || result == 0)
                return;
            if (result == 1)
                self->processor.loadInitPreset();
            else if (result >= 100)
                self->processor.loadPreset (result - 100);
            self->refresh();
        });
    }

    void Toolbar::showSaveMenu (juce::Component* target)
    {
        const int current = processor.presets.getCurrentIndex();
        const bool userCurrent = current >= 0 && ! processor.presets.isCurrentFactory();

        juce::PopupMenu m;
        m.addItem (1, "Save", userCurrent);
        m.addItem (2, "Save as...");
        m.addItem (3, "Rename...", userCurrent);
        m.addItem (4, "Delete", userCurrent);
        m.addSeparator();
        m.addItem (5, "Copy plugin state");
        m.addItem (6, "Paste plugin state");
        m.addSeparator();
        m.addItem (7, "Open user preset folder");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target != nullptr ? target : &saveButton),
                         [safe = juce::Component::SafePointer<Toolbar> (this), current] (int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;
            auto& proc = self->processor;

            switch (result)
            {
                case 1:
                    if (auto r = proc.saveUserPreset (proc.presets.getCurrentName()); r.failed())
                        self->showError (r.getErrorMessage());
                    break;
                case 2:
                    self->askForName ("Save preset as", proc.presets.getCurrentName(), [safe] (const juce::String& name)
                    {
                        if (auto* s = safe.getComponent())
                            if (auto r = s->processor.saveUserPreset (name); r.failed())
                                s->showError (r.getErrorMessage());
                    });
                    break;
                case 3:
                    self->askForName ("Rename preset", proc.presets.getCurrentName(), [safe, current] (const juce::String& name)
                    {
                        if (auto* s = safe.getComponent())
                            if (auto r = s->processor.presets.renameUserPreset (current, name); r.failed())
                                s->showError (r.getErrorMessage());
                    });
                    break;
                case 4:
                    if (auto r = proc.presets.deleteUserPreset (current); r.failed())
                        self->showError (r.getErrorMessage());
                    else
                        proc.presets.setCurrentToInit (state::capture (proc.apvts));
                    break;
                case 5:
                    juce::SystemClipboard::copyTextToClipboard (proc.stateToClipboardJson());
                    break;
                case 6:
                    if (auto r = proc.pasteStateFromJson (juce::SystemClipboard::getTextFromClipboard()); r.failed())
                        self->showError (r.getErrorMessage());
                    break;
                case 7:
                {
                    auto dir = proc.presets.getUserDirectory();
                    dir.createDirectory();
                    dir.startAsProcess();
                    break;
                }
                default: break;
            }
            self->refresh();
        });
    }

    void Toolbar::showScaleMenu()
    {
        juce::PopupMenu m;
        const double current = processor.getUiProperty ("scale", 1.0);
        const int sizes[] = { 75, 100, 125, 150, 175, 200 };
        for (int s : sizes)
            m.addItem (s, juce::String (s) + "%", true, juce::roundToInt (current * 100.0) == s);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scaleButton),
                         [safe = juce::Component::SafePointer<Toolbar> (this)] (int result)
        {
            if (auto* self = safe.getComponent(); self != nullptr && result > 0 && self->onScaleChosen)
                self->onScaleChosen ((float) result / 100.0f);
        });
    }

    void Toolbar::deleteCurrentPreset()
    {
        const int current = processor.presets.getCurrentIndex();
        if (current < 0 || processor.presets.isCurrentFactory())
            return;
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Delete preset")
                                          .withMessage ("Delete \"" + processor.presets.getCurrentName() + "\"?")
                                          .withButton ("Delete")
                                          .withButton ("Cancel")
                                          .withAssociatedComponent (this),
                                      [safe = juce::Component::SafePointer<Toolbar> (this), current] (int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || result != 1)
                return;
            auto& proc = self->processor;
            if (auto r = proc.presets.deleteUserPreset (current); r.failed())
                self->showError (r.getErrorMessage());
            else
                proc.presets.setCurrentToInit (state::capture (proc.apvts));
            self->refresh();
        });
    }

    void Toolbar::askForName (const juce::String& title, const juce::String& initial,
                              std::function<void (const juce::String&)> onOk)
    {
        nameDialog = std::make_unique<juce::AlertWindow> (title, "Preset name:", juce::MessageBoxIconType::NoIcon, this);
        nameDialog->addTextEditor ("name", initial);
        nameDialog->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        nameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        nameDialog->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<Toolbar> (this), onOk] (int result)
            {
                auto* self = safe.getComponent();
                if (self == nullptr || self->nameDialog == nullptr)
                    return;
                const auto text = self->nameDialog->getTextEditorContents ("name");
                self->nameDialog.reset();
                if (result == 1)
                    onOk (text);
                self->refresh();
            }), false);
    }

    void Toolbar::showError (const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("TLEET " + processor.getPluginId())
                                          .withMessage (message)
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
    }
}
