// MinGW in strict `-std=c++NN` mode does not accept the single-underscore `_stdcall`
// spelling, and ORT's header uses it for every function-pointer member on Windows. The
// build uses `-std=c++17`, not `gnu++17`, so this has to be said before the include or the
// header is a wall of "expected ')' before '*'". MSVC and every non-Windows target are
// unaffected.
#if defined(_WIN32) && !defined(_MSC_VER) && !defined(_stdcall)
#define _stdcall __stdcall
#endif
#include "OnnxSegmenter.h"
#include "../../../core/ImageProcessing/src/base/ColorSpace.h"
#include "onnxruntime_c_api.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>

namespace arstro
{
namespace cosmo_v2
{
    namespace fs = std::filesystem;

    // ── the manifest ─────────────────────────────────────────────────────────────────
    bool SegModelSpec::handles(SemanticSubject s) const
    {
        if (output == Output::Alpha) return s == alphaSubject;
        for (const auto &kv : classes)
            if (kv.second == s) return true;
        return false;
    }

    namespace
    {
        std::string trim(const std::string &s)
        {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
            return s.substr(a, b - a);
        }
        std::string lower(std::string s)
        {
            for (char &c : s) c = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
            return s;
        }
    }

    bool parseSegModelSpec(const std::string &text, const std::string &dir, SegModelSpec &out,
                           std::string &err)
    {
        SegModelSpec sp;
        std::string model;
        std::istringstream in(text);
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line))
        {
            ++lineNo;
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                err = "line " + std::to_string(lineNo) + ": expected key=value";
                return false;
            }
            const std::string k = lower(trim(line.substr(0, eq)));
            const std::string v = trim(line.substr(eq + 1));

