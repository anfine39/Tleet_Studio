#include "PresetManager.h"
#include "core/Config.h"

namespace tleet
{
    PresetManager::PresetManager (juce::String id, juce::File dir)
        : pluginId (std::move (id)), userDir (std::move (dir))
    {
    }

    juce::File PresetManager::defaultUserDirectory (const juce::String& id)
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile (config::companyName)
                   .getChildFile (id)
                   .getChildFile ("Presets");
    }

    juce::String PresetManager::toJson (const juce::String& name, const ParamSnapshot& params,
                                        const juce::var& pluginSpecific) const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", "tleet-preset");
        root->setProperty ("version", config::presetFormatVersion);
        root->setProperty ("plugin", pluginId);
        root->setProperty ("name", name);
        root->setProperty ("params", params.toVar());
        if (! pluginSpecific.isVoid())
            root->setProperty ("pluginSpecific", pluginSpecific);
        return juce::JSON::toString (juce::var (root));
    }

    juce::Result PresetManager::parse (const juce::String& json, Preset& out) const
    {
        juce::var root;
        if (auto r = juce::JSON::parse (json, root); r.failed())
            return juce::Result::fail ("Preset is not valid JSON: " + r.getErrorMessage());

        if (root.getProperty ("format", {}).toString() != "tleet-preset")
            return juce::Result::fail ("Not a TLEET preset file");

        const auto plugin = root.getProperty ("plugin", {}).toString();
        if (plugin != pluginId)
            return juce::Result::fail ("This preset belongs to TLEET " + plugin + ", not TLEET " + pluginId);

        if ((int) root.getProperty ("version", 0) > config::presetFormatVersion)
            return juce::Result::fail ("Preset was saved by a newer TLEET version");

        out.name = root.getProperty ("name", "Untitled").toString();
        out.params = ParamSnapshot::fromVar (root.getProperty ("params", {}));
        out.pluginSpecific = root.getProperty ("pluginSpecific", {});
        return juce::Result::ok();
    }

    juce::Result PresetManager::addFactoryPreset (const juce::String& json)
    {
        Preset p;
        if (auto r = parse (json, p); r.failed())
            return r;
        p.isFactory = true;
        presets.push_back (std::move (p));
        sortPresets();
        return juce::Result::ok();
    }

    void PresetManager::rescanUserPresets()
    {
        presets.erase (std::remove_if (presets.begin(), presets.end(),
                                       [] (const Preset& p) { return ! p.isFactory; }),
                       presets.end());

        if (userDir.isDirectory())
        {
            for (const auto& f : userDir.findChildFiles (juce::File::findFiles, false,
                                                         juce::String ("*") + fileExtension))
            {
                Preset p;
                if (parse (f.loadFileAsString(), p).wasOk())
                {
                    p.isFactory = false;
                    p.file = f;
                    p.name = f.getFileNameWithoutExtension();
                    presets.push_back (std::move (p));
                }
            }
        }

        sortPresets();
        currentIndex = indexOf (currentName, currentIsFactory);
        sendChangeMessage();
    }

    void PresetManager::sortPresets()
    {
        std::stable_sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
        {
            if (a.isFactory != b.isFactory)
                return a.isFactory; // factory first
            return a.isFactory ? false : a.name.compareNatural (b.name) < 0;
        });
    }

    int PresetManager::indexOf (const juce::String& name, bool factory) const
    {
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].isFactory == factory && presets[i].name == name)
                return (int) i;
        return -1;
    }

    void PresetManager::setCurrent (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;
        currentIndex = index;
        currentName = presets[(size_t) index].name;
        currentIsFactory = presets[(size_t) index].isFactory;
        reference = presets[(size_t) index].params;
        sendChangeMessage();
    }

    void PresetManager::setCurrentToInit (const ParamSnapshot& defaults)
    {
        currentIndex = -1;
        currentName = "Init";
        currentIsFactory = true;
        reference = defaults;
        sendChangeMessage();
    }

    void PresetManager::setCurrentByName (const juce::String& name, bool isFactory, const ParamSnapshot& ref)
    {
        currentName = name;
        currentIsFactory = isFactory;
        currentIndex = indexOf (name, isFactory);
        reference = currentIndex >= 0 ? presets[(size_t) currentIndex].params : ref;
        sendChangeMessage();
    }

    juce::String PresetManager::getCurrentName() const { return currentName; }

    bool PresetManager::isModified (const ParamSnapshot& now) const
    {
        if (reference.isEmpty())
            return false;
        // Compare only the keys the reference defines plus anything extra in `now`.
        for (const auto& [id, value] : now.values)
        {
            auto it = reference.values.find (id);
            if (it == reference.values.end())
                continue;
            if (std::abs (it->second - value) > 1.0e-3f * juce::jmax (1.0f, std::abs (value)))
                return true;
        }
        return false;
    }

    static juce::String legalName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).substring (0, 64);
    }

    juce::Result PresetManager::saveUserPreset (const juce::String& name, const ParamSnapshot& params,
                                                const juce::var& pluginSpecific)
    {
        const auto clean = legalName (name);
        if (clean.isEmpty())
            return juce::Result::fail ("Preset name is empty");

        if (! userDir.createDirectory())
            return juce::Result::fail ("Cannot create " + userDir.getFullPathName());

        auto file = userDir.getChildFile (clean + fileExtension);
        if (! file.replaceWithText (toJson (clean, params, pluginSpecific)))
            return juce::Result::fail ("Cannot write " + file.getFullPathName());

        currentName = clean;
        currentIsFactory = false;
        reference = params;
        rescanUserPresets();
        return juce::Result::ok();
    }

    juce::Result PresetManager::renameUserPreset (int index, const juce::String& newName)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].isFactory)
            return juce::Result::fail ("Only user presets can be renamed");

        const auto clean = legalName (newName);
        if (clean.isEmpty())
            return juce::Result::fail ("Preset name is empty");

        auto& p = presets[(size_t) index];
        auto target = userDir.getChildFile (clean + fileExtension);
        if (target.exists())
            return juce::Result::fail ("A preset with this name already exists");

        if (! target.replaceWithText (toJson (clean, p.params, p.pluginSpecific)) || ! p.file.deleteFile())
            return juce::Result::fail ("Cannot rename preset file");

        if (currentName == p.name && ! currentIsFactory)
            currentName = clean;

        rescanUserPresets();
        return juce::Result::ok();
    }

    juce::Result PresetManager::deleteUserPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].isFactory)
            return juce::Result::fail ("Only user presets can be deleted");

        if (! presets[(size_t) index].file.deleteFile())
            return juce::Result::fail ("Cannot delete preset file");

        rescanUserPresets();
        return juce::Result::ok();
    }
}
