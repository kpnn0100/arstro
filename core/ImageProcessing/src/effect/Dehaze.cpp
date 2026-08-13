#include "Dehaze.h"

namespace arstro
{
    Dehaze::Dehaze() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(amountID, 0);
    }

    void Dehaze::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const int ch = in.channels();
        const int colorCh = ch >= 3 ? 3 : ch;
        const size_t px = in.pixelCount();
        const Pixel amount = getProperty(amountID) / (Pixel)100;
        const Pixel *s = in.data();
        Pixel *d = out.data();

        if (amount == (Pixel)0)  // identity
        {
            for (size_t i = 0; i < px * ch; ++i) d[i] = s[i];
            return;
        }

        // Atmospheric light A = the largest per-pixel dark channel (haziest pixel).
        Pixel A = (Pixel)0.1;
        for (size_t i = 0; i < px; ++i)
        {
            const Pixel *p = s + i * ch;
            Pixel mn = p[0];
            for (int c = 1; c < colorCh; ++c) mn = p[c] < mn ? p[c] : mn;
            if (mn > A) A = mn;
        }

        if (amount > (Pixel)0)
        {
            const Pixel omega = (Pixel)0.95 * amount;
            const Pixel t0 = (Pixel)0.1;
            for (size_t i = 0; i < px; ++i)
            {
                const Pixel *p = s + i * ch;
                Pixel *q = d + i * ch;
                Pixel mn = p[0];
                for (int c = 1; c < colorCh; ++c) mn = p[c] < mn ? p[c] : mn;
                Pixel t = (Pixel)1 - omega * (mn / A);
                if (t < t0) t = t0;
                for (int c = 0; c < colorCh; ++c) q[c] = (p[c] - A) / t + A;
                for (int c = colorCh; c < ch; ++c) q[c] = p[c];
            }
        }
        else
        {
            // add haze: fade toward the airlight
            const Pixel k = -amount * (Pixel)0.6;
            for (size_t i = 0; i < px; ++i)
            {
                const Pixel *p = s + i * ch;
                Pixel *q = d + i * ch;
                for (int c = 0; c < colorCh; ++c) q[c] = p[c] + (A - p[c]) * k;
                for (int c = colorCh; c < ch; ++c) q[c] = p[c];
            }
        }
    }
}