            // A manifest is written by a human, so an unknown key is REPORTED. Silently
            // ignoring one turns "I misspelled inputWidth" into "the model does nothing and
            // I cannot see why", which is the failure this whole file is trying to avoid.
            if (k == "name") sp.name = v;
            else if (k == "model") model = v;
            else if (k == "license" || k == "licence") sp.licence = v;
            else if (k == "source") sp.source = v;
            else if (k == "inputwidth") sp.inputWidth = std::atoi(v.c_str());
            else if (k == "inputheight") sp.inputHeight = std::atoi(v.c_str());
            else if (k == "inputsize") { sp.inputWidth = sp.inputHeight = std::atoi(v.c_str()); }
            else if (k == "input") sp.inputTensor = v;
            else if (k == "outputtensor") sp.outputTensor = v;
            else if (k == "layout")
            {
                const std::string lv = lower(v);
                if (lv == "nchw") sp.layout = SegModelSpec::Layout::NCHW;
                else if (lv == "nhwc") sp.layout = SegModelSpec::Layout::NHWC;
                else { err = "line " + std::to_string(lineNo) + ": layout must be nchw or nhwc"; return false; }
            }
            else if (k == "range")
            {
                const std::string lv = lower(v);
                if (lv == "0..1" || lv == "unit") sp.range = SegModelSpec::Range::Unit;
                else if (lv == "-1..1" || lv == "signed") sp.range = SegModelSpec::Range::Signed;
                else if (lv == "imagenet") sp.range = SegModelSpec::Range::ImageNet;
                else { err = "line " + std::to_string(lineNo) + ": range must be 0..1, -1..1 or imagenet"; return false; }
            }
            else if (k == "output")
            {
                const std::string lv = lower(v);
                if (lv == "alpha") sp.output = SegModelSpec::Output::Alpha;
                else if (lv == "classes") sp.output = SegModelSpec::Output::Classes;
                else { err = "line " + std::to_string(lineNo) + ": output must be alpha or classes"; return false; }
            }
            else if (k == "activation")
            {
                const std::string lv = lower(v);
                if (lv == "none") sp.activation = SegModelSpec::Activation::None;
                else if (lv == "sigmoid") sp.activation = SegModelSpec::Activation::Sigmoid;
                else if (lv == "softmax") sp.activation = SegModelSpec::Activation::Softmax;
                else { err = "line " + std::to_string(lineNo) + ": activation must be none, sigmoid or softmax"; return false; }
            }
            else if (k == "subject")
            {
                SemanticSubject s{};
                if (!parseSemanticSubject(v, s))
                { err = "line " + std::to_string(lineNo) + ": unknown subject " + v; return false; }
                sp.alphaSubject = s;
            }
            else if (k.rfind("class.", 0) == 0)
            {
                const std::string idx = k.substr(6);
                if (idx.empty() || idx.find_first_not_of("0123456789") != std::string::npos)
                { err = "line " + std::to_string(lineNo) + ": class.<n> needs a number"; return false; }
                SemanticSubject s{};
                if (!parseSemanticSubject(v, s))
                { err = "line " + std::to_string(lineNo) + ": unknown subject " + v; return false; }
                sp.classes[std::atoi(idx.c_str())] = s;
            }
            else
            {
                err = "line " + std::to_string(lineNo) + ": unknown key " + k;
                return false;
            }
        }

        if (model.empty()) { err = "no model= in the manifest"; return false; }
        if (sp.inputWidth <= 0 || sp.inputHeight <= 0)
        { err = "inputWidth/inputHeight (or inputSize) must be set"; return false; }
        if (sp.output == SegModelSpec::Output::Classes && sp.classes.empty())
        { err = "an output=classes model needs at least one class.<n>=<subject>"; return false; }

        const fs::path mp(model);
        sp.modelPath = mp.is_absolute() ? model : (fs::path(dir) / mp).string();
        out = std::move(sp);
        return true;
    }

    // ── the runtime ──────────────────────────────────────────────────────────────────
    namespace
    {
        using GetApiBaseFn = const OrtApiBase *(ORT_API_CALL *)(void);

        struct DynLib
        {
            void *handle = nullptr;
            bool open(const std::string &path)
            {
#ifdef _WIN32
                handle = (void *)LoadLibraryA(path.c_str());
#else
                handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
                return handle != nullptr;
            }
            void *sym(const char *n) const
            {
                if (!handle) return nullptr;
#ifdef _WIN32
                return (void *)GetProcAddress((HMODULE)handle, n);
#else
                return dlsym(handle, n);
#endif
            }
            // Deliberately never closed. A session holds thread pools created inside the
            // library; unloading it while the render worker may still be inside a Run is a
            // crash for no benefit, since the process is about to exit anyway.
        };

        /** The names the runtime goes by, most specific first. The hint is a directory the
         *  caller expects it in (next to the model), so a user who unzipped both into the
         *  config directory does not also have to edit PATH. */
        std::vector<std::string> runtimeCandidates(const std::string &hintDir)
        {
            std::vector<std::string> v;
#ifdef _WIN32
            const char *names[] = {"onnxruntime.dll"};
#elif defined(__APPLE__)
            const char *names[] = {"libonnxruntime.dylib"};
#else
            const char *names[] = {"libonnxruntime.so", "libonnxruntime.so.1"};
#endif
            for (const char *n : names)
            {
                if (!hintDir.empty()) v.push_back((fs::path(hintDir) / n).string());
                v.push_back(n);   // then the ordinary search path
            }
            return v;
        }
    }

    struct OnnxSegmenter::Impl
    {
        DynLib lib;
        const OrtApi *api = nullptr;
        OrtEnv *env = nullptr;
        OrtSessionOptions *opts = nullptr;
        OrtSession *session = nullptr;
        OrtMemoryInfo *mem = nullptr;
        std::string inName, outName;

        ~Impl()
        {
            if (!api) return;
            if (mem) api->ReleaseMemoryInfo(mem);
            if (session) api->ReleaseSession(session);
            if (opts) api->ReleaseSessionOptions(opts);
            if (env) api->ReleaseEnv(env);
        }
        /** ORT reports failures as an OrtStatus that the CALLER owns. Forgetting to release
         *  one leaks on exactly the paths that are least exercised, so every call goes
         *  through here. */
        bool ok(OrtStatus *st, const char *what, std::string &why) const
        {
            if (!st) return true;
            why = std::string(what) + ": " + api->GetErrorMessage(st);
            api->ReleaseStatus(st);
            return false;
        }
    };

    OnnxSegmenter::~OnnxSegmenter() = default;

    std::string OnnxSegmenter::findManifest(const std::string &dir)
    {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return {};
        std::vector<std::string> found;
        for (const auto &e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".cosmoseg")
                found.push_back(e.path().string());
        // Sorted, so "whichever the filesystem happened to hand back first" is not part of
        // the behaviour — two manifests must pick the same one on every machine.
        std::sort(found.begin(), found.end());
        return found.empty() ? std::string() : found.front();
    }

    std::unique_ptr<OnnxSegmenter> OnnxSegmenter::open(const std::string &manifestPath,
                                                       const std::string &libraryHint,
                                                       std::string &why)
    {
        std::ifstream f(manifestPath, std::ios::binary);
        if (!f) { why = "cannot read " + manifestPath; return nullptr; }
        std::stringstream ss;
        ss << f.rdbuf();

        SegModelSpec spec;
        if (!parseSegModelSpec(ss.str(), fs::path(manifestPath).parent_path().string(), spec, why))
            return nullptr;
        std::error_code ec;
        if (!fs::exists(spec.modelPath, ec))
        { why = "the manifest names a model that is not there: " + spec.modelPath; return nullptr; }

        auto impl = std::unique_ptr<Impl>(new Impl());
        for (const auto &cand : runtimeCandidates(libraryHint))
            if (impl->lib.open(cand)) break;
        if (!impl->lib.handle)
        { why = "the ONNX Runtime shared library is not installed (see docs/segmentation-models.md)"; return nullptr; }

        auto getBase = (GetApiBaseFn)impl->lib.sym("OrtGetApiBase");
        if (!getBase) { why = "the library found is not ONNX Runtime (no OrtGetApiBase)"; return nullptr; }
        const OrtApiBase *base = getBase();
        impl->api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!impl->api)
        {
            // The negotiation working as designed: an older runtime cannot supply the struct
            // this was compiled against, and says so instead of handing back a shorter one.
            why = std::string("ONNX Runtime ") + (base ? base->GetVersionString() : "?") +
                  " is older than the API cosmo was built against (" + std::to_string(ORT_API_VERSION) + ")";
            return nullptr;
        }
        const OrtApi *api = impl->api;

        if (!impl->ok(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "cosmo", &impl->env), "CreateEnv", why))
            return nullptr;
        if (!impl->ok(api->CreateSessionOptions(&impl->opts), "CreateSessionOptions", why))
            return nullptr;
        // One thread. The render worker is already one of a budgeted set (R-CPU/R-SVC-10), and a
        // model helping itself to every core behind the budget's back is exactly the duplication
        // D-11 was about.
        api->SetIntraOpNumThreads(impl->opts, 1);
        api->SetSessionGraphOptimizationLevel(impl->opts, ORT_ENABLE_ALL);

