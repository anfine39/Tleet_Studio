#include "ShapeModel.h"

namespace tleet::shape
{
    namespace
    {
        double glideCoeff (double seconds, double rate, int interval) noexcept
        {
            return -std::expm1 (-(double) interval / std::max (1.0, seconds * rate));
        }

        // Exponential glide of a knob value; returns true while it moves.
        bool glideLinear (double& v, double target, double k) noexcept
        {
            const double d = target - v;
            if (d == 0.0)
                return false;
            v = std::abs (d) < 1.0e-7 ? target : v + k * d;
            return true;
        }

        // Critically damped glide: two exponential stages in series, so a
        // control starts and lands with zero slope (no kink in the envelope).
        bool glide2 (double& mid, double& v, double target, double k) noexcept
        {
            const bool a = glideLinear (mid, target, k);
            const bool b = glideLinear (v, mid, k);
            return a || b;
        }

        // Exponential glide in the log domain (capacitances, frequencies).
        bool glideLog (double& v, double target, double k) noexcept
        {
            if (v == target)
                return false;
            const double r = std::log (target / v);
            v = std::abs (r) < 1.0e-9 ? target : v * std::exp (k * r);
            return true;
        }

        bool glideLog2 (double& mid, double& v, double target, double k) noexcept
        {
            const bool a = glideLog (mid, target, k);
            const bool b = glideLog (v, mid, k);
            return a || b;
        }
    }

    void ShapeModel::prepare (double hostRate, int oversamplingFactor)
    {
        fsHost = hostRate;
        fs = hostRate * std::max (1, oversamplingFactor);
        stageCoeffs.prepare (fs);
        kKnob = glideCoeff (0.5 * knobGlideS, fs, controlInterval);         // two stages of half the time
        kSelector = glideCoeff (0.5 * selectorGlideS, fs, controlInterval);
        kVoice = glideCoeff (voicingGlideS, fs, controlInterval);
        kDrive = glideCoeff (0.5 * driveGlideS, fs, 1);
        eqStep = 1.0 / std::max (1.0, eqFadeS * fs);
        kRound = glideCoeff (cornerS, fs, 1);
        reset();
    }

    void ShapeModel::reset()
    {
        for (auto& c : eq) c.reset();
        for (auto& t : inXfmr) t.reset();
        for (auto& t : outXfmr) t.reset();
        for (auto& a : amp) a.reset();
        countdown = 0;
        controlUpdate (true);
    }

    void ShapeModel::controlUpdate (bool snapAll) noexcept
    {
        const Voicing& tv = stages::voicing (characterIndex);
        bool moved = false;
        if (snapAll)
        {
            current = mid = target;
            voice = tv;
            moved = true;
        }
        else
        {
            moved |= glideLog2 (mid.lfBoostCap, current.lfBoostCap, target.lfBoostCap, kSelector);
            moved |= glide2 (mid.lfBoost, current.lfBoost, target.lfBoost, kKnob);
            moved |= glide2 (mid.lfAtten, current.lfAtten, target.lfAtten, kKnob);
            moved |= glideLog2 (mid.hfHz, current.hfHz, target.hfHz, kSelector);
            moved |= glide2 (mid.hfBoost, current.hfBoost, target.hfBoost, kKnob);
            moved |= glide2 (mid.bandwidth, current.bandwidth, target.bandwidth, kKnob);
            moved |= glideLog2 (mid.attenCap, current.attenCap, target.attenCap, kSelector);
            moved |= glideLog2 (mid.attenHz, current.attenHz, target.attenHz, kSelector);
            moved |= glide2 (mid.hfAtten, current.hfAtten, target.hfAtten, kKnob);

            glideLinear (voice.driveDb, tv.driveDb, kVoice);
            voice.levelDb = tv.levelDb; // glided per sample below
            glideLog (voice.ampVc, tv.ampVc, kVoice);
            glideLinear (voice.ampEven, tv.ampEven, kVoice);
            glideLinear (voice.xfmrIn, tv.xfmrIn, kVoice);
            glideLinear (voice.xfmrOut, tv.xfmrOut, kVoice);
        }

        if (moved || ! settled)
        {
            elements = net::elementsFor (current, fs);
            coeffsNext.set (elements);
            if (snapAll)
            {
                coeffs = coeffsNext;
                rampLeft = 0;
            }
            else
            {
                for (int k = 0; k < net::Coeffs::count; ++k)
                    coeffsDelta.v[k] = (coeffsNext.v[k] - coeffs.v[k]) * (1.0 / controlInterval);
                rampLeft = controlInterval;
            }
        }
        settled = ! moved;

        driveGainTarget = stages::driveGain (driveTarget) * std::pow (10.0, voice.driveDb / 20.0);
        levelTarget = std::pow (10.0, voice.levelDb / 20.0);
        if (snapAll)
        {
            level = levelMid = levelTarget;
            driveGain = driveMid = driveGainTarget;
            invDrive = 1.0 / driveGain;
            eqFade = eqFadeLin = eqInTarget;
        }
    }

