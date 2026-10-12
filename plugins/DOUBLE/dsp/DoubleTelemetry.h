#pragma once

#include <array>
#include <atomic>

namespace tleet::dbl
{
    // Hand-off from the audio thread to the voice display. Everything the
    // display shows comes from here: which voices really run, where they sit
    // in the stereo field, their current delay and pitch offset, and the
    // envelope of each voice's own output. Lock-free; the audio thread never
    // waits and never allocates.
    class DoubleTelemetry
    {
    public:
        static constexpr int maxVoices = 4;
        static constexpr int historySize = 256;     // envelope slices per voice
        static constexpr double sliceSeconds = 0.004; // one slice = peak over 4 ms

        struct Voice
        {
            bool active = false;
            float pan = 0.0f;          // -1 = hard left ... +1 = hard right, after Width and Mono Safe
            float delayMs = 0.0f;      // current delay incl. humanize drift
            float detuneCents = 0.0f;  // current pitch offset incl. humanize drift
            float gain = 0.0f;         // linear voice gain incl. Auto Level
        };

        // ---- audio thread
        void publishVoice (int index, const Voice& v) noexcept
        {
            auto& s = voices[(size_t) index];
            s.active.store (v.active, std::memory_order_relaxed);
            s.pan.store (v.pan, std::memory_order_relaxed);
            s.delayMs.store (v.delayMs, std::memory_order_relaxed);
            s.detuneCents.store (v.detuneCents, std::memory_order_relaxed);
            s.gain.store (v.gain, std::memory_order_relaxed);
        }

        // Peak of |voice output| over one slice, for every voice (inactive voices push 0).
        void pushSlice (const float* peaks) noexcept
        {
            const int w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < maxVoices; ++i)
                history[(size_t) i][(size_t) w].store (peaks[i], std::memory_order_relaxed);
            writePos.store ((w + 1) % historySize, std::memory_order_release);
            slices.fetch_add (1, std::memory_order_release);
        }

        // ---- UI thread
        Voice voice (int index) const noexcept
        {
            const auto& s = voices[(size_t) index];
            return { s.active.load (std::memory_order_relaxed), s.pan.load (std::memory_order_relaxed),
                     s.delayMs.load (std::memory_order_relaxed), s.detuneCents.load (std::memory_order_relaxed),
                     s.gain.load (std::memory_order_relaxed) };
        }

        // Copies the newest `n` slices of one voice, oldest first.
        void readHistory (int index, float* dest, int n) const noexcept
        {
            const int w = writePos.load (std::memory_order_acquire);
            for (int k = 0; k < n; ++k)
                dest[k] = history[(size_t) index][(size_t) ((w - n + k + 2 * historySize) % historySize)].load (std::memory_order_relaxed);
        }

        // Total slices pushed so far (lets the UI tell "no audio" from silence).
        long long sliceCount() const noexcept { return slices.load (std::memory_order_acquire); }

    private:
        struct AtomicVoice
        {
            std::atomic<bool> active { false };
            std::atomic<float> pan { 0.0f }, delayMs { 0.0f }, detuneCents { 0.0f }, gain { 0.0f };
        };
        std::array<AtomicVoice, maxVoices> voices;
        std::array<std::array<std::atomic<float>, historySize>, maxVoices> history {};
        std::atomic<int> writePos { 0 };
        std::atomic<long long> slices { 0 };
    };
}
