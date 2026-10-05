#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>

namespace arstro
{
namespace interstellar
{
namespace render
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        /** Rows [0, h) split over a few threads — the effects are per-pixel independent. */
        void parallelRows(int h, const std::function<void(int, int)> &fn)
        {
            const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
            const int n = std::clamp(std::min(hw, 8), 1, std::max(1, h / 16));
            if (n <= 1) { fn(0, h); return; }
            std::vector<std::thread> ts;
            for (int i = 0; i < n; ++i)
            {
                const int y0 = h * i / n, y1 = h * (i + 1) / n;
                ts.emplace_back([&fn, y0, y1] { fn(y0, y1); });
            }
            for (auto &t : ts) t.join();
        }

        /** The pixels of `img` as T: 8-bit, or the deep payload (R-COLOR-1). */
        template <class T> T *px(Raster &img);
        template <> uint8_t *px<uint8_t>(Raster &img) { return img.rgba.data(); }
        template <> uint16_t *px<uint16_t>(Raster &img) { return img.rgba16.data(); }
        template <class T> const T *px(const Raster &img) { return px<T>(const_cast<Raster &>(img)); }
        template <class T> constexpr int kMax = (int)((1u << (8 * sizeof(T))) - 1);

        void allocateLike(Raster &out, int w, int h, bool deep)
        {
            if (deep) out.allocate16(w, h);
            else out.allocate(w, h);
        }

        /** One separable box pass of radius r along rows (horizontal) or columns. Edges clamp. */
        template <class T>
        void boxPass(const Raster &in, Raster &out, int r, bool horizontal)
        {
            const int w = in.width, h = in.height;
            allocateLike(out, w, h, sizeof(T) > 1);
            if (r <= 0) { out = in; return; }
            const int lines = horizontal ? h : w, len = horizontal ? w : h;
            const int win = 2 * r + 1;
            const T *src = px<T>(in);
            T *dst = px<T>(out);
            parallelRows(lines, [&](int l0, int l1) {
                for (int l = l0; l < l1; ++l)
                {
                    auto at = [&](int i) -> const T * {
                        i = std::clamp(i, 0, len - 1);
                        return horizontal ? &src[((size_t)l * w + i) * 4] : &src[((size_t)i * w + l) * 4];
                    };
                    long long sum[4] = {0, 0, 0, 0};
                    for (int i = -r; i <= r; ++i)
                        for (int c = 0; c < 4; ++c) sum[c] += at(i)[c];
                    for (int i = 0; i < len; ++i)
                    {
                        T *o = horizontal ? &dst[((size_t)l * w + i) * 4] : &dst[((size_t)i * w + l) * 4];
                        for (int c = 0; c < 4; ++c) o[c] = (T)((sum[c] + win / 2) / win);
                        const T *add = at(i + r + 1), *sub = at(i - r);
                        for (int c = 0; c < 4; ++c) sum[c] += add[c] - sub[c];
                    }
                }
            });
        }

        template <class T>
        void boxBlur(Raster &img, int r)
        {
            if (r <= 0) return;
            Raster tmp;
            boxPass<T>(img, tmp, r, true);
            boxPass<T>(tmp, img, r, false);
        }

        /** Three box radii whose successive application approximates a Gaussian of σ (Kovesi). */
        void boxesForGauss(double sigma, int out[3])
        {
            const int n = 3;
            const double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
            int wl = (int)std::floor(wIdeal);
            if (wl % 2 == 0) --wl;
            const int wu = wl + 2;
            const double mIdeal = (12.0 * sigma * sigma - n * wl * wl - 4.0 * n * wl - 3.0 * n) / (-4.0 * wl - 4.0);
            const int m = (int)std::lround(mIdeal);
            for (int i = 0; i < n; ++i) out[i] = ((i < m ? wl : wu) - 1) / 2;
        }

        /** Bilinear sample, clamped to the image, into four floats. */
        template <class T>
        inline void sample(const Raster &img, double x, double y, float *out)
        {
            const int w = img.width, h = img.height;
            x = std::clamp(x, 0.0, (double)(w - 1));
            y = std::clamp(y, 0.0, (double)(h - 1));
            const int x0 = (int)x, y0 = (int)y, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const float fx = (float)(x - x0), fy = (float)(y - y0);
            const T *p = px<T>(img);
            const T *a = &p[((size_t)y0 * w + x0) * 4], *b = &p[((size_t)y0 * w + x1) * 4];
            const T *c = &p[((size_t)y1 * w + x0) * 4], *d = &p[((size_t)y1 * w + x1) * 4];
            for (int k = 0; k < 4; ++k)
            {
                const float top = a[k] + ((float)b[k] - a[k]) * fx, bot = c[k] + ((float)d[k] - c[k]) * fx;
                out[k] = top + (bot - top) * fy;
            }
        }

        /** Average `n(x,y)` samples along a path per pixel; `pos(x, y, i, n, sx, sy)` gives the i-th. */
        template <class T, class NFn, class PosFn>
        void pathBlur(Raster &img, NFn count, PosFn pos)
        {
            const Raster in = img;
            const int w = img.width;
            T *dst = px<T>(img);
            parallelRows(img.height, [&](int y0, int y1) {
                float p[4];
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const int n = count(x, y);
                        if (n <= 1) continue;
                        float acc[4] = {0, 0, 0, 0};
                        for (int i = 0; i < n; ++i)
                        {
                            double sx = 0, sy = 0;
                            pos(x, y, i, n, sx, sy);
                            sample<T>(in, sx, sy, p);
                            for (int k = 0; k < 4; ++k) acc[k] += p[k];
                        }
                        T *o = &dst[((size_t)y * w + x) * 4];
                        for (int k = 0; k < 3; ++k) o[k] = (T)std::clamp((int)std::lround(acc[k] / n), 0, kMax<T>);
                    }
            });
        }

