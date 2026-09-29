#include "ComputeBackend.h"
#include "GlComputeBackend.h"
#include "GlesComputeBackend.h"

namespace arstro
{
    SelectingComputeBackend::SelectingComputeBackend(std::vector<std::unique_ptr<IComputeBackend>> candidates)
        : mCandidates(std::move(candidates))
    {
    }

    // call_once, not a plain flag: RenderService's worker and the UI thread both ask.
    IComputeBackend *SelectingComputeBackend::chosen() const
    {
        std::call_once(mOnce, [this]
        {
            for (const auto &c : mCandidates)
                if (c && c->available()) { mChosen = c.get(); return; }
        });
        return mChosen;
    }

    // Before anything is available the first candidate still names the intent, so a
    // diagnostics line reads "OpenGL" rather than an empty string.
    const char *SelectingComputeBackend::name() const
    {
        if (IComputeBackend *c = chosen()) return c->name();
        return mCandidates.empty() || !mCandidates.front() ? "none" : mCandidates.front()->name();
    }

    IComputeBackend::Kind SelectingComputeBackend::kind() const
    {
        if (IComputeBackend *c = chosen()) return c->kind();
        return Kind::Gpu;
    }

    bool SelectingComputeBackend::process(const Image &linearSource, const EditParams &params, ComputeResult &out)
    {
        IComputeBackend *c = chosen();
        return c && c->process(linearSource, params, out);
    }

    // The ONE place a concrete backend is registered — keeping GPU support a
    // per-platform add-on that touches neither the engine nor the UI (Open/Closed).
    // Every backend this build compiled is a candidate, in preference order: desktop GL
    // 4.3 first (the richer API, and what Mesa/AMD/Intel/NVIDIA and WGL offer), then GL
    // ES 3.1 (Android, and ARM Linux boards whose vendor driver speaks only GLES, e.g.
    // Mali-G610 under libmali). Which one runs is decided at run time (R-GPU-7).
    std::unique_ptr<IComputeBackend> createComputeAccelerator()
    {
        std::vector<std::unique_ptr<IComputeBackend>> c;
#if defined(ARSTRO_GL_COMPUTE)
        c.push_back(createGlComputeAccelerator());
#endif
#if defined(ARSTRO_GLES_COMPUTE)
        c.push_back(createGlesComputeAccelerator());
#endif
        if (c.empty()) return nullptr;
        if (c.size() == 1) return std::move(c.front());
        return std::unique_ptr<IComputeBackend>(new SelectingComputeBackend(std::move(c)));
    }
}
