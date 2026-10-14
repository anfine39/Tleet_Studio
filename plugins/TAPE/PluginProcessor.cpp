#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TAPEPresets.h"

namespace tleet::tape
{
    namespace
    {
        constexpr double gainRampSeconds = 0.03;    // OUTPUT, MIX
        constexpr double gainCornerSeconds = 0.003; // rounds the corners of the ramps
        constexpr double bypassSeconds = 0.02;      // host Bypass crossfade
        constexpr double signalHoldSeconds = 0.5;   // transport fallback: audio within the last 0.5 s
        const float signalThreshold = std::pow (10.0f, -60.0f / 20.0f);
        constexpr double satDisplayRangeDb = 36.0;  // SATURATION bar: knee depth -36 dB .. 0 dB
    }

    TapeProcessor::TapeProcessor()
        : TleetProcessorBase (BusesProperties()
                                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                              params::createLayout(), "TAPE")
    {
        auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        p.input       = raw (params::input);
        p.output      = raw (params::output);
        p.tapeType    = raw (params::tapeType);
        p.speed       = raw (params::speed);
        p.calibration = raw (params::calibration);
        p.bias        = raw (params::bias);
        p.saturation  = raw (params::saturation);
        p.headBump    = raw (params::headBump);
        p.lf          = raw (params::lf);
        p.hf          = raw (params::hf);
        p.age         = raw (params::age);
        p.wow         = raw (params::wow);
        p.flutter     = raw (params::flutter);
        p.noise       = raw (params::noise);
        p.noiseAmount = raw (params::noiseAmount);
        p.crosstalk   = raw (params::crosstalk);
        p.mix         = raw (params::mix);
        p.autoGain    = raw (params::autoGain);
        p.bypass      = raw (params::bypass);

        juce::StringArray factory;
        for (int i = 0; i < TAPEPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = TAPEPresets::getNamedResource (TAPEPresets::namedResourceList[i], size))
                factory.add (juce::String::fromUTF8 (data, size));
        }
        addFactoryPresets (factory);
    }

    TapeProcessor::~TapeProcessor() = default;

    bool TapeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    EngineSettings TapeProcessor::currentSettings() const noexcept
    {
        EngineSettings s;
        s.inputDb = p.input->load();
        s.type = juce::jlimit (0, 2, juce::roundToInt (p.tapeType->load()));
        s.speed = juce::jlimit (0, 2, juce::roundToInt (p.speed->load()));
        s.calibration = juce::jlimit (0, 3, juce::roundToInt (p.calibration->load()));
        s.bias = p.bias->load();
        s.saturation = p.saturation->load();
        s.headBump = p.headBump->load();
        s.lf = p.lf->load();
        s.hf = p.hf->load();
        s.age = p.age->load();
        s.wow = p.wow->load();
        s.flutter = p.flutter->load();
        s.noise = p.noise->load() > 0.5f;
        s.noiseAmount = p.noiseAmount->load();
        s.crosstalk = p.crosstalk->load();
        s.autoGain = p.autoGain->load() > 0.5f;
        return s;
    }

    void TapeProcessor::prepareToPlay (double fs, int samplesPerBlock)
    {
        sampleRate = fs;
        preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
        maxBlock = juce::jmax (1, samplesPerBlock);

        engine.prepare (fs, maxBlock, preparedChannels, currentSettings(), osOverride);
        setLatencySamples (engine.latency());
        dry.prepare (preparedChannels, maxBlock, engine.latency());
        for (auto* v : { &xBuf, &wetBuf, &reproBuf, &outBuf })
            for (auto& b : *v)
                b.assign ((size_t) maxBlock, 0.0f);

        outputSmoothed.prepare (fs, gainRampSeconds, gainCornerSeconds, p.output->load());
        mixSmoothed.prepare (fs, gainRampSeconds, gainCornerSeconds, p.mix->load() * 0.01);
        bypassRamp.prepare (fs, bypassSeconds);
        bypassRamp.snap (p.bypass->load() > 0.5f ? 1.0f : 0.0f);
        outputSpectrum.setSampleRate (fs);
        satActivity = 0.0f;
        tapeIps = engine.currentIps();
        autoGainDb = (float) engine.currentAutoGainDb();
        samplesSinceSignal = std::numeric_limits<int32_t>::max();
        transportRunning = false;
    }

    void TapeProcessor::releaseResources() {}

    void TapeProcessor::reset()
    {
        engine.reset();
        dry.reset();
    }

    void TapeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const int total = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, total);
        if (total == 0 || numChannels == 0 || preparedChannels == 0)
            return;

        engine.setTargets (currentSettings());
        outputSmoothed.setTargetDb (p.output->load());
        mixSmoothed.setTarget (p.mix->load() * 0.01);
        bypassRamp.setTarget (p.bypass->load() > 0.5f ? 1.0f : 0.0f);

        // reels: the host's transport; without one, recent signal on the input
        bool hostKnows = false, hostPlaying = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                hostKnows = true;
                hostPlaying = pos->getIsPlaying();
            }

        for (int start = 0; start < total; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, total - start), numChannels);

        transportRunning.store (hostKnows ? hostPlaying : samplesSinceSignal < (int64_t) (signalHoldSeconds * sampleRate),
                                std::memory_order_relaxed);
        tapeIps.store (engine.currentIps(), std::memory_order_relaxed);
        autoGainDb.store ((float) engine.currentAutoGainDb(), std::memory_order_relaxed);
    }

    void TapeProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, int numChannels)
    {
        float* io[2] {};
        const float* x[2] {};
        float* wet[2] {};
        float* rep[2] {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            io[ch] = buffer.getWritePointer (ch, start);
            auto* xb = xBuf[(size_t) ch].data();
            for (int i = 0; i < n; ++i)
                xb[i] = dsp::sanitize (io[ch][i]);
            x[ch] = xb;
            wet[ch] = wetBuf[(size_t) ch].data();
            rep[ch] = reproBuf[(size_t) ch].data();
        }

        // transport fallback detector (plugin input above -60 dBFS)
        {
            int last = -1;
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = n - 1; i > last; --i)
                    if (std::abs (x[ch][i]) > signalThreshold)
                    {
                        last = i;
                        break;
                    }
            samplesSinceSignal = last >= 0 ? (int64_t) (n - 1 - last) : std::min<int64_t> (samplesSinceSignal + n, std::numeric_limits<int32_t>::max());
        }

        engine.process (x, wet, rep, n);
        dry.push (x, numChannels, n);

        // ---- MIX (linear: wet and dry are correlated and level-matched by AUTO GAIN),
        //      OUTPUT, host Bypass (bit-exact delayed dry once the crossfade is done)
        float outSq[2] {}, outPk[2] {};
        for (int i = 0; i < n; ++i)
        {
            const float m = mixSmoothed.next();
            const float og = outputSmoothed.next();
            const float byp = bypassRamp.next();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float d = dry.getDry (ch)[i];
                float v = m >= 1.0f ? wet[ch][i] : (m <= 0.0f ? d : d + m * (wet[ch][i] - d));
                v *= og;
                if (byp >= 1.0f)
                    v = d;
                else if (byp > 0.0f)
                    v += (d - v) * byp;
                io[ch][i] = v;
                outBuf[(size_t) ch][(size_t) i] = v;
                outSq[ch] += v * v;
                outPk[ch] = juce::jmax (outPk[ch], std::abs (v));
            }
        }

        // ---- meters
        float inSq = 0.0f, inPk = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                inSq += x[ch][i] * x[ch][i];
                inPk = juce::jmax (inPk, std::abs (x[ch][i]));
            }
        inputMeter.pushLevel (inSq / (float) numChannels, n, inPk);
        outputMeter.pushLevel ((outSq[0] + outSq[1]) / (float) numChannels, n, juce::jmax (outPk[0], outPk[1]));

        const float k = (float) (1.0 / model::zeroVuPeak); // peaks in VU units (1 = 0 VU sine peak)
        const int c1 = numChannels > 1 ? 1 : 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const int src = ch == 0 ? 0 : c1;
            recordVuChannel[(size_t) ch].pushLevel ((float) engine.recordSquares (src), n, (float) engine.recordPeak (src) * k);
            reproVuChannel[(size_t) ch].pushLevel ((float) engine.reproSquares (src), n, (float) engine.reproPeak (src) * k);
        }
        recordVu.pushLevel ((float) (0.5 * (engine.recordSquares (0) + engine.recordSquares (c1))), n,
                            (float) std::max (engine.recordPeak (0), engine.recordPeak (c1)) * k);
        reproVu.pushLevel ((float) (0.5 * (engine.reproSquares (0) + engine.reproSquares (c1))), n,
                           (float) std::max (engine.reproPeak (0), engine.reproPeak (c1)) * k);

        {
            const double knee = engine.saturationActivity();
            const float a = knee > 0.0 ? (float) juce::jlimit (0.0, 1.0, 1.0 + 20.0 * std::log10 (knee) / satDisplayRangeDb) : 0.0f;
            float cur = satActivity.load (std::memory_order_relaxed);
            while (a > cur && ! satActivity.compare_exchange_weak (cur, a, std::memory_order_relaxed)) {}
        }

        outputSpectrum.push (outBuf[0].data(), numChannels > 1 ? outBuf[1].data() : nullptr, n);
    }

    juce::AudioProcessorEditor* TapeProcessor::createEditor()
    {
        return new TapeEditor (*this);
    }
}

#if ! TLEET_TESTING // the test runner links several plugins into one binary
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tleet::tape::TapeProcessor();
}
#endif
