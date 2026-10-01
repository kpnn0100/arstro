#include "PngWriter.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

namespace arstro
{
namespace interstellar_host
{
    bool writePng(const std::string &path, const interstellar::Raster &frame, std::string &err)
    {
        if (frame.empty()) { err = "nothing to write: the frame is empty"; return false; }
        GdkPixbuf *pb = gdk_pixbuf_new_from_data(frame.rgba.data(), GDK_COLORSPACE_RGB, TRUE, 8, frame.width,
                                                 frame.height, frame.width * 4, nullptr, nullptr);
        if (!pb) { err = "cannot wrap the frame for PNG"; return false; }
        GError *e = nullptr;
        const gboolean ok = gdk_pixbuf_save(pb, path.c_str(), "png", &e, nullptr);
        g_object_unref(pb);
        if (!ok)
        {
            err = "cannot write " + path + (e ? std::string(": ") + e->message : std::string());
            if (e) g_error_free(e);
            return false;
        }
        return true;
    }
}
}