        template <class T>
        void run(const EffectRun &e, double scale, Raster &img)
        {
            const Raster before = e.mix < 1.0 ? img : Raster{};
            const int w = img.width, h = img.height;
            if (e.type == "blur.gaussian")
            {
                const double sigma = std::max(0.0, e.get("radius") * scale * 0.5);
                if (sigma >= 0.5)
                {
                    int r[3];
                    boxesForGauss(sigma, r);
                    for (int i = 0; i < 3; ++i) boxBlur<T>(img, r[i]);
                }
            }
            else if (e.type == "blur.box")
                boxBlur<T>(img, (int)std::lround(std::max(0.0, e.get("radius") * scale)));
            else if (e.type == "blur.directional")
            {
                const double len = std::max(0.0, e.get("length") * scale);
                const double a = e.get("angle") * kPi / 180.0, dx = std::cos(a), dy = -std::sin(a);   // y grows down
                const int n = std::clamp((int)std::ceil(len) + 1, 1, 64);
                if (len >= 1.0)
                    pathBlur<T>(img, [n](int, int) { return n; }, [&](int x, int y, int i, int cnt, double &sx, double &sy) {
                        const double s = (i / (double)(cnt - 1) - 0.5) * len;
                        sx = x + dx * s;
                        sy = y + dy * s;
                    });
            }
            else if (e.type == "blur.zoom")
            {
                const double amt = std::clamp(e.get("amount"), 0.0, 1.0);
                const double cx = e.get("centerX") * (w - 1), cy = e.get("centerY") * (h - 1);
                if (amt > 0.0)
                    pathBlur<T>(img,
                                [&](int x, int y) { return std::clamp((int)std::ceil(amt * std::hypot(x - cx, y - cy)) + 1, 1, 48); },
                                [&](int x, int y, int i, int cnt, double &sx, double &sy) {
                                    const double t = 1.0 - amt * i / (double)(cnt - 1);
                                    sx = cx + (x - cx) * t;
                                    sy = cy + (y - cy) * t;
                                });
            }
            else if (e.type == "blur.spin")
            {
                const double ang = std::clamp(e.get("angle"), 0.0, 90.0) * kPi / 180.0;
                const double cx = e.get("centerX") * (w - 1), cy = e.get("centerY") * (h - 1);
                if (ang > 0.0)
                    pathBlur<T>(img,
                                [&](int x, int y) { return std::clamp((int)std::ceil(ang * std::hypot(x - cx, y - cy)) + 1, 1, 48); },
                                [&](int x, int y, int i, int cnt, double &sx, double &sy) {
                                    const double r = std::hypot(x - cx, y - cy), th = std::atan2(y - cy, x - cx);
                                    const double t = th + (i / (double)(cnt - 1) - 0.5) * ang;
                                    sx = cx + r * std::cos(t);
                                    sy = cy + r * std::sin(t);
                                });
            }
            if (e.mix < 1.0)
            {
                const float m = (float)e.mix;
                T *o = px<T>(img);
                const T *b = px<T>(before);
                const size_t n = (size_t)w * h * 4;
                for (size_t i = 0; i < n; ++i) o[i] = (T)std::lround(b[i] + ((float)o[i] - b[i]) * m);
            }
        }
    }

    const std::vector<EffectTypeDef> &effectCatalog()
    {
        static const std::vector<EffectTypeDef> k = {
            {"blur.gaussian", "Gaussian Blur", "Blur", {{"radius", "Radius", 8.0, 0.0, 200.0, "px"}}},
            {"blur.box", "Box Blur", "Blur", {{"radius", "Radius", 8.0, 0.0, 200.0, "px"}}},
            {"blur.directional", "Directional Blur", "Blur",
             {{"length", "Length", 30.0, 0.0, 400.0, "px"}, {"angle", "Angle", 0.0, -180.0, 180.0, "deg"}}},
            {"blur.zoom", "Zoom Blur", "Blur",
             {{"amount", "Amount", 0.2, 0.0, 1.0, ""}, {"centerX", "Centre X", 0.5, 0.0, 1.0, ""}, {"centerY", "Centre Y", 0.5, 0.0, 1.0, ""}}},
            {"blur.spin", "Spin Blur", "Blur",
             {{"angle", "Angle", 5.0, 0.0, 90.0, "deg"}, {"centerX", "Centre X", 0.5, 0.0, 1.0, ""}, {"centerY", "Centre Y", 0.5, 0.0, 1.0, ""}}},
        };
        return k;
    }

    const EffectTypeDef *effectType(const std::string &type)
    {
        for (const auto &t : effectCatalog())
            if (t.type == type) return &t;
        return nullptr;
    }

    bool applyEffect(const EffectRun &e, double scale, Raster &img)
    {
        if (!effectType(e.type)) return false;
        if (img.empty() || e.mix <= 0.0) return true;
        if (img.deep()) run<uint16_t>(e, scale, img);   // R-COLOR-1: a delivery's deep frame, the same kernels at 16 bits
        else run<uint8_t>(e, scale, img);
        return true;
    }
}
}
}
