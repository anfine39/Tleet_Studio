#include "PluginState.h"

namespace tleet
{
    bool ParamSnapshot::approximatelyEquals (const ParamSnapshot& other, float tolerance) const
    {
        if (values.size() != other.values.size())
            return false;

        for (const auto& [id, value] : values)
        {
            auto it = other.values.find (id);
            if (it == other.values.end())
                return false;

            const float scale = juce::jmax (1.0f, std::abs (value));
            if (std::abs (it->second - value) > tolerance * scale)
                return false;
        }
        return true;
    }

    juce::var ParamSnapshot::toVar() const
    {
        auto* obj = new juce::DynamicObject();
        for (const auto& [id, value] : values)
            obj->setProperty (id, value);
        return juce::var (obj);
    }

    ParamSnapshot ParamSnapshot::fromVar (const juce::var& v)
    {
        ParamSnapshot s;
        if (auto* obj = v.getDynamicObject())
            for (const auto& prop : obj->getProperties())
                if (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool())
                    s.values[prop.name.toString()] = (float) (double) prop.value;
        return s;
    }

    namespace state
    {
        bool isExcludedFromSnapshots (const juce::String& paramId)
        {
            return paramId == "bypass";
        }

        static juce::RangedAudioParameter* asRanged (juce::AudioProcessorParameter* p)
        {
            return dynamic_cast<juce::RangedAudioParameter*> (p);
        }

        ParamSnapshot capture (juce::AudioProcessorValueTreeState& apvts)
        {
            ParamSnapshot s;
            for (auto* p : apvts.processor.getParameters())
                if (auto* r = asRanged (p))
                    if (! isExcludedFromSnapshots (r->paramID))
                        s.values[r->paramID] = r->convertFrom0to1 (r->getValue());
            return s;
        }

        ParamSnapshot defaults (juce::AudioProcessorValueTreeState& apvts)
        {
            ParamSnapshot s;
            for (auto* p : apvts.processor.getParameters())
                if (auto* r = asRanged (p))
                    if (! isExcludedFromSnapshots (r->paramID))
                        s.values[r->paramID] = r->convertFrom0to1 (r->getDefaultValue());
            return s;
        }

        void apply (juce::AudioProcessorValueTreeState& apvts, const ParamSnapshot& snapshot,
                    bool resetMissingToDefault)
        {
            for (auto* p : apvts.processor.getParameters())
            {
                auto* r = asRanged (p);
                if (r == nullptr || isExcludedFromSnapshots (r->paramID))
                    continue;

                float normalised;
                if (auto it = snapshot.values.find (r->paramID); it != snapshot.values.end())
                    normalised = r->convertTo0to1 (r->getNormalisableRange().snapToLegalValue (it->second));
                else if (resetMissingToDefault)
                    normalised = r->getDefaultValue();
                else
                    continue;

                if (std::abs (normalised - r->getValue()) > 1.0e-7f)
                {
                    r->beginChangeGesture();
                    r->setValueNotifyingHost (normalised);
                    r->endChangeGesture();
                }
            }
        }
    }
}
