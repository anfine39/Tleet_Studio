#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VOIDPresets.h"

namespace tleet::voidrev
{
    VoidProcessor::VoidProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "VOID")
    {
        auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        p.algorithm  = raw (params::algorithm);
        p.color      = raw (params::color);
        p.preDelay   = raw (params::preDelay);
        p.size       = raw (params::size);
        p.decay      = raw (params::decay);
        p.diffusion  = raw (params::diffusion);
        p.drown      = raw (params::drown);
        p.modRate    = raw (params::modRate);
        p.modDepth   = raw (params::modDepth);
        p.width      = raw (params::width);
        p.hpf        = raw (params::hpf);
        p.lpf        = raw (params::lpf);
        p.filterMode = raw (params::filterMode);
        p.duck       = raw (params::duck);
        p.mix        = raw (params::mix);
        p.freeze     = raw (params::freeze);
        p.output     = raw (params::output);
        p.bypass     = raw (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < VOIDPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = VOIDPresets::getNamedResource (VOIDPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    VoidProcessor::~VoidProcessor() = default;

    bool VoidProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
        return (in == mono && out == mono) || (in == stereo && out == stereo) || (in == mono && out == stereo);
    }

    Settings VoidProcessor::currentSettings() const noexcept
    {
        Settings s;
        s.algorithm = juce::jlimit (0, params::numAlgorithms - 1, juce::roundToInt (p.algorithm->load()));
        s.color = juce::jlimit (0, params::numColors - 1, juce::roundToInt (p.color->load()));
        s.preDelayMs = p.preDelay->load();
        s.sizePct = p.size->load();
        s.decayS = p.decay->load();
        s.decayInfinite = params::isInfiniteDecay (p.decay->load());
        s.diffusionPct = p.diffusion->load();
        s.drownPct = p.drown->load();
        s.modRateHz = p.modRate->load();
        s.modDepthPct = p.modDepth->load();
        s.widthPct = p.width->load();
        s.hpfHz = p.hpf->load();
        s.lpfHz = p.lpf->load();
        s.filterMode = juce::jlimit (0, 2, juce::roundToInt (p.filterMode->load()));
        s.duckPct = p.duck->load();
        s.freeze = p.freeze->load() > 0.5f;
        return s;
    }

    void VoidProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);
        setLatencySamples (0);
        dry.prepare (preparedChannels, maxBlock, 0);
        wet.setSize (2, maxBlock, false, true, false);

        engine.setSettings (currentSettings());
        engine.prepare (sampleRate, maxBlock);
        wetSpectrum.setSampleRate (sampleRate);

        outputSmoothed.reset (sampleRate, 0.03);
        outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        mixSmoothed.reset (sampleRate, 0.03);
        mixSmoothed.setCurrentAndTargetValue (p.mix->load() * 0.01f);
        mixRounded = p.mix->load() * 0.01f;
        outRounded = outputSmoothed.getCurrentValue();
        mixRound = dsp::onePoleCoeff (0.002, sampleRate);
        bypassRamp.prepare (sampleRate, 0.02);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
    }

    void VoidProcessor::releaseResources()
    {
        dry.reset();
    }

    void VoidProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numIn = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels(), 2);
        const int numOut = juce::jmin (getMainBusNumOutputChannels(), buffer.getNumChannels(), preparedChannels);
        for (int ch = numOut; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);
        if (total == 0 || numOut == 0 || numIn == 0 || preparedChannels == 0)
            return;

        engine.setSettings (currentSettings());
        outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
        mixSmoothed.setTargetValue (p.mix->load() * 0.01f);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // Hosts may exceed the announced block size; process in prepared-size chunks.
        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numIn, numOut);
    }

    void VoidProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numIn, int numOut)
    {
        float* io[2] {};
        for (int ch = 0; ch < numOut; ++ch)
            io[ch] = buffer.getWritePointer (ch, start);

        // Sanitise the input and meter it; mono -> stereo duplicates the input.
        float inSum = 0.0f, inPk = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
        {
            float squares = 0.0f, peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = dsp::sanitize (io[ch][i]);
                io[ch][i] = x;
                squares += x * x;
                peak = juce::jmax (peak, std::abs (x));
            }
            inputMeters[(size_t) ch].pushLevel (squares, numSamples, peak);
            inSum += squares;
            inPk = juce::jmax (inPk, peak);
        }
        if (numIn == 1)
            inputMeters[1].pushLevel (inSum, numSamples, inPk);
        inputMeter.pushLevel (inSum / (float) numIn, numSamples, inPk);
        if (numIn == 1 && numOut == 2)
            juce::FloatVectorOperations::copy (io[1], io[0], numSamples);
        dry.push (io, numOut, numSamples);

        // Wet path.
        float* wl = wet.getWritePointer (0);
        float* wr = wet.getWritePointer (1);
        engine.process (io[0], numOut > 1 ? io[1] : io[0], wl, wr, numSamples);
        wetSpectrum.push (wl, wr, numSamples);
        if (numOut == 1)
            for (int i = 0; i < numSamples; ++i)
                wl[i] = 0.5f * (wl[i] + wr[i]); // mono output: the mid of the wet image

        // MIX (linear crossfade with the dry input), OUTPUT (after the blend),
        // bypass (fully bypassed = bit-exact input).
        float outSq[2] {}, outPk[2] {};
        const float* wetCh[2] { wl, wr };
        for (int i = 0; i < numSamples; ++i)
        {
            // OUTPUT: 30 ms exponential (dB-linear) ramp, MIX: 30 ms linear ramp;
            // both with rounded corners (2 ms one pole, snapped to the ramp)
            const float outRamp = outputSmoothed.getNextValue();
            const float od = outRamp - outRounded;
            outRounded = std::abs (od) < 1.0e-6f * outRamp ? outRamp : outRounded + mixRound * od;
            const float out = outRounded;
            const float mixLin = mixSmoothed.getNextValue();
            const float md = mixLin - mixRounded;
            mixRounded = std::abs (md) < 1.0e-6f ? mixLin : mixRounded + mixRound * md;
            const float mx = mixRounded;
            const float byp = bypassRamp.next();

            for (int ch = 0; ch < numOut; ++ch)
            {
                const float drySample = dry.getDry (ch)[i];
                // MIX 100 % is exactly the wet signal (no rounding through the blend)
                float y = (mx >= 1.0f ? wetCh[ch][i] : drySample + (wetCh[ch][i] - drySample) * mx) * out;
                y = byp >= 1.0f ? drySample : y + (drySample - y) * byp;
                io[ch][i] = y;
                outSq[ch] += y * y;
                outPk[ch] = juce::jmax (outPk[ch], std::abs (y));
            }
        }
        for (int ch = 0; ch < numOut; ++ch)
            outputMeters[(size_t) ch].pushLevel (outSq[ch], numSamples, outPk[ch]);
        if (numOut == 1)
            outputMeters[1].pushLevel (outSq[0], numSamples, outPk[0]);
        outputMeter.pushLevel ((outSq[0] + outSq[1]) / (float) numOut, numSamples, juce::jmax (outPk[0], outPk[1]));
    }

    juce::AudioProcessorEditor* VoidProcessor::createEditor()
    {
        return new VoidEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::voidrev::VoidProcessor();
}
#endif
