#include "InstrumentEditor.h"
#include "EmbeddedFonts.h"
#include "RegistryModel.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;

    InstrumentEditor::InstrumentEditor(const DeviceType &type, ParamAccess &access) : mType(type), mAccess(access)
    {
        // the suite's typeface is IN the binary (design rule §2.3) and the accent is Solaris's teal (R-UI-2):
        // both process-wide, both before a widget is built
        static std::once_flag fonts;
        std::call_once(fonts, [] { cosmo_v2::registerEmbeddedFonts(); });
        installSolarisAccent();
        mPanel = std::make_shared<DevicePanel>(kDevice);
        mPanel->songControls = false;
        mPanel->onCommand = [this](const std::string &line) { return command(line); };
        mRecognizer.setSink([this](const Gesture &g) { mPanel->onGesture(g); });
        refresh();
    }

    void InstrumentEditor::setSize(double w, double h)
    {
        mW = std::max(kMinWidth, w);
        mH = std::max(kMinHeight, h);
    }

    void InstrumentEditor::refresh()
    {
        // the host's values → a model of one device; a new revision only when something moved
        std::vector<double> v(mType.params.size());
        for (size_t i = 0; i < v.size(); ++i) v[i] = mAccess.value((int)i);
        if (mBound && v == mShown) return;
        for (size_t i = 0; i < v.size() && mBound; ++i)
            if (v[i] != mShown[i]) mLast = mType.params[i].name; // whoever changed it, it is lit (R-WIN-2)
        mShown = v;
        solaris::DeviceModel d = solaris::deviceModelOf(mType, v);
        d.id = kDevice;
        d.lastChanged = mLast;
        solaris::StripModel s;
        s.id = "ch_1";
        s.name = mType.label;
        s.kind = "instrument";
        s.devices = {d};
        mModel.strips = {s};
        mModel.revision++;
        mPanel->bind(mModel, mDown);
        mBound = true;
    }

    bool InstrumentEditor::command(const std::string &line)
    {
        // `set dv_1.<name>=<value>` — the only line the panel sends with the song's controls off
        const std::string head = std::string("set ") + kDevice + ".";
        if (line.rfind(head, 0) != 0) return false;
        const auto eq = line.find('=', head.size());
        if (eq == std::string::npos) return false;
        const std::string name = line.substr(head.size(), eq - head.size()), text = line.substr(eq + 1);
        for (size_t i = 0; i < mType.params.size(); ++i)
        {
            if (mType.params[i].name != name) continue;
            double v = 0;
            if (!paramFromText(mType.params[i], text, v)) return false;
            const int k = (int)i;
            if (mDown)
            {
                // a press: begun once, performed per step (a step that moved nothing says nothing), ended at release
                if (mBegun.insert(k).second) mAccess.begin(k);
                const auto was = mSent.find(k);
                if (was != mSent.end() && was->second == v) return true;
                mSent[k] = v;
                mAccess.perform(k, v);
            }
            else
            {
                mAccess.begin(k);
                mAccess.perform(k, v);
                mAccess.end(k);
            }
            mLast = name;
            return true;
        }
        return false;
    }

    void InstrumentEditor::pointer(int kind, double x, double y, int button, double timeMs, bool shift, bool ctrl)
    {
        if (kind == 0) mDown = true;
        RawPointer rp{};
        rp.kind = kind == 0 ? RawPointer::Kind::Down : (kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move);
        rp.pos = Point{x, y};
        rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
        rp.timeMs = timeMs;
        rp.shift = shift;
        rp.ctrl = ctrl;
        mRecognizer.feed(rp);
        if (kind == 2)
        {
            // the click a release makes (a choice, a reset) has landed inside the press: now every edit ends
            for (int k : mBegun) mAccess.end(k);
            mBegun.clear();
            mSent.clear();
            mDown = false;
        }
    }

    void InstrumentEditor::wheel(double x, double y, double notches, double timeMs)
    {
        RawPointer rp{};
        rp.kind = RawPointer::Kind::Scroll;
        rp.pos = Point{x, y};
        rp.timeMs = timeMs;
        rp.scroll = Point{0.0, -notches * 40.0}; // Solaris's notch (App::wheel)
        mRecognizer.feed(rp);
    }

    void InstrumentEditor::render(IRenderTarget &t, double nowMs)
    {
        refresh();
        mRecognizer.advance(nowMs);
        drawRoundedRect(t, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::background()));
        mPanel->x.set(0.0);
        mPanel->y.set(0.0);
        mPanel->width.set(mW);
        mPanel->height.set(mH);
        mPanel->layout();
        mPanel->advance(nowMs);
        mPanel->layout(); // rows built this frame are placed this frame
        mPanel->render(t);
        mPanel->renderOverlay(t);
    }
}
}
