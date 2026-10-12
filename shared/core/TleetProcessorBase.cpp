#include "TleetProcessorBase.h"
#include "core/Config.h"

namespace tleet
{
    namespace ids
    {
        static const juce::Identifier root { "TLEET_STATE" }, version { "version" }, plugin { "plugin" },
            params { "PARAMS" }, preset { "PRESET" }, name { "name" }, factory { "factory" },
            ab { "AB" }, active { "active" }, slotA { "A" }, slotB { "B" },
            ui { "UI" }, pluginSpecific { "PLUGIN_SPECIFIC" }, json { "json" };
    }

    TleetProcessorBase::TleetProcessorBase (const BusesProperties& buses,
                                            juce::AudioProcessorValueTreeState::ParameterLayout layout,
                                            const juce::String& id)
        : juce::AudioProcessor (buses),
          apvts (*this, nullptr, ids::params, std::move (layout)),
          presets (id, PresetManager::defaultUserDirectory (id)),
          pluginId (id)
    {
        uiState.setProperty ("scale", 1.0, nullptr);
        history.resetTo (state::capture (apvts));
        presets.setCurrentToInit (state::defaults (apvts));
    }

    TleetProcessorBase::~TleetProcessorBase() = default;

    juce::AudioProcessorParameter* TleetProcessorBase::getBypassParameter() const
    {
        return apvts.getParameter ("bypass");
    }

    void TleetProcessorBase::addFactoryPresets (const juce::StringArray& jsonPresets)
    {
        for (const auto& json : jsonPresets)
        {
            auto r = presets.addFactoryPreset (json);
            jassert (r.wasOk()); // a broken factory preset is a build error
            juce::ignoreUnused (r);
        }
        presets.rescanUserPresets();
    }

    // ------------------------------------------------------------------ state

    void TleetProcessorBase::getStateInformation (juce::MemoryBlock& dest)
    {
        juce::ValueTree root (ids::root);
        root.setProperty (ids::version, config::stateSchemaVersion, nullptr);
        root.setProperty (ids::plugin, pluginId, nullptr);
        root.appendChild (apvts.copyState(), nullptr);

        {
            const juce::ScopedLock sl (stateLock);

            juce::ValueTree p (ids::preset);
            p.setProperty (ids::name, presets.getCurrentName(), nullptr);
            p.setProperty (ids::factory, presets.isCurrentFactory(), nullptr);
            root.appendChild (p, nullptr);

            juce::ValueTree ab (ids::ab);
            ab.setProperty (ids::active, activeSlot, nullptr);
            ab.setProperty (ids::slotA, juce::JSON::toString (slots[0].toVar(), true), nullptr);
            ab.setProperty (ids::slotB, juce::JSON::toString (slots[1].toVar(), true), nullptr);
            root.appendChild (ab, nullptr);

            root.appendChild (uiState.createCopy(), nullptr);

            juce::ValueTree ps (ids::pluginSpecific);
            ps.setProperty (ids::json, juce::JSON::toString (getPluginSpecificState(), true), nullptr);
            root.appendChild (ps, nullptr);
        }

        if (auto xml = root.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void TleetProcessorBase::setStateInformation (const void* data, int size)
    {
        auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr)
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.hasType (ids::root) || tree.getProperty (ids::plugin).toString() != pluginId)
            return; // not ours: keep current state rather than loading garbage

        const int version = tree.getProperty (ids::version, 0);
        if (version < config::stateSchemaVersion)
            migrateState (tree, version);

        restoreFromTree (tree);
    }

    void TleetProcessorBase::migrateState (juce::ValueTree&, int) {}

    void TleetProcessorBase::restoreFromTree (juce::ValueTree tree)
    {
        if (auto params = tree.getChildWithName (ids::params); params.isValid())
            apvts.replaceState (params);

        {
            const juce::ScopedLock sl (stateLock);

            if (auto ab = tree.getChildWithName (ids::ab); ab.isValid())
            {
                activeSlot = juce::jlimit (0, 1, (int) ab.getProperty (ids::active, 0));
                slots[0] = ParamSnapshot::fromVar (juce::JSON::parse (ab.getProperty (ids::slotA).toString()));
                slots[1] = ParamSnapshot::fromVar (juce::JSON::parse (ab.getProperty (ids::slotB).toString()));
            }

            if (auto ui = tree.getChildWithName (ids::ui); ui.isValid())
                uiState.copyPropertiesFrom (ui, nullptr);

            if (auto ps = tree.getChildWithName (ids::pluginSpecific); ps.isValid())
                setPluginSpecificState (juce::JSON::parse (ps.getProperty (ids::json).toString()));
        }

        const auto presetTree = tree.getChildWithName (ids::preset);
        const auto presetName = presetTree.getProperty (ids::name, "Init").toString();
        const bool presetFactory = presetTree.getProperty (ids::factory, true);

        // History and preset bookkeeping live on the message thread.
        juce::MessageManager::callAsync ([safe = juce::WeakReference<TleetProcessorBase> (this), presetName, presetFactory]
        {
            if (auto* self = safe.get())
            {
                auto now = state::capture (self->apvts);
                self->presets.setCurrentByName (presetName, presetFactory, now);
                self->history.resetTo (now);
                self->stateChanged.sendChangeMessage();
            }
        });
    }

