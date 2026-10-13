#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Time system of ECHO (MODEL_SPEC.md section 4): the head-1 delay in FREE and
// SYNC, the note-value formula of the master spec (13.8), the buffer limits and
// the TAP tempo estimator. Pure functions, used by the processor and the tests.
namespace tleet::echo::timing
{
    // Head positions as multiples of the head-1 tape distance. From the
    // licensed Roland/UA RE-201 model's published head delay ranges
    // (RESEARCH S2: 69-177 / 131-337 / 189-489 ms): 1.899-1.904 and
    // 2.739-2.763 at the two ends of the speed range. [SOURCE_CONFIRMED ranges,
    // ratios rounded to two decimals]
    inline constexpr std::array<double, 3> headRatio { 1.0, 1.90, 2.75 };
    inline constexpr int numHeads = 3;

    // The hardware's head-1 range (fastest / slowest tape speed), S2.
    inline constexpr double hardwareHead1MinMs = 69.0, hardwareHead1MaxMs = 177.0;

    // Head-1 delay limits of the buffers (MODEL_SPEC 4.3). 6 s covers a whole
    // note down to 40 BPM; head 3 then sits 16.5 s back on the tape.
    inline constexpr double minHead1Ms = 10.0, maxHead1Ms = 6000.0;

    // Note values relative to a quarter note; FEEL factors (master spec 13.8).
    inline constexpr std::array<double, 6> divisionQuarters { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    inline constexpr std::array<double, 3> feelFactor { 1.0, 1.5, 2.0 / 3.0 };

    // quarter_ms = 60000 / BPM; dotted = straight * 1.5; triplet = straight * 2/3.
    inline double noteMs (double bpm, int division, int feel) noexcept
    {
        division = std::clamp (division, 0, (int) divisionQuarters.size() - 1);
        feel = std::clamp (feel, 0, (int) feelFactor.size() - 1);
        return 60000.0 / std::max (1.0, bpm) * divisionQuarters[(size_t) division] * feelFactor[(size_t) feel];
    }

    struct Head1Time
    {
        double ms = 200.0;     // head-1 delay the tape speed is driven to
        bool clamped = false;  // the requested time was outside [minHead1Ms, maxHead1Ms]
    };

    // FREE: head 1 = REPEAT RATE. SYNC: head 1 = the note value at `bpm`.
    inline Head1Time head1Time (bool sync, double repeatMs, double bpm, int division, int feel) noexcept
    {
        const double want = sync ? noteMs (bpm, division, feel) : repeatMs;
        Head1Time t;
        t.ms = std::clamp (want, minHead1Ms, maxHead1Ms);
        t.clamped = want < minHead1Ms || want > maxHead1Ms;
        return t;
    }

    // TAP: two or more taps less than `timeoutS` apart form a sequence; the
    // result is the mean of the last (up to) `maxIntervals` intervals. A tap
    // after a longer pause (or an earlier time stamp) starts a new sequence.
    // Message thread only.
    class TapTempo
    {
    public:
        static constexpr double timeoutS = 2.0;
        static constexpr int maxIntervals = 3;

        // Returns the averaged interval in seconds, or 0 when this tap only
        // started a sequence.
        double tap (double t) noexcept
        {
            if (count == 0 || t <= times[(size_t) (count - 1)] || t - times[(size_t) (count - 1)] > timeoutS)
            {
                times[0] = t;
                count = 1;
                return 0.0;
            }
            if (count == (int) times.size())
            {
                std::rotate (times.begin(), times.begin() + 1, times.end());
                --count;
            }
            times[(size_t) count++] = t;
            return (times[(size_t) (count - 1)] - times[0]) / (double) (count - 1);
        }

        void reset() noexcept { count = 0; }
        int getCount() const noexcept { return count; }

    private:
        std::array<double, maxIntervals + 1> times {};
        int count = 0;
    };
}
