/*
 *  cosmo_v2 by arstro — OnnxSegmenter: a REAL segmentation model behind `ISegmenter`
 *  (R-AISEG-6, R-AISEG-15). Host layer, deliberately.
 *
 *  ── Why this is not in the engine ──
 *
 *  `arstro_image` opens no files, carries no assets and links no runtime, so it can
 *  stay portable to WASM and Android. A neural network is a file plus a runtime. That
 *  is exactly the split `ISegmenter` was added for: the engine states the question, the
 *  host answers it if it can, and the built-in statistical classifier answers when it
 *  cannot. Nothing in this file is reachable from `arstro_image`.
 *
 *  ── Why the runtime is loaded, not linked ──
 *
 *  ONNX Runtime is fetched by the user, not vendored: it is 12 MB of shared library per
 *  platform and its licence is not cosmo's to redistribute by default. So this loads it
 *  with `LoadLibrary`/`dlopen` and reaches it through the ONE exported C symbol,
 *  `OrtGetApiBase`, which hands back a struct of function pointers. That is the same
 *  trick `OmpPin` uses for OpenMP and for the same reason: **cosmo links nothing, so
 *  cosmo still builds and still runs when the library is absent.** A missing runtime is
 *  not a build error, not a crash, and not a broken feature — it is `segment()`
 *  returning false, and the built-in answering.
 *
 *  The C API is version-negotiated: `GetApi(ORT_API_VERSION)` returns a struct laid out
 *  for the version this was compiled against, or null if the runtime is older. Only the
 *  header is vendored (`third_party/onnxruntime_c_api.h`, MIT), because the struct's
 *  field ORDER is the ABI — a hand-written subset would compile and then call the wrong
 *  function pointer, silently.
 *
 *  ── Why there is a manifest ──
 *
 *  There is no one model. What a photo editor wants is sky, skin, hair, foliage, water
 *  and people, and no single permissively-licensed ONNX model covers that set (see
 *  `docs/segmentation-models.md`). So the model is described by a small text file beside
 *  it — input size, layout, normalisation, what the output means, and which output class
 *  maps to which subject — and cosmo ships none. Anyone can drop in a model they are
 *  entitled to use, and cosmo does not have to know its name.
 *
 *  A model that cannot answer for a subject **declines** rather than guessing, so a
 *  person-matting model never claims to have found the sky.
 */
#pragma once
#include "../../../core/ImageProcessing/src/analysis/Segmenter.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    /** What one model's manifest says. Line-based `key=value`, `#` comments, no escaping —
     *  the same shape as every other text format cosmo owns (settings.txt, .apf, .cmp). */
    struct SegModelSpec
    {
        enum class Layout { NCHW, NHWC };
        enum class Range { Unit, Signed, ImageNet };   // 0..1 · -1..1 · (v-mean)/std
        enum class Output { Alpha, Classes };          // one channel of "is it" · C class channels
        enum class Activation { None, Sigmoid, Softmax };

        std::string name = "model";
        std::string modelPath;                  // resolved beside the manifest
        std::string licence, source;            // reported, never enforced — see the docs
        int inputWidth = 0, inputHeight = 0;
        Layout layout = Layout::NCHW;
        Range range = Range::Unit;
        Output output = Output::Alpha;
        Activation activation = Activation::None;
        std::string inputTensor, outputTensor;  // empty = the model's first
        /** Alpha models answer for exactly one subject. */
        SemanticSubject alphaSubject = SemanticSubject::Person;
        /** Class models map output channel -> subject. A channel nobody maps is background. */
        std::map<int, SemanticSubject> classes;

        bool handles(SemanticSubject s) const;
    };

    /** Parse a manifest. `dir` is where the file lives, so `model=` can be relative to it.
     *  Returns false with a reason in `err` — a manifest a human wrote by hand is a place
     *  where a typo must be reported, not defaulted away. */
    bool parseSegModelSpec(const std::string &text, const std::string &dir, SegModelSpec &out,
                           std::string &err);

    /** The segmenter itself. Construct through `open`, which returns null and a reason
     *  rather than a half-built object: "no model installed" and "the model is broken" are
     *  different sentences and the log should say which. */
    class OnnxSegmenter : public arstro::ISegmenter
    {
    public:
        ~OnnxSegmenter() override;

        /** Load the manifest at `manifestPath` and open its model.
         *  `libraryHint` is tried before the system search path, so a runtime dropped next to
         *  the model is found without the user editing PATH. */
        static std::unique_ptr<OnnxSegmenter> open(const std::string &manifestPath,
                                                   const std::string &libraryHint,
                                                   std::string &why);

        /** The first `*.cosmoseg` manifest in `dir`, or empty. One place to look, named in the
         *  docs and written by `tools/fetch-segmentation-model.sh`. */
        static std::string findManifest(const std::string &dir);

        bool segment(const Image &img, SemanticSubject subject, float sensitivity,
                     std::vector<Pixel> &out) override;
        const char *name() const override { return mDisplayName.c_str(); }

        const SegModelSpec &spec() const { return mSpec; }

    private:
        OnnxSegmenter() = default;
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        SegModelSpec mSpec;
        std::string mDisplayName;
    };
}
}
