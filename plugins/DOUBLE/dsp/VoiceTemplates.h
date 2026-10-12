#pragma once

#include <array>

// Voice and mode templates of DOUBLE (MODEL_SPEC.md section 3). The knobs set
// the reference values; a mode scales them and every voice slot takes its
// own share, so offsets are always derived from Detune / Delay / Width and
// never locked to fixed numbers. All values are [TLEET_EXTENSION] design
// choices (DOUBLE is an original processor, there is no hardware reference).
namespace tleet::dbl::templates
{
    inline constexpr int maxVoices = 4;

    // Slot layout. VOICES 1 uses slot 0; VOICES 2 slots 0-1; VOICES 4 slots 0-3.
    // A slot keeps the same pitch and time share in every layout, so changing
    // the voice count only fades slots in or out (plus a pan glide for slot 0
    // between 1 and 2 voices); it never retunes or re-times a running voice.
    struct Slot
    {
        float detune;     // share of the mode-scaled Detune (sign: + = sharp for positive Detune)
        float delay;      // share of the mode-scaled Delay
        float pan;        // placement at Width 100 % in WIDE, before the mode spread (-1 left ... +1 right)
        float gainDb;     // voice level before Auto Level
        float diffuseMs[2]; // GHOST diffusion all-pass lengths (different per slot = decorrelated voices)
    };

    // Outer pair: equal and opposite detune (the "spread"), left sharp for a
    // positive Detune; delays 1.00 / 0.78 so the pair is symmetric in place but
    // not identical in time (identical delays would fuse into a phantom centre).
    // Inner pair: smaller detune and earlier, unequal delays, closer to centre,
    // 1 dB lower so the outer pair defines the width.
    inline constexpr std::array<Slot, maxVoices> slots { {
        { +1.00f, 1.00f, -1.00f,  0.0f, { 4.3f, 7.1f } },   // 0: outer left
        { -1.00f, 0.78f, +1.00f,  0.0f, { 5.1f, 6.3f } },   // 1: outer right
        { +0.45f, 0.70f, -0.45f, -1.0f, { 3.7f, 8.3f } },   // 2: inner left
        { -0.55f, 0.86f, +0.40f, -1.0f, { 4.7f, 6.9f } },   // 3: inner right
    } };

    // VOICES 1: the single voice is slot 0 at half its two-voice placement, so
    // Width still places it and Width 0 stacks it in the centre.
    inline constexpr float singleVoicePanShare = 0.5f;

    // Pan of a slot with |pan| = 1 at Width 100 % in WIDE. Width 200 % doubles
    // it (clamped at hard pan), Width 0 % centres every voice.
    inline constexpr float panAtWidth100 = 0.7f;

    struct Mode
    {
        float detune;        // x Detune knob
        float delay;         // x Delay knob
        float spread;        // x placement
        float humanizeDepth; // x humanize depths
        float humanizeRate;  // x humanize speed (1 = base timing)
        float shelfDb;       // high shelf on the wet feed (darker voices)
        float diffusion;     // all-pass coefficient of the voice diffusion (0 = off)
    };

    // TIGHT: small delay / detune / spread, a natural double.
    // WIDE:  the knob values as set, wider placement and full detune.
    // GHOST: darker (high shelf), diffused (all-pass smeared, decorrelated),
    //        later, more detuned and more unstable.
    inline constexpr std::array<Mode, 3> modes { {
        { 0.60f, 0.75f, 0.55f, 0.60f, 1.00f,  0.0f, 0.00f },  // TIGHT
        { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,  0.0f, 0.00f },  // WIDE
        { 1.25f, 1.40f, 0.85f, 1.70f, 1.50f, -8.0f, 0.55f },  // GHOST
    } };

    inline constexpr float ghostShelfHz = 2500.0f;

    // Humanize depths at 100 % in WIDE (scaled by Mode::humanizeDepth).
    inline constexpr float humanizePitchCents = 5.0f;  // slow pitch drift, +-
    inline constexpr float humanizeDelayMs = 1.2f;     // slow timing drift, +-
    inline constexpr float humanizeGainDb = 0.5f;      // very small level drift, +-

    // Mono Safe (stereo only): placement narrowed, all-pass diffusion
    // reduced, wet side signal high-passed (MODEL_SPEC.md section 6).
    inline constexpr float monoSafePanShare = 0.5f;
    inline constexpr float monoSafeDiffusionShare = 0.5f;
    inline constexpr float monoSafeSideHpfHz = 200.0f;

    inline int clampMode (int m) noexcept { return m < 0 ? 0 : (m > 2 ? 2 : m); }

    // Plain-value laws, shared by the engine and the tests.
    inline float voiceDetuneCents (float detuneKnob, int mode, int slot) noexcept
    {
        return detuneKnob * modes[(size_t) clampMode (mode)].detune * slots[(size_t) slot].detune;
    }

    // Requested centre delay before the micro-pitch floor (see DoubleEngine).
    inline float voiceDelayMs (float delayKnob, int mode, int slot) noexcept
    {
        return delayKnob * modes[(size_t) clampMode (mode)].delay * slots[(size_t) slot].delay;
    }

    // Placement after Width (0..2) and Mono Safe, before the pan glide.
    inline float voicePan (float width, int mode, int slot, int voiceCount, bool monoSafe) noexcept
    {
        float p = width * panAtWidth100 * modes[(size_t) clampMode (mode)].spread * slots[(size_t) slot].pan;
        if (voiceCount == 1)
            p *= singleVoicePanShare;
        if (monoSafe)
            p *= monoSafePanShare;
        return p < -1.0f ? -1.0f : (p > 1.0f ? 1.0f : p);
    }
}