    void ShapeModel::process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int nch = std::min (numChannels, maxChannels);
        for (int i = 0; i < numSamples; ++i)
        {
            if (--countdown <= 0)
            {
                countdown = controlInterval;
                controlUpdate (false);
            }

            // EQ IN: linear crossfade with rounded corners, snapped at 0 and 1.
            eqFadeLin = eqInTarget > 0.5 ? std::min (1.0, eqFadeLin + eqStep) : std::max (0.0, eqFadeLin - eqStep);
            const double fd = eqFadeLin - eqFade;
            eqFade = std::abs (fd) < 1.0e-7 ? eqFadeLin : eqFade + kRound * fd;

            if (rampLeft > 0)
            {
                if (--rampLeft == 0)
                    coeffs = coeffsNext; // land exactly on the control point
                else
                    for (int k = 0; k < net::Coeffs::count; ++k)
                        coeffs.v[k] += coeffsDelta.v[k];
            }

            if (driveGain != driveGainTarget)
            {
                // two one-poles in series (critically damped, no kink at the start)
                const double d1 = driveGainTarget - driveMid;
                driveMid = std::abs (d1) < 1.0e-9 * driveGainTarget ? driveGainTarget : driveMid + kDrive * d1;
                const double d2 = driveMid - driveGain;
                driveGain = std::abs (d2) < 1.0e-9 * driveGainTarget && driveMid == driveGainTarget ? driveGainTarget : driveGain + kDrive * d2;
                invDrive = 1.0 / driveGain;
            }

            if (level != levelTarget)
            {
                // the CHARACTER level step, glided like DRIVE
                const double d1 = levelTarget - levelMid;
                levelMid = std::abs (d1) < 1.0e-9 ? levelTarget : levelMid + kDrive * d1;
                const double d2 = levelMid - level;
                level = std::abs (d2) < 1.0e-9 && levelMid == levelTarget ? levelTarget : level + kDrive * d2;
            }

            for (int ch = 0; ch < nch; ++ch)
            {
                const double x = io[ch][i];
                const double t1 = stagesOn ? inXfmr[(size_t) ch].process (x * driveGain, voice.xfmrIn, stages::inFluxSat, stageCoeffs) : x;
                const double n = eq[(size_t) ch].process (t1, coeffs);
                const double y = eqFade >= 1.0 ? n : (eqFade <= 0.0 ? t1 : t1 + eqFade * (n - t1));
                double out = y;
                if (stagesOn)
                {
                    const double a = amp[(size_t) ch].process (y, voice.ampVc, voice.ampEven, stageCoeffs);
                    out = outXfmr[(size_t) ch].process (a, voice.xfmrOut, stages::outFluxSat, stageCoeffs) * (invDrive * level);
                }
                io[ch][i] = (float) out;
            }
        }
    }
}
