#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace tleet::dsp
{
    // Keeps a copy of the plugin input delayed by the wet path's latency, so
    // Mix, internal bypass and host bypass blend time-aligned signals.
    // Capacity is allocated in prepare(); push/read never allocate.
    class LatencyAlignedDry
    {
    public:
        void prepare (int numChannels, int maxBlockSize, int latencySamples)
        {
            delay = juce::jmax (0, latencySamples);
            size = juce::nextPowerOfTwo (maxBlockSize + delay + 1);
            buffer.setSize (numChannels, size, false, true, false);
            dry.setSize (numChannels, maxBlockSize, false, true, false);
            writePos = 0;
        }

        void reset()
        {
            buffer.clear();
            dry.clear();
            writePos = 0;
        }

        int getLatency() const noexcept { return delay; }

        // Store the incoming block and produce the delayed dry block.
        void push (const float* const* input, int numChannels, int numSamples)
        {
            const int chans = juce::jmin (numChannels, buffer.getNumChannels());
            const int mask = size - 1;

            for (int ch = 0; ch < chans; ++ch)
            {
                auto* ring = buffer.getWritePointer (ch);
                auto* out = dry.getWritePointer (ch);
                const auto* in = input[ch];

                for (int i = 0; i < numSamples; ++i)
                {
                    const int w = (writePos + i) & mask;
                    ring[w] = in[i];
                    out[i] = ring[(w - delay) & mask];
                }
            }

            writePos = (writePos + numSamples) & mask;
        }

        const float* getDry (int channel) const noexcept
        {
            return dry.getReadPointer (juce::jmin (channel, dry.getNumChannels() - 1));
        }

    private:
        juce::AudioBuffer<float> buffer, dry;
        int size = 1, delay = 0, writePos = 0;
    };
}