    // ------------------------------------------------------------------ presets

    void TleetProcessorBase::applySnapshot (const ParamSnapshot& s)
    {
        state::apply (apvts, s, false);
    }

    void TleetProcessorBase::loadPreset (int index)
    {
        const auto& list = presets.getPresets();
        if (! juce::isPositiveAndBelow (index, (int) list.size()))
            return;

        state::apply (apvts, list[(size_t) index].params, true);
        setPluginSpecificState (list[(size_t) index].pluginSpecific);
        presets.setCurrent (index);
        commitHistory();
    }

    void TleetProcessorBase::loadInitPreset()
    {
        const auto defaults = state::defaults (apvts);
        state::apply (apvts, defaults, true);
        setPluginSpecificState ({});
        presets.setCurrentToInit (defaults);
        commitHistory();
    }

    void TleetProcessorBase::stepPreset (int delta)
    {
        const int n = (int) presets.getPresets().size();
        if (n == 0)
            return;
        int idx = presets.getCurrentIndex();
        idx = idx < 0 ? (delta > 0 ? 0 : n - 1) : (idx + delta + n) % n;
        loadPreset (idx);
    }

    juce::Result TleetProcessorBase::saveUserPreset (const juce::String& name)
    {
        auto r = presets.saveUserPreset (name, state::capture (apvts), getPluginSpecificState());
        stateChanged.sendChangeMessage();
        return r;
    }

    bool TleetProcessorBase::isPresetModified()
    {
        return presets.isModified (state::capture (apvts));
    }

    // ------------------------------------------------------------------ undo / A-B

    void TleetProcessorBase::commitHistory()
    {
        history.commit (state::capture (apvts));
        stateChanged.sendChangeMessage();
    }

    void TleetProcessorBase::undo()
    {
        if (auto* s = history.undo())
            applySnapshot (*s);
        stateChanged.sendChangeMessage();
    }

    void TleetProcessorBase::redo()
    {
        if (auto* s = history.redo())
            applySnapshot (*s);
        stateChanged.sendChangeMessage();
    }

    void TleetProcessorBase::switchToSlot (int slot)
    {
        slot = juce::jlimit (0, 1, slot);
        if (slot == activeSlot)
            return;

        const auto current = state::capture (apvts);
        {
            const juce::ScopedLock sl (stateLock);
            slots[activeSlot] = current;
            if (slots[slot].isEmpty())
                slots[slot] = current;
            activeSlot = slot;
        }
        applySnapshot (slots[slot]);
        commitHistory();
    }

    void TleetProcessorBase::copyActiveSlotToOther()
    {
        const juce::ScopedLock sl (stateLock);
        slots[1 - activeSlot] = state::capture (apvts);
        stateChanged.sendChangeMessage();
    }

    juce::String TleetProcessorBase::stateToClipboardJson()
    {
        return presets.toJson (presets.getCurrentName(), state::capture (apvts), getPluginSpecificState());
    }

    juce::Result TleetProcessorBase::pasteStateFromJson (const juce::String& json)
    {
        PresetManager::Preset p;
        if (auto r = presets.parse (json, p); r.failed())
            return r;

        state::apply (apvts, p.params, false);
        setPluginSpecificState (p.pluginSpecific);
        commitHistory();
        return juce::Result::ok();
    }

    // ------------------------------------------------------------------ UI prefs

    juce::var TleetProcessorBase::getUiProperty (const juce::Identifier& id, const juce::var& fallback) const
    {
        const juce::ScopedLock sl (stateLock);
        return uiState.getProperty (id, fallback);
    }

    void TleetProcessorBase::setUiProperty (const juce::Identifier& id, const juce::var& value)
    {
        const juce::ScopedLock sl (stateLock);
        uiState.setProperty (id, value, nullptr);
    }
}
