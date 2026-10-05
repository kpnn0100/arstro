/*
 *  interstellar_render — Lut implementation. See Lut.h.
 */
#include "Lut.h"
#include "base/Parallel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace interstellar
{
namespace render
{
    namespace
    {
        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
            return s.substr(a, b - a);
        }
    }

    void Lut::map(const float in[3], float out[3]) const
    {
        float u[3];
        for (int c = 0; c < 3; ++c)
        {
            const float span = domainMax[c] - domainMin[c];
            u[c] = span > 0 ? std::min(1.0f, std::max(0.0f, (in[c] - domainMin[c]) / span)) : 0.0f;
        }
        if (size1d > 1)
        {
            const int n = size1d;
            for (int c = 0; c < 3; ++c)
            {
                const float f = u[c] * (n - 1);
                const int i = std::min(n - 2, (int)f);
                const float t = f - i;
                out[c] = table[(size_t)i * 3 + c] + (table[(size_t)(i + 1) * 3 + c] - table[(size_t)i * 3 + c]) * t;
            }
            return;
        }
        if (size3d < 2) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; return; }
        // tetrahedral: the cube cell is split into six tetrahedra by the order of the fractions
        const int n = size3d;
        const float fr = u[0] * (n - 1), fg = u[1] * (n - 1), fb = u[2] * (n - 1);
        const int r0 = std::min(n - 2, (int)fr), g0 = std::min(n - 2, (int)fg), b0 = std::min(n - 2, (int)fb);
        const float x = fr - r0, y = fg - g0, z = fb - b0;
        auto at = [&](int dr, int dg, int db) {
            return &table[(((size_t)(b0 + db) * n + (g0 + dg)) * n + (r0 + dr)) * 3];
        };
        const float *c000 = at(0, 0, 0), *c111 = at(1, 1, 1);
        const float *a, *b;
        float w0, w1, w2, w3;
        if (x >= y)
        {
            if (y >= z) { a = at(1, 0, 0); b = at(1, 1, 0); w0 = 1 - x; w1 = x - y; w2 = y - z; w3 = z; }
            else if (x >= z) { a = at(1, 0, 0); b = at(1, 0, 1); w0 = 1 - x; w1 = x - z; w2 = z - y; w3 = y; }
            else { a = at(0, 0, 1); b = at(1, 0, 1); w0 = 1 - z; w1 = z - x; w2 = x - y; w3 = y; }
        }
        else
        {
            if (z >= y) { a = at(0, 0, 1); b = at(0, 1, 1); w0 = 1 - z; w1 = z - y; w2 = y - x; w3 = x; }
            else if (z >= x) { a = at(0, 1, 0); b = at(0, 1, 1); w0 = 1 - y; w1 = y - z; w2 = z - x; w3 = x; }
            else { a = at(0, 1, 0); b = at(1, 1, 0); w0 = 1 - y; w1 = y - x; w2 = x - z; w3 = z; }
        }
        for (int c = 0; c < 3; ++c) out[c] = w0 * c000[c] + w1 * a[c] + w2 * b[c] + w3 * c111[c];
    }

    void Lut::apply(Raster &img, double mix) const
    {
        if (empty() || img.empty() || mix <= 0.0) return;
        const int w = img.width;
        const bool deep = img.deep();
        if ((deep ? img.rgba16.size() : img.rgba.size()) < (size_t)w * img.height * 4) return;
        const float m = (float)std::min(1.0, mix);
        par::parallelFor(img.height, [&](int y0, int y1) {
            float in[3], out[3];
            for (int y = y0; y < y1; ++y)
            {
                if (deep)
                {
                    uint16_t *p = img.rgba16.data() + (size_t)y * w * 4;
                    for (int x = 0; x < w; ++x, p += 4)
                    {
                        for (int c = 0; c < 3; ++c) in[c] = p[c] / 65535.0f;
                        map(in, out);
                        for (int c = 0; c < 3; ++c)
                            p[c] = (uint16_t)(std::min(1.0f, std::max(0.0f, in[c] + (out[c] - in[c]) * m)) * 65535.0f + 0.5f);
                    }
                }
                else
                {
                    uint8_t *p = img.rgba.data() + (size_t)y * w * 4;
                    for (int x = 0; x < w; ++x, p += 4)
                    {
                        for (int c = 0; c < 3; ++c) in[c] = p[c] / 255.0f;
                        map(in, out);
                        for (int c = 0; c < 3; ++c)
                            p[c] = (uint8_t)(std::min(1.0f, std::max(0.0f, in[c] + (out[c] - in[c]) * m)) * 255.0f + 0.5f);
                    }
                }
            }
        });
    }

    bool readCube(const std::string &path, Lut &out, std::string &err)
    {
        out = Lut{};
        std::ifstream f(path);
        if (!f) { err = "cannot read " + path; return false; }
        std::string line;
        int lineNo = 0;
        size_t want = 0;
        while (std::getline(f, line))
        {
            ++lineNo;
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            std::istringstream ss(t);
            std::string word;
            ss >> word;
            auto fail = [&](const std::string &why) {
                err = path + ":" + std::to_string(lineNo) + ": " + why;
                out = Lut{};
                return false;
            };
            if (word == "TITLE")
            {
                const auto q0 = t.find('"'), q1 = t.rfind('"');
                out.title = q0 != std::string::npos && q1 > q0 ? t.substr(q0 + 1, q1 - q0 - 1) : trim(t.substr(5));
            }
            else if (word == "LUT_1D_SIZE" || word == "LUT_3D_SIZE")
            {
                int n = 0;
                if (!(ss >> n) || n < 2 || n > (word == "LUT_1D_SIZE" ? 65536 : 256)) return fail(word + " out of range");
                if (out.size1d || out.size3d) return fail("a second size line");
                (word == "LUT_1D_SIZE" ? out.size1d : out.size3d) = n;
                want = word == "LUT_1D_SIZE" ? (size_t)n : (size_t)n * n * n;
                out.table.reserve(want * 3);
            }
            else if (word == "DOMAIN_MIN" || word == "DOMAIN_MAX")
            {
                float *d = word == "DOMAIN_MIN" ? out.domainMin : out.domainMax;
                if (!(ss >> d[0] >> d[1] >> d[2])) return fail(word + " needs three numbers");
            }
            else if (word == "LUT_1D_INPUT_RANGE" || word == "LUT_3D_INPUT_RANGE")
            {
                float lo = 0, hi = 1;
                if (!(ss >> lo >> hi)) return fail(word + " needs two numbers");
                for (int c = 0; c < 3; ++c) { out.domainMin[c] = lo; out.domainMax[c] = hi; }
            }
            else
            {
                // a data row: three numbers
                char *end = nullptr;
                const char *p = t.c_str();
                float v[3];
                for (int c = 0; c < 3; ++c)
                {
                    v[c] = std::strtof(p, &end);
                    if (end == p) return fail("expected a keyword or three numbers, got `" + t + "`");
                    p = end;
                }
                if (!want) return fail("data before LUT_1D_SIZE / LUT_3D_SIZE");
                if (out.table.size() >= want * 3) return fail("more rows than the size says (" + std::to_string(want) + ")");
                out.table.insert(out.table.end(), v, v + 3);
            }
        }
        if (!want) { err = path + ": no LUT_1D_SIZE or LUT_3D_SIZE"; out = Lut{}; return false; }
        if (out.table.size() != want * 3)
        {
            err = path + ": " + std::to_string(out.table.size() / 3) + " rows, the size says " + std::to_string(want);
            out = Lut{};
            return false;
        }
        for (int c = 0; c < 3; ++c)
            if (!(out.domainMax[c] > out.domainMin[c])) { err = path + ": DOMAIN_MAX must exceed DOMAIN_MIN"; out = Lut{}; return false; }
        return true;
    }

    bool writeCube(const std::string &path, const Lut &lut, const std::vector<std::string> &comments, std::string &err)
    {
        if (lut.empty()) { err = "nothing to write"; return false; }
        std::ofstream f(path);
        if (!f) { err = "cannot write " + path; return false; }
        for (const auto &c : comments) f << "# " << c << "\n";
        if (!lut.title.empty()) f << "TITLE \"" << lut.title << "\"\n";
        if (lut.size3d) f << "LUT_3D_SIZE " << lut.size3d << "\n";
        else f << "LUT_1D_SIZE " << lut.size1d << "\n";
        char b[96];
        std::snprintf(b, sizeof b, "DOMAIN_MIN %g %g %g\nDOMAIN_MAX %g %g %g\n", lut.domainMin[0], lut.domainMin[1], lut.domainMin[2],
                      lut.domainMax[0], lut.domainMax[1], lut.domainMax[2]);
        f << b;
        for (size_t i = 0; i + 2 < lut.table.size(); i += 3)
        {
            std::snprintf(b, sizeof b, "%.6f %.6f %.6f\n", lut.table[i], lut.table[i + 1], lut.table[i + 2]);
            f << b;
        }
        if (!f) { err = "writing " + path + " failed"; return false; }
        return true;
    }
}
}
}
