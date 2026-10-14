#pragma once

#include "AnalogStages.h"
#include "PassiveNetwork.h"
#include <array>

namespace tleet::shape
{
    // Passive program EQ model (Pultec EQP-1A DNA). See plugins/SHAPE/RESEARCH.md
    // for the evidence and MODEL_SPEC.md for the provenance of every block.
    //
    // Runs entirely at the internal rate (host x 4 / 2 / 1, >= 176.4 kHz):
    //   x ─► × D (DRIVE, CHARACTER) ─► input transformer ─► passive network (EQ IN: crossfade
    //     with the network bypassed) ─► make-up amplifier ─► output transformer ─► × 1/D
    // With every boost/atten at 0 the network is flat (its loss is made up exactly).
    class ShapeModel
    {
    public:
        static constexpr int maxChannels = 2;
        static constexpr int controlInterval = 16;    // internal-rate samples between control updates
        static constexpr double knobGlideS = 0.02;    // boost / atten / bandwidth knobs (critically damped, 2 x 10 ms)
        static constexpr double selectorGlideS = 0.04; // frequency selectors (capacitor values, log domain, 2 x 20 ms)
        static constexpr double voicingGlideS = 0.02; // CHARACTER voicing constants
        static constexpr double driveGlideS = 0.02;   // DRIVE (per sample, critically damped, 2 x 10 ms)
        static constexpr double eqFadeS = 0.02;       // EQ IN crossfade (linear, rounded corners)
        static constexpr double cornerS = 0.002;

        void prepare (double hostRate, int oversamplingFactor);
        void reset();

        // ---- controls (call per block)
        void setEq (const EqSettings& s) noexcept { target = net::physicalFor (s); eqInTarget = s.eqIn ? 1.0 : 0.0; }
        void setDrive (float v01) noexcept { driveTarget = std::clamp ((double) v01, 0.0, 1.0); }
        void setCharacter (int index) noexcept { characterIndex = std::clamp (index, 0, stages::numCharacters - 1); }

        // ---- processing (internal rate, in place)
        void process (float* const* io, int numChannels, int numSamples) noexcept;

        // ---- state for tests and editors
        double getHostRate() const noexcept { return fsHost; }
        double getInternalRate() const noexcept { return fs; }
        const net::Physical& getCurrentPhysical() const noexcept { return current; }
        const net::Elements& getCurrentElements() const noexcept { return elements; }
        double getEqFade() const noexcept { return eqFade; }
        bool isSettled() const noexcept { return settled; }

        // Test hooks (mutation checks / isolated stages): bypass the analog stages.
        void setStagesEnabledForTesting (bool on) noexcept { stagesOn = on; }

    private:
        void controlUpdate (bool snapAll) noexcept;

        double fsHost = 48000.0, fs = 192000.0;
        net::Physical target, mid, current; // mid: first stage of the critically damped glides
        net::Elements elements {};
        net::Coeffs coeffs, coeffsNext, coeffsDelta; // per-sample coefficients, glided linearly to the next control point
        int rampLeft = 0;
        StageCoeffs stageCoeffs;
        Voicing voice = stages::voicing (1);
        int characterIndex = 1;
        double driveTarget = 0.25, driveGain = 1.0, driveMid = 1.0, driveGainTarget = 1.0, invDrive = 1.0;
        double level = 1.0, levelMid = 1.0, levelTarget = 1.0; // the voicing's output level (linear)
        double eqInTarget = 1.0, eqFade = 1.0, eqFadeLin = 1.0, eqStep = 0.001, kRound = 0.01;
        double kKnob = 0.1, kSelector = 0.05, kVoice = 0.1, kDrive = 0.001;
        int countdown = 0;
        bool settled = false, stagesOn = true;

        std::array<net::Channel, maxChannels> eq {};
        std::array<Transformer, maxChannels> inXfmr {}, outXfmr {};
        std::array<Amplifier, maxChannels> amp {};
    };
}
