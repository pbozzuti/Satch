/*
  ==============================================================================

    SaturationEngine.h

    The four saturation "characters" (Tube / Tape / Transistor / Transformer).

    This file is plain C++ with NO dependency on JUCE, so it can also be compiled
    on its own to measure what each style does (harmonics, level, etc).

    It runs at the OVERSAMPLED rate (4x the host rate), one sample at a time:
        out = engine.process (in, channel);
    Everything else (drive, low/high tone, mix, output) lives in PluginProcessor.cpp.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sat
{
    constexpr double kTwoPi = 6.283185307179586;

    //==========================================================================
    // x / (1 + |x|^p)^(1/p): a smooth clipping curve with slope 1 at the origin.
    // Small p = soft and gentle, large p = hard, near-clipping. Stays between -1 and 1.
    inline float softShape (float x, float p) noexcept
    {
        const float ax = std::abs (x);

        if (ax <= 1.0f)
            return x / std::pow (1.0f + std::pow (ax, p), 1.0f / p);

        // same curve rewritten for large inputs so |x|^p can never overflow
        return std::copysign (1.0f / std::pow (1.0f + std::pow (1.0f / ax, p), 1.0f / p), x);
    }

    //==========================================================================
    // Simple one-pole low-pass (6 dB/oct), two channels.
    struct OnePole
    {
        float a = 0.0f;
        float z[2] {};

        void setCutoff (double freq, double sampleRate) noexcept
        {
            a = (float) (1.0 - std::exp (-kTwoPi * freq / sampleRate));
        }

        float lp (int ch, float x) noexcept
        {
            z[ch] += a * (x - z[ch]);
            return z[ch];
        }

        void reset() noexcept { z[0] = z[1] = 0.0f; }
    };

    //==========================================================================
    // Biquad filter (RBJ "Audio EQ Cookbook"), two channels. Double precision so that
    // low frequencies at a high sample rate stay accurate.
    class Biquad
    {
    public:
        void setLowShelf (double sampleRate, double freq, double gainDb, double q = 0.707) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), c = std::cos (w (freq, sampleRate));
            const double s = 2.0 * std::sqrt (A) * (std::sin (w (freq, sampleRate)) / (2.0 * q));

            set (      A * ((A + 1) - (A - 1) * c + s),
                 2.0 * A * ((A - 1) - (A + 1) * c),
                       A * ((A + 1) - (A - 1) * c - s),
                            (A + 1) + (A - 1) * c + s,
                -2.0 *     ((A - 1) + (A + 1) * c),
                            (A + 1) + (A - 1) * c - s);
        }

        void setHighShelf (double sampleRate, double freq, double gainDb, double q = 0.707) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), c = std::cos (w (freq, sampleRate));
            const double s = 2.0 * std::sqrt (A) * (std::sin (w (freq, sampleRate)) / (2.0 * q));

            set (           A * ((A + 1) + (A - 1) * c + s),
                 -2.0 *     A * ((A - 1) + (A + 1) * c),
                            A * ((A + 1) + (A - 1) * c - s),
                                 (A + 1) - (A - 1) * c + s,
                  2.0 *          ((A - 1) - (A + 1) * c),
                                 (A + 1) - (A - 1) * c - s);
        }

        // A bump (or dip) around one frequency
        void setPeak (double sampleRate, double freq, double gainDb, double q = 1.0) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), c = std::cos (w (freq, sampleRate));
            const double alpha = std::sin (w (freq, sampleRate)) / (2.0 * q);

            set (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A,
                 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
        }

        void reset() noexcept
        {
            for (auto& v : z1) v = 0.0;
            for (auto& v : z2) v = 0.0;
        }

        float process (int ch, float xf) noexcept
        {
            const double x = xf;
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return (float) y;
        }

    private:
        static double w (double f, double sr) noexcept { return kTwoPi * f / sr; }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
        {
            b0 = nb0 / na0;  b1 = nb1 / na0;  b2 = nb2 / na0;
            a1 = na1 / na0;  a2 = na2 / na0;
        }

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1[2] {}, z2[2] {};
    };

    //==========================================================================
    // Harmonic generator. Uses Chebyshev polynomials, which turn a sine of amplitude 1 into a pure
    // 2nd, 3rd, 4th... harmonic. The input is first scaled by its own peak level, so the harmonics
    // are present at ANY signal level (not just when the signal is loud) and their strength relative
    // to the original is set directly by the amounts a2..a7.
    struct Exciter
    {
        float peak[2] {};
        float atk = 0.0f, rel = 0.0f;

        void setTimes (double attackSec, double releaseSec, double sampleRate) noexcept
        {
            atk = (float) (1.0 - std::exp (-1.0 / (attackSec  * sampleRate)));
            rel = (float) (1.0 - std::exp (-1.0 / (releaseSec * sampleRate)));
        }

        void reset() noexcept { peak[0] = peak[1] = 0.0f; }

        float process (float x, int ch, float a2, float a3, float a4, float a5, float a7) noexcept
        {
            const float ax = std::abs (x);
            peak[ch] += (ax > peak[ch] ? atk : rel) * (ax - peak[ch]);

            const float sc = std::max (peak[ch], 0.01f);            // below this, stay quiet (no hiss from silence)
            const float xn = std::clamp (x / sc, -1.0f, 1.0f);

            // Chebyshev recurrence: T(n+1) = 2 x T(n) - T(n-1)
            const float t2 = 2.0f * xn * xn - 1.0f;
            const float t3 = 2.0f * xn * t2 - xn;
            const float t4 = 2.0f * xn * t3 - t2;
            const float t5 = 2.0f * xn * t4 - t3;
            const float t6 = 2.0f * xn * t5 - t4;
            const float t7 = 2.0f * xn * t6 - t5;

            // fade the generator out completely for silence (the even polynomials have a constant at x = 0)
            const float gate = std::min (peak[ch] * 100.0f, 1.0f);

            return gate * sc * (a2 * t2 + a3 * t3 + a4 * t4 + a5 * t5 + a7 * t7);
        }
    };

    //==========================================================================
    class SaturationEngine
    {
    public:
        enum Type { Tube = 0, Tape, Transistor, Transformer, NumTypes };

        // osRate = the oversampled sample rate (e.g. 4 x 48000)
        void prepare (double osRate) noexcept
        {
            fs = osRate;
            auto coef = [this] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); };
            auto lpA  = [this] (double hz)      { return (float) (1.0 - std::exp (-kTwoPi * hz / fs)); };

            // Tube: level followers (fast one drives the lopsidedness, slow one is the supply "sag"),
            //       warm low-mids and a smooth top
            tubeAtk = coef (0.001);  tubeRel = coef (0.060);
            sagAtk  = coef (0.020);  sagRel  = coef (0.200);
            tubeWarm.setLowShelf (fs, 300.0, 2.0);
            tubeLp.setCutoff (15000.0, fs);
            exTube.setTimes (0.0002, 0.060, fs);

            // Tape: highs split off so they can be driven harder, a low bump, a top end that dulls as the
            //       level rises, and grain (noise that follows the signal)
            tapePre.setCutoff  (3000.0, fs);
            tapePost.setCutoff (3000.0, fs);
            tapeBump.setLowShelf (fs, 70.0, 3.0);
            tapeAtk = coef (0.005);  tapeRel = coef (0.080);
            tapeAHi = lpA (12000.0); tapeALo = lpA (2500.0);
            grainHp.setCutoff (1500.0, fs);
            exTape.setTimes (0.0002, 0.060, fs);

            // Transistor: a midrange boost into the clipper, plus some extra top for the bite
            trMid.setPeak (fs, 2200.0, 6.0, 0.9);
            trTop.setHighShelf (fs, 5000.0, 3.0);
            exTr.setTimes (0.0002, 0.060, fs);

            // Transformer: core "flux" = the low part of the signal; heavy low-mid weight; core memory; dark top
            xfFlux.setCutoff (160.0, fs);
            xfMem.setCutoff  (60.0, fs);
            xfWeight.setLowShelf (fs, 130.0, 6.0);
            xfLp.setCutoff (8000.0, fs);
            exXf.setTimes (0.0005, 0.150, fs);                  // slower: it works on the low end

            reset();
            updateComp();
        }

        void reset() noexcept
        {
            for (int c = 0; c < 2; ++c) { env[c] = sag[c] = tapeEnv[c] = tapeRz[c] = xfPrev[c] = 0.0f; grainSeed[c] = 0x9E3779B9u + (uint32_t) c * 7919u; }
            tubeWarm.reset(); tubeLp.reset();
            tapePre.reset(); tapePost.reset(); tapeBump.reset(); grainHp.reset();
            trMid.reset(); trTop.reset();
            xfFlux.reset(); xfMem.reset(); xfWeight.reset(); xfLp.reset();
            exTube.reset(); exTape.reset(); exTr.reset(); exXf.reset();
        }

        void setType (int t) noexcept { type = std::clamp (t, 0, (int) NumTypes - 1); updateComp(); }

        // h: 0 = softest, 1 = hardest. Comes from the Softness control (turned the other way round).
        // It changes BOTH how sharp the knee of the curve is (p) and where clipping begins (g):
        //   soft  -> very rounded knee, clipping starts late (gentle, lots of headroom)
        //   hard  -> razor-sharp knee, clipping starts very early (gritty, aggressive)
        // The harmonic generators also scale with it (more at the hard end).
        void setHardness (float h) noexcept
        {
            h01   = std::clamp (h, 0.0f, 1.0f);
            p     = 1.05f * std::pow (40.0f / 1.05f, h01);   // 1.05 .. 40
            g     = 0.40f * std::pow (10.0f, h01);           // 0.4 .. 4.0
            harmK = 0.8f + 1.4f * h01;                       // 0.8 .. 2.2
            updateComp();
        }

        float process (float x, int ch) noexcept
        {
            float y;

            switch (type)
            {
                case Tube:        y = tube (x, ch);        break;
                case Tape:        y = tape (x, ch);        break;
                case Transistor:  y = transistor (x, ch);  break;
                default:          y = transformer (x, ch); break;
            }

            // Softness changes how hard each style clips, which would also change its loudness; this undoes that
            y *= softComp;

            // Safety: very hot settings can push a style far above 0 dBFS. This is a gentle ceiling at about
            // +6 dBFS that leaves normal levels alone (only -0.35 dB at 0 dBFS) but tames extreme ones.
            return 2.0f * softShape (y * 0.5f, 3.0f);
        }

        // Each style has a different natural loudness; this evens them out so switching tabs doesn't jump in level.
        static float trimDb (int t) noexcept
        {
            // measured on pink noise (-24 and -14 dBFS RMS, drive 12 dB, softness 50%)
            static constexpr float kTrim[NumTypes] = { +0.4f, +3.0f, -0.3f, -3.0f };
            return kTrim[std::clamp (t, 0, (int) NumTypes - 1)];
        }

    private:
        // Level compensation across the Softness range: dB offsets at softness (hardness) 0, 25, 50, 75, 100 %.
        // Filled in from the offline measurement (see the test tools), 0 dB at the middle setting.
        static constexpr float kSoftComp[NumTypes][5] = {
            { +1.1f, +1.0f, +0.0f, -0.1f, -0.5f },   // Tube
            { -4.1f, -2.6f, +0.0f, +3.9f, +6.0f },   // Tape
            { -2.9f, -2.2f, +0.0f, +3.3f, +4.1f },   // Transistor
            { +2.8f, +2.2f, +0.0f, +0.5f, +3.4f },   // Transformer
        };

        void updateComp() noexcept
        {
            const float pos = h01 * 4.0f;
            const int   i0  = std::min ((int) pos, 3);
            const float f   = pos - (float) i0;
            const float db  = kSoftComp[type][i0] * (1.0f - f) + kSoftComp[type][i0 + 1] * f;
            softComp = std::pow (10.0f, db / 20.0f);
        }

        // The clipping curve with the Softness settings applied (slope 1 at the origin whatever g is)
        float knee (float x, float pp) const noexcept { return softShape (x * g, pp) / g; }

        float grain (int ch) noexcept
        {
            uint32_t& s = grainSeed[ch];
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;                          // xorshift32
            const float white = (float) (int32_t) s * (1.0f / 2147483648.0f);   // -1 .. 1
            return white - grainHp.lp (ch, white);                              // keep the brighter part: a "fizzy" grain
        }

        //----------------------------------------------------------------------
        // TUBE: lopsided (asymmetric) soft clipping -> strong even harmonics (2nd, 4th).
        //  - the lopsidedness grows with level, like a valve being pushed
        //  - supply "sag": loud passages squash a little and bloom back (slow, unlike a fast limiter)
        //  - warm low-mids, smooth top
        //  - a harmonic generator adds 2nd / 4th (and a little 3rd) at every level
        float tube (float x, int ch) noexcept
        {
            const float ax = std::abs (x);
            float& e = env[ch];
            e += (ax > e ? tubeAtk : tubeRel) * (ax - e);
            float& s = sag[ch];
            s += (ax > s ? sagAtk : sagRel) * (ax - s);

            x /= 1.0f + kTubeSag * std::min (s, 2.0f);

            const float pPos = std::max (1.05f, p * 0.5f);     // gentler than a transistor's curve
            const float bias = 0.02f + 0.42f * std::min (e, 1.5f);
            const float xb   = x + bias;

            float y = (xb >= 0.0f ? knee (xb, pPos) : knee (xb, 1.0f)) - knee (bias, pPos);

            y += harmK * exTube.process (x, ch, 0.17f, 0.040f, 0.045f, 0.0f, 0.0f);

            return tubeLp.lp (ch, tubeWarm.process (ch, y));
        }

        //----------------------------------------------------------------------
        // TAPE: soft, rounded symmetric curve (3rd harmonic, a little 5th). Highs are driven harder than lows
        // and then restored, so loud highs compress. A low bump gives the rubbery bottom end, the top end dulls
        // more and more as the signal gets louder (tape self-erasure), and GRAIN (noise that follows the
        // signal, silent when nothing plays) gives it texture.
        float tape (float x, int ch) noexcept
        {
            // the low "head bump" goes in BEFORE the curve, so the bass also saturates more (rubbery)
            const float xin = tapeBump.process (ch, x);

            const float ax = std::abs (xin);
            float& e = tapeEnv[ch];
            e += (ax > e ? tapeAtk : tapeRel) * (ax - e);

            const float lo = tapePre.lp (ch, xin);
            const float xs = lo + (xin - lo) * kTapeHfDrive;

            // a hint of lopsidedness gives a little low-order (2nd) warmth next to the odd harmonics
            constexpr float kWarmth = 0.03f;
            float y = (std::tanh ((xs + kWarmth) * g) - std::tanh (kWarmth * g)) / g;
            const float w = 0.5f * h01 * h01;                  // hard end: a slightly sharper knee mixed in
            if (w > 0.0f)
            {
                const float pk = std::max (2.0f, p * 0.5f);
                y += w * (knee (xs + kWarmth, pk) - knee (kWarmth, pk) - y);
            }

            y += harmK * exTape.process (xin, ch, 0.025f, 0.120f, 0.0f, 0.040f, 0.0f);

            const float lo2 = tapePost.lp (ch, y);
            y = lo2 + (y - lo2) / kTapeHfDrive;

            // grain: noise scaled by how loud the signal is right now
            y += kGrain * std::min (e, 1.5f) * grain (ch);

            // louder -> duller: the roll-off moves from 12 kHz down toward 2.5 kHz
            const float a = tapeAHi + (tapeALo - tapeAHi) * std::min (e * 1.2f, 1.0f);
            tapeRz[ch] += a * (y - tapeRz[ch]);
            return tapeRz[ch];
        }

        //----------------------------------------------------------------------
        // TRANSISTOR: hard clipping with a slight asymmetry (mixed harmonics). Midrange and top are
        // boosted going in, so the bite is bright and focused around 2 kHz. No smoothing, no squash.
        // The harmonic generator adds a stack of odd harmonics (3rd, 5th, 7th) for the buzz.
        float transistor (float x, int ch) noexcept
        {
            const float xf = trTop.process (ch, trMid.process (ch, x));
            const float pp = std::clamp (p * 1.5f, 2.0f, 60.0f);
            constexpr float b = 0.05f;

            float y = knee (xf + b, pp) - knee (b, pp);
            y += harmK * exTr.process (xf, ch, 0.020f, 0.150f, 0.0f, 0.065f, 0.035f);
            return y;
        }

        //----------------------------------------------------------------------
        // TRANSFORMER: the iron core saturates mostly on LOW frequencies (flux), with a lopsided
        // hysteresis (the core remembers its recent magnetisation) -> strong 2nd harmonic and
        // heavy low-mid weight, with a dull top end. The harmonic generator works on the low end only.
        float transformer (float x, int ch) noexcept
        {
            const float lf = xfFlux.lp (ch, x);
            const float hf = x - lf;

            // Low end saturates with a lopsided flux curve -> strong 2nd harmonic
            constexpr float kFluxBias = 0.35f;
            const float pF = std::max (1.05f, p * 0.4f);
            const float kF = 4.0f * g;
            const float lfSat = (softShape (lf * kF + kFluxBias, pF) - softShape (kFluxBias, pF)) / kF
                              + harmK * exXf.process (lf, ch, 0.24f, 0.070f, 0.040f, 0.0f, 0.0f);

            const float y = xfWeight.process (ch, hf + lfSat);

            // The core remembers its recent magnetisation: the offset drifts with the signal (hysteresis).
            // The final stage sees a reduced level so mids and highs stay cleaner than the lows.
            const float memory = xfMem.lp (ch, xfPrev[ch]);
            const float bias   = 0.16f - 0.20f * memory;
            const float pT     = std::max (1.05f, p * 0.35f);
            const float kFinal = 0.25f + 0.45f * h01;          // soft: mids stay clean; hard: the whole signal grits up
            const float gk     = g * kFinal;

            const float out = (softShape (y * gk + bias, pT) - softShape (bias, pT)) / gk;
            xfPrev[ch] = out;

            return xfLp.lp (ch, out);
        }

        //----------------------------------------------------------------------
        static constexpr float kTapeHfDrive = 1.5f;
        static constexpr float kTubeSag     = 0.5f;
        static constexpr float kGrain       = 0.10f;

        double fs = 192000.0;
        int    type = Tube;
        float  h01 = 0.5f, p = 4.0f, g = 1.05f, harmK = 1.0f, softComp = 1.0f;

        // Tube
        float env[2] {}, sag[2] {};
        float tubeAtk = 0.0f, tubeRel = 0.0f, sagAtk = 0.0f, sagRel = 0.0f;
        Biquad  tubeWarm;
        OnePole tubeLp;
        Exciter exTube;

        // Tape
        OnePole tapePre, tapePost, grainHp;
        Biquad  tapeBump;
        float tapeEnv[2] {}, tapeRz[2] {};
        float tapeAtk = 0.0f, tapeRel = 0.0f, tapeAHi = 0.0f, tapeALo = 0.0f;
        uint32_t grainSeed[2] {};
        Exciter exTape;

        // Transistor
        Biquad trMid, trTop;
        Exciter exTr;

        // Transformer
        OnePole xfFlux, xfMem, xfLp;
        Biquad  xfWeight;
        float   xfPrev[2] {};
        Exciter exXf;
    };
}
