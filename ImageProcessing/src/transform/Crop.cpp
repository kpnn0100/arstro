#include "Crop.h"
#include <algorithm>

namespace arstro
{
    Crop::Crop() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(xID, 0); initProperty(yID, 0);
        initProperty(wID, 1); initProperty(hID, 1);
    }

    void Crop::process(const Image &in, Image &out)
    {
        const int sw = in.width(), sh = in.height(), ch = in.channels();
        Pixel nx = getProperty(xID), ny = getProperty(yID);
        Pixel nw = getProperty(wID), nh = getProperty(hID);
        // clamp the normalized rect into [0,1]
        nx = nx < 0 ? 0 : (nx > 1 ? 1 : nx);
        ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
        if (nw < 0) nw = 0; if (nx + nw > 1) nw = 1 - nx;
        if (nh < 0) nh = 0; if (ny + nh > 1) nh = 1 - ny;

        int x0 = (int)(nx * sw + 0.5), y0 = (int)(ny * sh + 0.5);
        int cw = (int)(nw * sw + 0.5), cyh = (int)(nh * sh + 0.5);
        if (cw < 1) cw = 1; if (cyh < 1) cyh = 1;
        if (x0 + cw > sw) cw = sw - x0;
        if (y0 + cyh > sh) cyh = sh - y0;
        if (cw < 1 || cyh < 1 || sw <= 0 || sh <= 0)  // degenerate -> passthrough copy
        {
            out = in.clone();
            return;
        }

        out.allocate(cw, cyh, ch, in.space());
        for (int y = 0; y < cyh; ++y)
        {
            const Pixel *srow = in.row(y0 + y) + (size_t)x0 * ch;
            Pixel *drow = out.row(y);
            std::copy(srow, srow + (size_t)cw * ch, drow);
        }
    }
}
