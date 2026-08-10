#include "Rotate.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        // <cmath> doesn't guarantee M_PI (MSVC only defines it when _USE_MATH_DEFINES
        // is set before the first include of <cmath>/<math.h>, which is order-fragile
        // across translation units) — define it locally instead.
        constexpr double kPi = 3.14159265358979323846;

        // k clockwise 90-degree turns of `in` into `out` (lossless index remap).
        void quarterTurn(const Image &in, Image &out, int k)
        {
            const int W = in.width(), H = in.height(), ch = in.channels();
            k &= 3;
            if (k == 0) { out = in.clone(); return; }
            if (k == 2)
            {
                out.allocate(W, H, ch, in.space());
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x)
                        for (int c = 0; c < ch; ++c)
                            out.at(x, y, c) = in.at(W - 1 - x, H - 1 - y, c);
                return;
            }
            // k == 1 or 3: dims swap to H x W
            out.allocate(H, W, ch, in.space());
            for (int y = 0; y < W; ++y)
                for (int x = 0; x < H; ++x)
                    for (int c = 0; c < ch; ++c)
                        out.at(x, y, c) = (k == 1) ? in.at(y, H - 1 - x, c)
                                                   : in.at(W - 1 - y, x, c);
        }
    }

    Rotate::Rotate() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(angleID, 0);
        initProperty(quarterTurnsID, 0);
    }

    void Rotate::process(const Image &in, Image &out)
    {
        Image turned;
        quarterTurn(in, turned, (int)(getProperty(quarterTurnsID) + (Pixel)0.5));

        const double angle = (double)getProperty(angleID) * kPi / 180.0;
        if (std::fabs(angle) < 1e-9)
        {
            out = std::move(turned);
            return;
        }

        const int w = turned.width(), h = turned.height(), ch = turned.channels();
        out.allocate(w, h, ch, turned.space());
        const double cx = w * 0.5, cy = h * 0.5;
        const double cs = std::cos(angle), sn = std::sin(angle);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                // inverse rotation: source = R(-angle) * (dst - center) + center
                const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
                const double sx = cs * dx + sn * dy + cx - 0.5;
                const double sy = -sn * dx + cs * dy + cy - 0.5;
                Pixel *d = out.row(y) + (size_t)x * ch;
                if (sx < 0 || sy < 0 || sx > w - 1 || sy > h - 1)
                {
                    for (int c = 0; c < ch; ++c) d[c] = 0;  // outside -> transparent/black
                    continue;
                }
                for (int c = 0; c < ch; ++c)
                    d[c] = turned.sampleBilinear((float)sx, (float)sy, c);
            }
        }
    }
}
