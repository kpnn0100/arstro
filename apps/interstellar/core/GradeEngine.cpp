#include "GradeEngine.h"
#include "engine/EditParamsIO.h"
#include <cstring>

namespace arstro
{
namespace interstellar
{
    GradeEngine::GradeEngine() : mEngine(new arstro::EditEngine()) {}
    GradeEngine::~GradeEngine() = default;

    bool GradeEngine::isIdentity(const arstro::EditParams &p)
    {
        static const std::string kDefault = arstro::serializeParams(arstro::EditParams{});
        return arstro::serializeParams(p) == kDefault;
    }

    bool GradeEngine::render(const Raster &in, const arstro::EditParams &params, bool hasParams,
                             int longEdge, Raster &out)
    {
        if (in.empty()) return false;

        if (!hasParams || isIdentity(params))
        {
            // Identity: hand the decoded bytes through. The engine would convert 1080p to linear
            // float (24.8 MB), drop all 17 stages, and convert back — to produce the input.
            out = in;
            return true;
        }

        // The engine as a pure function: nothing survives between frames, because a video frame is
        // different pixels every time and a slot per frame would leak one per frame.
        mEngine->clearImages();
        const int slot = mEngine->addImage(in.rgba.data(), in.width, in.height, 4);
        if (slot < 0) { out = in; return true; }
        mEngine->selectImage(slot);
        mEngine->applyParams(params);

        arstro::PreviewBuffer buf;
        if (longEdge > 0)
        {
            mEngine->setPreviewSize(longEdge);
            mEngine->setPreviewLevel(0);
            buf = mEngine->renderPreview();
        }
        else buf = mEngine->renderFull();

        if (!buf.rgba || buf.width <= 0 || buf.height <= 0) { out = in; return true; }
        out.width = buf.width;
        out.height = buf.height;
        out.rgba.assign(buf.rgba, buf.rgba + (size_t)buf.width * buf.height * 4);
        return true;
    }
}
}
