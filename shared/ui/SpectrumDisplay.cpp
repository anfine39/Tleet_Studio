#include "SpectrumDisplay.h"
#include "TleetLookAndFeel.h"
#include <cmath>

namespace tleet::ui
{
    namespace
    {
        constexpr float minusInf = -1000.0f;

        juce::String frequencyLabel (float hz)
        {
            if (hz >= 1000.0f)
                return juce::String (hz / 1000.0f, hz >= 10000.0f || std::fmod (hz, 1000.0f) == 0.0f ? 0 : 1) + "K";
            return juce::String (juce::roundToInt (hz));
        }
    }

    SpectrumDisplay::SpectrumDisplay (dsp::SpectrumTap& t, Style s) : tap (t), style (std::move (s))
    {
        setInterceptsMouseClicks (false, false);
        configure();
    }

    SpectrumDisplay::~SpectrumDisplay()
    {
        tap.setActive (false);
    }

    juce::String SpectrumDisplay::qualityName (Quality q)
    {
        switch (q)
        {
            case Quality::off:    return "Off";
            case Quality::eco:    return "Eco";
            case Quality::normal: return "Normal";
            case Quality::high:   return "High";
        }
        return {};
    }

    void SpectrumDisplay::setQuality (Quality q)
    {
        if (q == quality)
            return;
        quality = q;
        configure();
        repaint();
    }

