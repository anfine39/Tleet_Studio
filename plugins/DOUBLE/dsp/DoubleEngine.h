#pragma once

#include <array>
#include <vector>
#include "VoiceRing.h"
#include "WetFilter.h"
#include "Humanizer.h"
#include "VoiceTemplates.h"
#include "DoubleTelemetry.h"

// The DOUBLE wet path (MODEL_SPEC.md):
//
//   in -> mono feed -> wet HPF/LPF (+ GHOST shelf) -> delay ring
//        -> 4 voice slots, each a moving band-limited read tap:
//             delay (template x Delay) + micro-pitch (template x Detune)
//             + humanize drift (pitch, delay, gain)
//             -> GHOST diffusion (all-pass) -> gain (template, Auto Level, fade)
//        -> constant-power placement (Width, Mono Safe) -> Mono Safe side HPF -> wet out
//
// Micro-pitch is a constant-rate delay ramp (Doppler): a tap whose delay
// changes by (1 - r) samples per sample plays the input at ratio r exactly.
// The ramp is kept inside a window by splicing: a second tap is started one
// or more signal periods away, chosen by normalised cross-correlation so the
// two taps are in phase, and the pair is crossfaded with gains that keep the
// level constant for the measured correlation. No latency, no grains: a
// splice happens every 0.3-0.7 s at the largest offset (GHOST, 31 cents) and
// every 10-21 s at 1 cent and below.
//
// Realtime: everything is sized in prepare(); process() never allocates.
namespace tleet::dbl
{
    class DoubleEngine
    {
    public:
        static constexpr int maxVoices = templates::maxVoices;
        static constexpr int maxChannels = 2;
        static constexpr int controlInterval = WetFilter::controlInterval; // samples between control updates

        // Micro-pitch / splice design values (MODEL_SPEC.md section 4).
        static constexpr double spliceSpanMs = 12.0;        // window of the pitch ramp at >= 1 cent
        static constexpr double fullWindowCents = 1.0;      // below this the window shrinks with the detune
        static constexpr double spliceFadeMs = 24.0;        // splice crossfade
        static constexpr double correlationWindowMs = 8.0;  // splice match window
        static constexpr double jumpThresholdMs = 2.0;      // larger centre-delay steps splice instead of glide
        static constexpr double jumpSearchMs = 2.0;         // phase-match range of a delay jump
        static constexpr double delayGlideMs = 50.0;        // glide of small centre-delay changes
        static constexpr double detuneGlideMs = 20.0;
        static constexpr double panGlideMs = 30.0;
        static constexpr double gainGlideMs = 20.0;         // Auto Level on/off ramp
        static constexpr double voiceFadeMs = 50.0;         // voice count changes
        static constexpr double diffusionFadeMs = 40.0;     // entering / leaving GHOST diffusion
        static constexpr double maxCentreDelayMs = 60.0;
        static constexpr double guardSamples = 6.0;         // keeps a modulated tap in the sinc read range

        struct Settings
        {
            int voices = 2;          // added voices: 1, 2 or 4
            int mode = 0;            // 0 TIGHT, 1 WIDE, 2 GHOST
            float detuneCents = 8.0f;
            float delayMs = 15.0f;
            float humanize = 0.35f;  // 0..1
            float width = 1.0f;      // 0..2
            float hpfHz = 20.0f;
            float lpfHz = 20000.0f;
            bool monoSafe = true;
            bool autoLevel = true;
        };

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset() noexcept;
        void setSettings (const Settings& s) noexcept { settings = s; }

        // Renders the wet signal for `in` into `wet` (numChannels 1 or 2, may not alias `in`).
        void process (const float* const* in, float* const* wet, int numChannels, int numSamples) noexcept;

        void setTelemetry (DoubleTelemetry* t) noexcept { telemetry = t; }
        double getSampleRate() const noexcept { return fs; }

        // ---- test / inspection hooks (message or audio thread, read-only)
        void setSoloVoice (int slot) noexcept { solo = slot; } // -1 = all voices to the wet bus