#ifdef _WIN32
        const std::wstring wpath = fs::path(spec.modelPath).wstring();
        OrtStatus *st = api->CreateSession(impl->env, wpath.c_str(), impl->opts, &impl->session);
#else
        OrtStatus *st = api->CreateSession(impl->env, spec.modelPath.c_str(), impl->opts, &impl->session);
#endif
        if (!impl->ok(st, "CreateSession", why)) return nullptr;

        OrtAllocator *alloc = nullptr;
        if (!impl->ok(api->GetAllocatorWithDefaultOptions(&alloc), "GetAllocator", why)) return nullptr;
        char *nm = nullptr;
        if (spec.inputTensor.empty())
        {
            if (!impl->ok(api->SessionGetInputName(impl->session, 0, alloc, &nm), "SessionGetInputName", why))
                return nullptr;
            impl->inName = nm; api->AllocatorFree(alloc, nm);
        }
        else impl->inName = spec.inputTensor;
        if (spec.outputTensor.empty())
        {
            if (!impl->ok(api->SessionGetOutputName(impl->session, 0, alloc, &nm), "SessionGetOutputName", why))
                return nullptr;
            impl->outName = nm; api->AllocatorFree(alloc, nm);
        }
        else impl->outName = spec.outputTensor;

        if (!impl->ok(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &impl->mem),
                      "CreateCpuMemoryInfo", why))
            return nullptr;

        std::unique_ptr<OnnxSegmenter> seg(new OnnxSegmenter());
        seg->mImpl = std::move(impl);
        seg->mSpec = std::move(spec);
        seg->mDisplayName = seg->mSpec.name;
        why.clear();
        return seg;
    }

    namespace
    {
        /** Area-average the framed image down to the model's input size, in LINEAR light —
         *  the only place averaging pixels is physically meaningful, and the reason the engine
         *  works in linear light at all. Bilinear would alias badly going from 1600 px to 256. */
        void areaResize(const Image &img, int dw, int dh, std::vector<float> &rgb)
        {
            const int w = img.width(), h = img.height(), ch = img.channels();
            rgb.assign((std::size_t)dw * dh * 3, 0.f);
            for (int y = 0; y < dh; ++y)
            {
                const int sy0 = (int)((int64_t)y * h / dh), sy1 = std::max(sy0 + 1, (int)((int64_t)(y + 1) * h / dh));
                for (int x = 0; x < dw; ++x)
                {
                    const int sx0 = (int)((int64_t)x * w / dw), sx1 = std::max(sx0 + 1, (int)((int64_t)(x + 1) * w / dw));
                    double acc[3] = {0, 0, 0};
                    int n = 0;
                    for (int sy = sy0; sy < sy1 && sy < h; ++sy)
                        for (int sx = sx0; sx < sx1 && sx < w; ++sx)
                        {
                            const Pixel *p = img.data() + ((std::size_t)sy * w + sx) * ch;
                            acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
                            ++n;
                        }
                    const double inv = n ? 1.0 / n : 0.0;
                    float *d = rgb.data() + ((std::size_t)y * dw + x) * 3;
                    for (int c = 0; c < 3; ++c) d[c] = (float)(acc[c] * inv);
                }
            }
        }
    }

    bool OnnxSegmenter::segment(const Image &img, SemanticSubject subject, float sensitivity,
                                std::vector<Pixel> &out)
    {
        // Declining is the contract, not a failure: a person-matting model must never claim to
        // have found the sky, or the built-in would stop being consulted for the subjects it is
        // actually good at.
        if (!mImpl || !mImpl->session || !mSpec.handles(subject)) return false;
        const int w = img.width(), h = img.height();
        if (w <= 0 || h <= 0 || img.channels() < 3) return false;

        const int mw = mSpec.inputWidth, mh = mSpec.inputHeight;
        std::vector<float> rgb;
        areaResize(img, mw, mh, rgb);

        // Display-referred, then normalised as the model was trained. Same reasoning as
        // R-AISEG-3: every published preprocessing recipe is stated in gamma-encoded terms.
        static const float kMean[3] = {0.485f, 0.456f, 0.406f};
        static const float kStd[3] = {0.229f, 0.224f, 0.225f};
        std::vector<float> tensor((std::size_t)mw * mh * 3);
        for (std::size_t i = 0; i < (std::size_t)mw * mh; ++i)
            for (int c = 0; c < 3; ++c)
            {
                float v = (float)color::srgbEncode((Pixel)rgb[i * 3 + c]);
                switch (mSpec.range)
                {
                case SegModelSpec::Range::Unit: break;
                case SegModelSpec::Range::Signed: v = v * 2.f - 1.f; break;
                case SegModelSpec::Range::ImageNet: v = (v - kMean[c]) / kStd[c]; break;
                }
                if (mSpec.layout == SegModelSpec::Layout::NCHW)
                    tensor[(std::size_t)c * mw * mh + i] = v;
                else
                    tensor[i * 3 + (std::size_t)c] = v;
            }

        const OrtApi *api = mImpl->api;
        int64_t dims[4];
        if (mSpec.layout == SegModelSpec::Layout::NCHW) { dims[0] = 1; dims[1] = 3; dims[2] = mh; dims[3] = mw; }
        else                                            { dims[0] = 1; dims[1] = mh; dims[2] = mw; dims[3] = 3; }

        OrtValue *input = nullptr;
        if (api->CreateTensorWithDataAsOrtValue(mImpl->mem, tensor.data(),
                                                tensor.size() * sizeof(float), dims, 4,
                                                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input) != nullptr)
            return false;

        const char *inNames[1] = {mImpl->inName.c_str()};
        const char *outNames[1] = {mImpl->outName.c_str()};
        OrtValue *output = nullptr;
        OrtStatus *st = api->Run(mImpl->session, nullptr, inNames, (const OrtValue *const *)&input, 1,
                                 outNames, 1, &output);
        api->ReleaseValue(input);
        if (st) { api->ReleaseStatus(st); return false; }
        if (!output) return false;

        // What came back, and how many channels of it.
        OrtTensorTypeAndShapeInfo *info = nullptr;
        if (api->GetTensorTypeAndShape(output, &info) != nullptr) { api->ReleaseValue(output); return false; }
        std::size_t nd = 0;
        api->GetDimensionsCount(info, &nd);
        std::vector<int64_t> odim(nd ? nd : 1, 1);
        if (nd) api->GetDimensions(info, odim.data(), nd);
        api->ReleaseTensorTypeAndShapeInfo(info);

        float *raw = nullptr;
        if (api->GetTensorMutableData(output, (void **)&raw) != nullptr || !raw)
        { api->ReleaseValue(output); return false; }

        // Channel count and plane size, from the declared layout rather than guessed: a 1x1xHxW
        // and a 1xHxWx1 hold the same numbers in a different order, and guessing wrong is a
        // transposed mask that looks like a bad model.
        int channels = 1, ow = mw, oh = mh;
        if (nd == 4)
        {
            if (mSpec.layout == SegModelSpec::Layout::NCHW)
            { channels = (int)odim[1]; oh = (int)odim[2]; ow = (int)odim[3]; }
            else
            { oh = (int)odim[1]; ow = (int)odim[2]; channels = (int)odim[3]; }
        }
        else if (nd == 3) { oh = (int)odim[1]; ow = (int)odim[2]; }
        if (ow <= 0 || oh <= 0 || channels <= 0) { api->ReleaseValue(output); return false; }

        const std::size_t plane = (std::size_t)ow * oh;
        auto value = [&](int c, std::size_t i) -> float {
            return mSpec.layout == SegModelSpec::Layout::NCHW ? raw[(std::size_t)c * plane + i]
                                                              : raw[i * channels + c];
        };

        std::vector<Pixel> score(plane, (Pixel)0);
        if (mSpec.output == SegModelSpec::Output::Alpha)
        {
            // One channel of "is it", or two where the model emits background/foreground.
            const int fg = channels >= 2 ? 1 : 0;
            for (std::size_t i = 0; i < plane; ++i)
            {
                float v = value(fg, i);
                if (mSpec.activation == SegModelSpec::Activation::Sigmoid)
                    v = 1.f / (1.f + std::exp(-v));
                else if (mSpec.activation == SegModelSpec::Activation::Softmax && channels >= 2)
                {
                    const float a = value(0, i), b = value(1, i), m = std::max(a, b);
                    const float ea = std::exp(a - m), eb = std::exp(b - m);
                    v = eb / (ea + eb);
                }
                score[i] = (Pixel)std::min(1.f, std::max(0.f, v));
            }
        }
        else
        {
            // Class channels: the mapped classes' share of the total. Softmax is done per pixel
            // over the class axis when the model emits logits — at the MODEL's resolution, which
            // is why the analysis size matters as much here as it does for the built-in.
            std::vector<int> want;
            for (const auto &kv : mSpec.classes)
                if (kv.second == subject && kv.first < channels) want.push_back(kv.first);
            if (want.empty()) { api->ReleaseValue(output); return false; }
            for (std::size_t i = 0; i < plane; ++i)
            {
                float sum = 0.f, mine = 0.f;
                if (mSpec.activation == SegModelSpec::Activation::Softmax)
                {
                    float m = value(0, i);
                    for (int c = 1; c < channels; ++c) m = std::max(m, value(c, i));
                    for (int c = 0; c < channels; ++c) sum += std::exp(value(c, i) - m);
                    for (int c : want) mine += std::exp(value(c, i) - m);
                    score[i] = (Pixel)(sum > 0.f ? mine / sum : 0.f);
                }
                else
                {
                    for (int c : want) mine += value(c, i);
                    score[i] = (Pixel)std::min(1.f, std::max(0.f, mine));
                }
            }
        }
        api->ReleaseValue(output);

        // The SAME threshold the built-in uses, at the model's own resolution, then resampled to
        // the render's. Sensitivity is a promise to the photographer and it may not mean one
        // thing with a model installed and another without.
        segment::scoreToCoverage(score, ow, oh, sensitivity);
        segment::resamplePlane(score, ow, oh, out, w, h);
        return true;
    }
}
}