    void SpectrumDisplay::configure()
    {
        // visualisation workload only: FFT size, overlap, refresh, averaging
        int hz = 30;
        switch (quality)
        {
            case Quality::off:    fftOrder = 11; hop = 1 << 10; hz = 0;  release = 0.7f;  break;
            case Quality::eco:    fftOrder = 11; hop = 1 << 10; hz = 15; release = 0.7f;  break;
            case Quality::normal: fftOrder = 12; hop = 1 << 10; hz = 30; release = 0.75f; break;
            case Quality::high:   fftOrder = 13; hop = 1 << 10; hz = 60; release = 0.8f;  break;
        }
        fftSize = 1 << fftOrder;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        window.assign ((size_t) fftSize, 0.0f);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
                                                                   juce::dsp::WindowingFunction<float>::hann, false);
        double sumSq = 0.0;
        for (auto w : window)
            sumSq += (double) w * w;
        windowGain = (float) sumSq;
        ring.assign ((size_t) fftSize, 0.0f);
        frame.assign ((size_t) fftSize * 2, 0.0f);
        power.assign ((size_t) fftSize / 2 + 1, 0.0f);
        scratch.assign ((size_t) dsp::SpectrumTap::capacity / 2, 0.0f);
        ringFill = 0;
        sinceLast = 0;
        std::fill (displayed.begin(), displayed.end(), minusInf);
        std::fill (last.begin(), last.end(), minusInf);
        stopTimer();
        if (hz > 0)
            startTimerHz (hz);
        updateTap();
    }

    void SpectrumDisplay::updateTap()
    {
        tap.setActive (quality != Quality::off && isVisible() && getParentComponent() != nullptr);
    }

    void SpectrumDisplay::resized()
    {
        rebuildColumns();
    }

    void SpectrumDisplay::rebuildColumns()
    {
        const int n = juce::jmax (2, (int) std::ceil (style.screen.getWidth() - 2.0f * style.inset) + 1);
        columnHz.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            columnHz[(size_t) i] = frequencyForX (style.screen.getX() + style.inset + (float) i);
        displayed.assign ((size_t) n, minusInf);
        last.assign ((size_t) n, minusInf);
    }

    float SpectrumDisplay::xForFrequency (float hz) const noexcept
    {
        const float x0 = style.screen.getX() + style.inset, x1 = style.screen.getRight() - style.inset;
        const float t = std::log (hz / style.minHz) / std::log (style.maxHz / style.minHz);
        return x0 + t * (x1 - x0);
    }

    float SpectrumDisplay::frequencyForX (float x) const noexcept
    {
        const float x0 = style.screen.getX() + style.inset, x1 = style.screen.getRight() - style.inset;
        const float t = (x - x0) / juce::jmax (1.0f, x1 - x0);
        return style.minHz * std::pow (style.maxHz / style.minHz, t);
    }

    float SpectrumDisplay::yForDb (float db) const noexcept
    {
        const float t = (style.topDb - db) / (style.topDb - style.bottomDb);
        return style.screen.getY() + 2.0f + juce::jlimit (0.0f, 1.0f, t) * (style.screen.getHeight() - 4.0f);
    }

    void SpectrumDisplay::processPending()
    {
        if (quality == Quality::off)
            return;
        sampleRate = tap.getSampleRate();
        const int n = tap.pull (scratch.data(), (int) scratch.size());
        int framesThisTick = 0;
        for (int i = 0; i < n; ++i)
        {
            // ring holds the newest fftSize samples, oldest first at ringFill
            ring[(size_t) ringFill] = scratch[(size_t) i];
            ringFill = (ringFill + 1) % fftSize;
            if (++sinceLast >= hop)
            {
                sinceLast = 0;
                // a long backlog: only the newest frames are worth analysing
                if (n - i <= 4 * hop || framesThisTick == 0)
                {
                    analyse();
                    ++framesThisTick;
                }
            }
        }
    }

    void SpectrumDisplay::analyse()
    {
        for (int i = 0; i < fftSize; ++i)
            frame[(size_t) i] = ring[(size_t) ((ringFill + i) % fftSize)] * window[(size_t) i];
        std::fill (frame.begin() + fftSize, frame.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (frame.data(), true);
        // power per bin such that the bins of the positive half sum to the
        // signal's mean square (Parseval with the window's energy)
        const float scale = 2.0f / ((float) fftSize * windowGain);
        for (int k = 0; k <= fftSize / 2; ++k)
            power[(size_t) k] = frame[(size_t) k] * frame[(size_t) k] * scale;
        power[0] *= 0.5f;

        for (size_t c = 0; c < columnHz.size(); ++c)
        {
            const float db = bandDbFromPower (columnHz[c], power);
            last[c] = db;
            auto& d = displayed[c];
            d = (db >= d || d <= minusInf + 1.0f) ? db : d + (db - d) * (1.0f - release);
        }
        ++framesAnalysed;
    }

    float SpectrumDisplay::bandDbFromPower (float hz, const std::vector<float>& p) const noexcept
    {
        // 1/6-octave band, at least four bins wide so a sine's main lobe fits
        const float df = (float) sampleRate / (float) fftSize;
        const float k = std::pow (2.0f, 1.0f / 12.0f);
        float lo = hz / k, hi = hz * k;
        if (hi - lo < 4.0f * df)
        {
            lo = hz - 2.0f * df;
            hi = hz + 2.0f * df;
        }
        const int k0 = juce::jmax (1, (int) std::floor (lo / df));
        const int k1 = juce::jmin ((int) p.size() - 1, (int) std::ceil (hi / df));
        double sum = 0.0;
        for (int b = k0; b <= k1; ++b)
        {
            const float f0 = ((float) b - 0.5f) * df, f1 = ((float) b + 0.5f) * df;
            const float overlap = juce::jmax (0.0f, juce::jmin (f1, hi) - juce::jmax (f0, lo)) / df;
            sum += (double) p[(size_t) b] * overlap;
        }
        // a sine of amplitude A has mean square A^2 / 2: read it as 20 log10 A
        return sum > 1.0e-20 ? (float) (10.0 * std::log10 (2.0 * sum)) : minusInf;
    }

    float SpectrumDisplay::getDisplayedDb (float hz) const noexcept
    {
        if (columnHz.empty())
            return minusInf;
        const auto it = std::lower_bound (columnHz.begin(), columnHz.end(), hz);
        const size_t i = (size_t) juce::jlimit (0, (int) columnHz.size() - 1, (int) (it - columnHz.begin()));
        return displayed[i];
    }

    float SpectrumDisplay::getFrameDb (float hz) const noexcept
    {
        if (columnHz.empty())
            return minusInf;
        const auto it = std::lower_bound (columnHz.begin(), columnHz.end(), hz);
        const size_t i = (size_t) juce::jlimit (0, (int) columnHz.size() - 1, (int) (it - columnHz.begin()));
        return last[i];
    }

    void SpectrumDisplay::paint (juce::Graphics& g)
    {
        const auto scr = style.screen;

        // grid at the axis' real frequencies and every gridDbStep dB
        g.setColour (style.grid.withAlpha (0.55f));
        for (auto f : style.gridFrequencies)
        {
            const float x = xForFrequency (f);
            g.drawLine (x, scr.getY() + 1.0f, x, scr.getBottom() - 1.0f, 0.6f);
        }
        for (float db = style.topDb - style.gridDbStep; db > style.bottomDb + 0.5f; db -= style.gridDbStep)
        {
            const float y = yForDb (db);
            g.drawLine (scr.getX() + 1.0f, y, scr.getRight() - 1.0f, y, 0.6f);
        }

        // frequency labels under the screen
        if (! style.labelStrip.isEmpty())
        {
            g.setFont (TleetLookAndFeel::labelFont (style.labelCapHeight / 0.72f, false).withHorizontalScale (0.84f));
            g.setColour (style.labelInk);
            for (auto f : style.gridFrequencies)
            {
                const auto text = frequencyLabel (f);
                const float x = xForFrequency (f);
                // centred on its grid line; the end labels are kept inside the strip
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 2.0f;
                auto box = juce::Rectangle<float> (w, style.labelStrip.getHeight()).withCentre ({ x, style.labelStrip.getCentreY() });
                box = box.withX (juce::jlimit (style.labelStrip.getX(), style.labelStrip.getRight() - w, box.getX()));
                g.drawText (text, box, juce::Justification::centred, false);
            }
        }

        if (quality == Quality::off || columnHz.empty())
            return;

        juce::Path line;
        bool started = false;
        for (size_t c = 0; c < columnHz.size(); ++c)
        {
            const float x = scr.getX() + style.inset + (float) c;
            const float y = yForDb (displayed[c] <= minusInf + 1.0f ? style.bottomDb - 10.0f : displayed[c]);
            if (! started) { line.startNewSubPath (x, y); started = true; }
            else           line.lineTo (x, y);
        }
        if (! started)
            return;

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (scr.reduced (1.0f).toNearestInt());

        juce::Path area (line);
        area.lineTo (scr.getRight() - style.inset, scr.getBottom());
        area.lineTo (scr.getX() + style.inset, scr.getBottom());
        area.closeSubPath();
        g.setGradientFill (juce::ColourGradient (style.fill.withAlpha (0.22f), 0.0f, scr.getY(),
                                                 style.fill.withAlpha (0.04f), 0.0f, scr.getBottom(), false));
        g.fillPath (area);

        g.setColour (style.glow.withAlpha (0.35f));
        g.strokePath (line, juce::PathStrokeType (style.lineWidth * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (style.line);
        g.strokePath (line, juce::PathStrokeType (style.lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ==================================================================

    SpectrumQualityButton::SpectrumQualityButton (SpectrumDisplay& d) : display (d)
    {
        setTooltip ("Spectrum analyser quality: only the display's work (FFT size, overlap, refresh); the sound never changes. Off stops the analyser.");
        refresh();
    }

    void SpectrumQualityButton::refresh()
    {
        setButtonText ("FFT " + SpectrumDisplay::qualityName (display.getQuality()));
    }

    void SpectrumQualityButton::clicked()
    {
        juce::PopupMenu m;
        for (int i = 0; i < 4; ++i)
        {
            const auto q = (SpectrumDisplay::Quality) i;
            m.addItem (i + 1, SpectrumDisplay::qualityName (q), true, q == display.getQuality());
        }
        juce::Component::SafePointer<SpectrumQualityButton> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            const auto q = (SpectrumDisplay::Quality) (r - 1);
            safe->display.setQuality (q);
            safe->refresh();
            if (safe->onQualityChanged)
                safe->onQualityChanged (q);
        });
    }
}
