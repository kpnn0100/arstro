#include "InstrumentEditor.h"
#include "EmbeddedFonts.h"
#include "RegistryModel.h"
#include "../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    InstrumentEditor::InstrumentEditor(const DeviceType &type, ParamAccess &access) : mType(type), mAccess(access)
    {
        // the suite's typeface is IN the binary (design rule §2.3) and the accent is Solaris's teal (R-UI-2):
        // both process-wide, both before a widget is built
        static std::once_flag fonts;
        std::call_once(fonts, [] { cosmo_v2::registerEmbeddedFonts(); });
        installSolarisAccent();
        mRoot = std::make_shared<Segment>();
        mPanel = std::make_shared<DevicePanel>(kDevice);
        mPanel->songControls = false;
        mPanel->onCommand = [this](const std::string &line) { return command(line); };
        mRoot->addChild(mPanel);
        if (!type.noteNames.empty())
        {
            // a kit: its pads over the panel; a pad picked shows the parameters that shape it
            mPads = std::make_shared<PadGrid>(type, access);
            mPads->onPick = [this](const std::string &prefix) { mPanel->revealGroup(prefix); };
            mRoot->addChild(mPads);
        }
        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
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

    // ── a kit's pads ────────────────────────────────────────────────────────────────────────

    PadGrid::PadGrid(const DeviceType &type, ParamAccess &access) : mAccess(access)
    {
        clipToBounds = true;
        for (size_t i = 0; i < type.noteNames.size(); ++i)
        {
            Pad p;
            p.note = type.noteNames[i].first;
            p.name = type.noteNames[i].second;
            for (const auto &np : type.notePrefixes)
                if (np.first == p.note) p.prefix = np.second; // the registry joins a key to its parameters (REQ-device-9)
            mPads.push_back(std::move(p));
        }
    }

    double PadGrid::contentHeight() const
    {
        const int rows = ((int)mPads.size() + kColumns - 1) / kColumns;
        return rows <= 0 ? 0.0 : 2.0 * kPad + rows * kPadH + (rows - 1) * kGap;
    }

    Rect PadGrid::padRect(int i) const
    {
        const double w = (width.value() - 2.0 * kPad - (kColumns - 1) * kGap) / kColumns;
        const int r = i / kColumns, c = i % kColumns;
        return Rect{kPad + c * (w + kGap), kPad + r * (kPadH + kGap), std::max(0.0, w), kPadH};
    }

    int PadGrid::padAt(const Point &p) const
    {
        for (int i = 0; i < padCount(); ++i)
            if (padRect(i).contains(p)) return i;
        return -1;
    }

    void PadGrid::advance(double nowMs)
    {
        mNowMs = nowMs;
        for (int i = 0; i < padCount(); ++i)
        {
            Pad &p = mPads[(size_t)i];
            const bool want = i == mPicked;
            if (want != p.pickLast)
            {
                p.pick.animateTo(want ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
                p.pickLast = want;
            }
            if (p.hit)
            {
                // a hit: lit at once (R2, a response this frame), then dying away like the sound
                p.flash.set(1.0);
                p.flash.animateTo(0.0, 320.0, Easing::EaseOutCubic, nowMs);
                p.hit = false;
            }
            p.pick.update(nowMs);
            p.flash.update(nowMs);
        }
        Segment::advance(nowMs);
    }

    bool PadGrid::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            const int i = padAt(local);
            if (i < 0) return true;
            mHeld = i;
            mPads[(size_t)i].hit = true;
            mAccess.play(mPads[(size_t)i].note, 100);
            if (mPicked != i)
            {
                mPicked = i;
                if (onPick && !mPads[(size_t)i].prefix.empty()) onPick(mPads[(size_t)i].prefix);
            }
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            if (mHeld >= 0) mAccess.play(mPads[(size_t)mHeld].note, 0);
            mHeld = -1;
            return true;
        default:
            return true; // the grid is a surface
        }
    }

    void PadGrid::onPaint(IRenderTarget &t) const
    {
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, height.value() - 0.5); t.lineTo(width.value(), height.value() - 0.5); t.strokePath();
        for (int i = 0; i < padCount(); ++i)
        {
            const Pad &p = mPads[(size_t)i];
            const Rect r = padRect(i);
            drawRoundedRect(t, r, radius::control(), Paint::filled(palette::secondary()));
            if (p.flash.value() > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::primaryAlpha(0.45 * p.flash.value())));
            if (p.pick.value() > 0.001) drawRoundedRect(t, r, radius::control(), Paint::stroked(palette::primaryAlpha(p.pick.value()), 1.5));
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, p.name, r.w - 12.0, 11.0, font::sansMedium()), r.x + 6.0, r.y + 16.0, 11.0, font::sansMedium());
            t.setFill(palette::mutedForeground());
            t.drawText(std::to_string(p.note), r.x + 6.0, r.bottom() - 7.0, 9.0, font::mono());
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
        mRoot->width.set(mW);
        mRoot->height.set(mH);
        if (mPads)
        {
            // the pads in the panel's band: under its title, over its rows
            mPads->x.set(0.0);
            mPads->y.set(DevicePanel::kHeaderH);
            mPads->width.set(mW);
            mPads->height.set(mPads->contentHeight());
            mPanel->band = mPads->contentHeight();
        }
        mPanel->x.set(0.0);
        mPanel->y.set(0.0);
        mPanel->width.set(mW);
        mPanel->height.set(mH);
        mPanel->layout();
        mRoot->advance(nowMs);
        mPanel->layout(); // rows built this frame are placed this frame
        mRoot->render(t);
        mRoot->renderOverlay(t);
    }
}
}