        struct VoiceInfo
        {
            bool active = false;      // part of the current layout
            float fade = 0.0f;        // voice count fade, 0..1
            float pan = 0.0f;         // placement after Width / Mono Safe (0 on a mono bus)
            float delayMs = 0.0f;     // centre delay incl. humanize drift
            float tapDelayMs = 0.0f;  // instantaneous delay of the dominant read tap
            float detuneCents = 0.0f; // instantaneous pitch offset incl. drift and delay motion
            float gain = 0.0f;        // linear, relative to the dry level of a centred voice
            bool splicing = false;
            int splices = 0;          // splices since prepare
            float windowMs = 0.0f;    // micro-pitch window
        };
        VoiceInfo getVoiceInfo (int slot) const noexcept;

    private:
        struct Allpass
        {
            std::vector<float> buffer;
            int length = 1, pos = 0;
        };

        struct Voice
        {
            // read taps: delay = c + drift + u (samples); B is live only while splicing
            double uA = 0.0, uB = 0.0, cA = 0.0, cB = 0.0;
            bool splicing = false;
            int xfPos = 0;
            double rho = 1.0;

            double detune = 0.0, detuneTarget = 0.0; // cents
            double cTarget = 0.0, window = 0.0;      // samples
            double delta = 0.0;                      // delay drift (samples), last value

            // glides run in double so they land exactly on their targets (Width 0 gives L == R bit for bit)
            double pan = 0.0, panTarget = 0.0;
            double gL = 0.0, gR = 0.0, gLTarget = 0.0, gRTarget = 0.0;
            double gain = 0.0;                       // template gain x Auto Level norm (last sample)
            float fade = 0.0f;
            bool inLayout = false;
            float humanGain = 1.0f;

            Drift pitchDrift, timeDrift, levelDrift;
            std::array<Allpass, 2> diffusers;

            // last-sample motion, for the pitch telemetry
            double lastRatio = 1.0, lastDeltaStep = 0.0, lastCStep = 0.0;
            float slicePeak = 0.0f;
            int splices = 0;
        };

        void control (int nowIndex, bool allowSplices) noexcept;
        double levelNorm (int samplesAhead) const noexcept;
        void startSplice (Voice& v, int nowIndex, double ra, double rb, double newCentre) noexcept;
        double bestLag (int nowIndex, double tauA, double lagLo, double lagHi, double& rhoOut) const noexcept;
        void renderVoice (int slot, int nowStart, int n, float* outL, float* outR) noexcept;
        void publishTelemetry() noexcept;

        Settings settings;
        double fs = 48000.0;
        int channels = 2, maxBlock = 512;
        VoiceRing ring;
        WetFilter wetFilter;
        std::array<Voice, maxVoices> voices;
        std::vector<float> feed;

        // per-sample shared smoothed values of the current control block
        std::array<float, controlInterval> hsBuf {}, diffBuf {}, apgBuf {}, sideBuf {};
        std::array<double, controlInterval> normBuf {};
        std::array<double, maxVoices> templateGain {};
        // Auto Level: 1 = on, 0 = off, ramped linearly on a toggle; norm of the last sample
        double levelMix = 1.0, levelMixTarget = 1.0, normNow = 1.0;
        double hs = 0.0, hsTarget = 0.0, diff = 0.0, diffTarget = 0.0, apg = 0.0, apgTarget = 0.0,
               side = 0.0, sideTarget = 0.0;

        // mono-safe side high-pass (TPT SVF, high-pass output)
        double sideG = 0.0, sideA = 1.0, sideS1 = 0.0, sideS2 = 0.0;

        // coefficients
        double aDetune = 0, aDelay = 0, aHumanize = 0, apgStep = 0, sideStep = 0, diffStep = 0;
        double aPan = 0, levelMixStep = 0;
        float fadeStep = 0;
        int spliceLen = 1, corrWindow = 1, coarseStep = 1, refineStride = 1;
        double maxTapDelay = 0.0;
        int driftMode = -1; // mode whose humanize rate the drifts run at

        long long sampleCounter = 0;
        int sliceLength = 192, sliceCounter = 0;
        int solo = -1;
        DoubleTelemetry* telemetry = nullptr;
    };
}
