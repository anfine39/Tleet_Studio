#pragma once

#include "core/TleetProcessorBase.h"
#include "dsp/DspUtils.h"
#include "dsp/LatencyAlignedDry.h"
#include "dsp/MeterSource.h"
#include "dsp/SpectrumTap.h"
#include "dsp/TapeEngine.h"
#include "Parameters.h"
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace tleet::tape
{
    // Gain in dB moving linearly over a fixed time, then a short one-pole on the
    // linear gain to round the ramp's corners (as STEEL's SmoothGain): a plain
    // linear ramp's corners measure as clicks on a full-scale tone.
    class SmoothDbGain
    {
    public:
        void prepare (double fs, double rampSeconds, double cornerSeconds, double db) noexcept
        {
            rampSamples = std::max (1.0, rampSeconds * fs);
            corner = 1.0 - std::exp (-1.0 / std::max (1.0, cornerSeconds * fs));
            curDb = targetDb = db;
            step = 0.0;
            gain = target = std::pow (10.0, db / 20.0);
        }
        void setTargetDb (double db) noexcept
        {
            if (db != targetDb)
            {
                targetDb = db;
                step = (targetDb - curDb) / rampSamples;
            }
        }
        float next() noexcept
        {
            if (curDb != targetDb)
            {
                curDb += step;
                if ((step > 0.0 && curDb >= targetDb) || (step < 0.0 && curDb <= targetDb))
                    curDb = targetDb;
                target = std::pow (10.0, curDb / 20.0);
            }
            if (gain != target)
            {
                gain += corner * (target - gain);
                if (std::abs (gain - target) < 1.0e-7 * target)
                    gain = target;
            }
            return (float) gain;
        }

    private:
        double rampSamples = 1.0, corner = 1.0, curDb = 0.0, targetDb = 0.0, step = 0.0, gain = 1.0, target = 1.0;
    };

    // Linear value with the same ramp-and-corner smoothing (MIX).
    class SmoothLinear
    {
    public:
        void prepare (double fs, double rampSeconds, double cornerSeconds, double v) noexcept
        {
            rampSamples = std::max (1.0, rampSeconds * fs);
            corner = 1.0 - std::exp (-1.0 / std::max (1.0, cornerSeconds * fs));
            ramp = targetV = value = v;
            step = 0.0;
        }
        void setTarget (double v) noexcept
        {
            if (v != targetV)
            {
                targetV = v;
                step = (targetV - ramp) / rampSamples;
            }
        }
        float next() noexcept
        {
            if (ramp != targetV)
            {
                ramp += step;
                if ((step > 0.0 && ramp >= targetV) || (step < 0.0 && ramp <= targetV))
                    ramp = targetV;
            }
            if (value != ramp)
            {
                value += corner * (ramp - value);
                if (std::abs (value - ramp) < 1.0e-7)
                    value = ramp;
            }
            return (float) value;
        }

    private:
        double rampSamples = 1.0, corner = 1.0, ramp = 0.0, targetV = 0.0, step = 0.0, value = 0.0;
    };

    // TLEET // TAPE: multitrack tape machine, Studer A800 DNA (MODEL_SPEC.md).
    // Buses: mono -> mono or stereo -> stereo.
    //
    //   in ─► sanitise ─► x ─┬─► TapeEngine (INPUT ... crosstalk) ─► repro ─► [REPRO VU]
    //                        │                                       × AUTO GAIN ─► wet ─┐
    //                        └─► dry (delayed by the latency) ──────────────────── MIX ◄─┘
    //                                                                 × OUTPUT ─► host Bypass (dry) ─► out
    class TapeProcessor : public TleetProcessorBase
    {
    public:
        TapeProcessor();
        ~TapeProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        double getTailLengthSeconds() const override { return 0.0; }

        // ================================================================ telemetry (MODEL_SPEC 9)
        // Lock-free; the editor polls on a 30 Hz timer. Channel 0 = left, 1 = right;
        // with a mono bus both carry the single channel.
        //
        // RECORD VU: the signal after INPUT, before the record amplifier and its
        //            pre-emphasis (the machine's line input as its meter sees it).
        // REPRO VU:  the repro amplifier output: after the tape, the head losses, the repro
        //            EQ (HEAD BUMP, LF, HF), AGE, noise and crosstalk; before AUTO GAIN,
        //            MIX and OUTPUT.
        // 0 VU = the machine's operating level = a sine at -18 dBFS at that point, whatever
        // CALIBRATION and TAPE TYPE (they set how much flux 0 VU puts on the tape; the meters
        // are aligned to the operating level, as on the A800).
        // The MeterSources accumulate squares normalised to 0 VU: takeRms() == 1 is 0 VU.
        // recordVu / reproVu: the needle value = power mean of L and R; the per-channel ones
        // are independent sources. VU ballistics run in the UI (dsp::VUBallistics).
        dsp::MeterSource recordVu, reproVu;
        std::array<dsp::MeterSource, 2> recordVuChannel, reproVuChannel;

        // VU level (dB re 0 VU, floor -80) since the previous call, NaN if no audio arrived.
        float takeRecordVuDb() noexcept { return vuDbFromRms (recordVu.takeRms()); }
        float takeReproVuDb() noexcept { return vuDbFromRms (reproVu.takeRms()); }
        float takeRecordVuDb (int ch) noexcept { return vuDbFromRms (recordVuChannel[(size_t) juce::jlimit (0, 1, ch)].takeRms()); }
        float takeReproVuDb (int ch) noexcept { return vuDbFromRms (reproVuChannel[(size_t) juce::jlimit (0, 1, ch)].takeRms()); }
        static float vuDbFromRms (float rms) noexcept
        {
            return rms < 0.0f ? std::numeric_limits<float>::quiet_NaN() : juce::jmax (-80.0f, 20.0f * std::log10 (juce::jmax (rms, 1.0e-6f)));
        }

        // SATURATION bar: the deepest instantaneous knee reached by the magnetic layer since
        // the previous call (0..1, both channels): 1 - L(|Q|) / (|Q| / 3), the shortfall of
        // the tape's magnetisation against a linear medium at the effective field Q.
        // 0 = linear, 0.5 = the layer delivers half of what a linear medium would, 1 = full
        // saturation (asymptote). Bar segments lit = round(24 x activity) (the skin has 24).
        float getSaturationActivity() noexcept { return satActivity.exchange (0.0f, std::memory_order_acq_rel); }

        // TAPE CONDITION: the spectrum of the final plugin output, (L + R) / 2.
        // Inactive until the editor activates it.
        dsp::SpectrumTap outputSpectrum;

        // The machine's small-signal frequency response as it runs (dB, everything from the
        // plugin input to the REPRO point except INPUT): head losses (gap, AGE spacing, bias),
        // head bump / dip / roll-off, LF / HF, bias sensitivity, AGE L/R mismatch. Same model
        // parameters as the audio, verified against a measured sweep. channel -1 = power mean
        // of L and R. Any thread.
        double tapeResponseDb (double hz, int channel = -1) const { return engine.responseDb (hz, channel); }
        void tapeResponseDb (const double* hz, double* outDb, int count, int channel = -1) const { engine.responseDb (hz, outDb, count, channel); }

        // Reels: true while the host's playhead reports playing; without playhead
        // information, while the plugin input exceeded -60 dBFS within the last 0.5 s.
        bool isTransportRunning() const noexcept { return transportRunning.load (std::memory_order_relaxed); }
        // The tape speed in use (IPS); glides over 100 ms after a TAPE SPEED change.
        double getTapeSpeedIps() const noexcept { return tapeIps.load (std::memory_order_relaxed); }
        // AUTO GAIN in force (dB; 0 when off).
        float getAutoGainDb() const noexcept { return autoGainDb.load (std::memory_order_relaxed); }

        // ---- test hooks
        TapeEngine& getEngineForTesting() noexcept { return engine; }
        void setOversamplingOverrideForTesting (int log2) noexcept { osOverride = log2; }

    private:
        EngineSettings currentSettings() const noexcept;
        void processChunk (juce::AudioBuffer<float>& io, int start, int numSamples, int numChannels);

        struct Params
        {
            std::atomic<float>* input;
            std::atomic<float>* output;
            std::atomic<float>* tapeType;
            std::atomic<float>* speed;
            std::atomic<float>* calibration;
            std::atomic<float>* bias;
            std::atomic<float>* saturation;
            std::atomic<float>* headBump;
            std::atomic<float>* lf;
            std::atomic<float>* hf;
            std::atomic<float>* age;
            std::atomic<float>* wow;
            std::atomic<float>* flutter;
            std::atomic<float>* noise;
            std::atomic<float>* noiseAmount;
            std::atomic<float>* crosstalk;
            std::atomic<float>* mix;
            std::atomic<float>* autoGain;
            std::atomic<float>* bypass;
        } p {};

        TapeEngine engine;
        dsp::LatencyAlignedDry dry;
        std::array<std::vector<float>, 2> xBuf, wetBuf, reproBuf, outBuf;
        SmoothDbGain outputSmoothed;
        SmoothLinear mixSmoothed;
        dsp::Ramp bypassRamp;
        int preparedChannels = 0, maxBlock = 512, osOverride = -1;
        double sampleRate = 48000.0;

        std::atomic<float> satActivity { 0.0f }, autoGainDb { 0.0f };
        std::atomic<bool> transportRunning { false };
        std::atomic<double> tapeIps { 15.0 };
        int64_t samplesSinceSignal = std::numeric_limits<int32_t>::max();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeProcessor)
    };
}
