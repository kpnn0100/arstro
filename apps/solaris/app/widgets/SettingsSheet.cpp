#include "SettingsSheet.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include "KeyCombo.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    namespace glyph = interstellar_v1::glyph;

    namespace
    {
        constexpr double kFontPx = 11.0;
        constexpr double kFolderRowH = 26.0;
        constexpr double kRailItemH = 30.0;
        constexpr double kHeaderH = 60.0;    // the card's title band
        constexpr double kFooterH = 56.0;    // Done, under a hairline
        constexpr double kPageTop = 34.0;    // a page's title, then its first row
        constexpr int kDoneId = 900, kAddId = 901, kResetId = 902, kRailBase = 950, kRemoveBase = 1000, kKeyChipBase = 2000, kKeyAddBase = 4000,
                      kMapRemoveBase = 6000; // R-CTRL-1
        constexpr double kKeyRowGap = 4.0;   // R-SET-4: the keymap's rows sit close — one action a line
        constexpr double kRowLabelGap = 13.0;
        constexpr double kAreaGap = 22.0;
        const char *const kListening = "Press a key\xE2\x80\xA6";
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        const char *kLabels[SettingsSheet::kRows] = {"Output", "Input", "Sample rate", "Buffer", "Metronome", "Click level",
                                                     "Preview level", "Tempo", "Meter", "Reduced motion", "Autosave", "Driver", "MIDI input", "Record quantize", "Count-in"};
        const char *kKeys[SettingsSheet::kRows] = {"output", "input", "sampleRate", "bufferSize", "metronome", "metronomeLevel",
                                                   "auditionLevel", "newBpm", "newSig", "reducedMotion", "autosave", "driver", "midiInputs", "recordQuantize", "countIn"};
        // the sections (R-SET-3, R-SET-5) and the rows each page holds
        const char *kTitles[SettingsSheet::kSections] = {"Audio", "Playback", "New songs", "Sample folders", "Interface", "Keyboard", "MIDI"};
        const std::vector<int> kPageRows[SettingsSheet::kSections] = {
            {SettingsSheet::kOutput, SettingsSheet::kInput, SettingsSheet::kRate, SettingsSheet::kBuffer, SettingsSheet::kDriver, SettingsSheet::kMidiIn},
            {SettingsSheet::kMetronome, SettingsSheet::kClickLevel, SettingsSheet::kPreviewLevel, SettingsSheet::kRecQuant, SettingsSheet::kCountIn},
            {SettingsSheet::kNewBpm, SettingsSheet::kNewSig},
            {},
            {SettingsSheet::kMotion, SettingsSheet::kAutosave},
            {},
            {}, // R-CTRL-1: the MIDI controls mapped (paintMidi)
        };
        bool isToggle(int row) { return row == SettingsSheet::kMetronome || row == SettingsSheet::kMotion; }
        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%g", v);
            return b;
        }
        std::string upper(std::string s)
        {
            for (auto &ch : s) ch = (char)std::toupper((unsigned char)ch);
            return s;
        }
        const int kRates[] = {44100, 48000, 88200, 96000};
        const int kBuffers[] = {64, 128, 256, 512, 1024, 2048};
        // a path too wide for its row keeps its END — the folder's own name is the part that identifies it
        std::string tailFit(const IRenderTarget &t, const std::string &s, double maxW, double px, const char *fam)
        {
            if (t.measureText(s, px, fam) <= maxW) return s;
            for (size_t cut = 1; cut < s.size(); ++cut)
            {
                if ((s[cut] & 0xC0) == 0x80) continue;
                const std::string tail = "\xE2\x80\xA6" + s.substr(cut);
                if (t.measureText(tail, px, fam) <= maxW) return tail;
            }
            return "\xE2\x80\xA6";
        }
        // R-SET-6: Artboard's combo in cosmo's tokens, as Genesis dresses it — the list is the library's
        ComboStyle comboStyle()
        {
            ComboStyle s;
            s.field.paint = Paint::filledStroked(palette::input(), palette::border(), 1.0);
            s.field.cornerRadius = radius::control();
            s.popup.paint = Paint::filledStroked(palette::popover(), palette::border(), 1.0);
            s.popup.cornerRadius = radius::control();
            s.rowSelected.paint = Paint::filled(palette::primaryAlpha(0.22));
            s.rowSelected.cornerRadius = radius::hairline();
            s.text.color = palette::foreground();
            s.text.sizePx = kFontPx;
            s.text.fontFamily = font::sans();
            s.caretColor = palette::mutedForeground();
            return s;
        }
    }

    // ── the controls (R-SET-6) ──────────────────────────────────────────────────────────────────

    SettingCombo::SettingCombo() : artboard::ComboBox(comboStyle())
    {
        rowHeight = 26.0;
        maxPopupHeight = 26.0 * 8;
    }

    void SettingCombo::advance(double nowMs)
    {
        const std::string now = options().empty() ? std::string() : options()[(size_t)selectedIndex()];
        if (!mPlaced) { mTo = now; mFade.set(1.0); mPlaced = true; } // first placement: at rest
        else if (now != mTo)
        {
            mFrom = mTo;
            mTo = now;
            mFade.set(0.0);
            mFade.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
        }
        mFade.update(nowMs);
        ComboBox::advance(nowMs);
    }

    void SettingCombo::close()
    {
        if (!isOpen()) return;
        Gesture g;
        g.type = Gesture::Type::Click;
        handleGesture(g, Point{1.0, 1.0}); // on the field: the library closes its list, eased
    }

    void SettingCombo::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value(), hv = hoverAmount();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::input(), lerpColor(palette::border(), palette::primary(), 0.6 * hv), 1.0));
        const double f = mFade.value(), by = textfit::baseline(h * 0.5, kFontPx), room = w - 10.0 - 26.0;
        if (f < 0.999 && !mFrom.empty())
        {
            t.setFill(fade(palette::foreground(), 1.0 - f));
            t.drawText(textfit::ellipsize(t, mFrom, room, kFontPx, font::sans()), 10.0, by, kFontPx, font::sans());
        }
        t.setFill(fade(palette::foreground(), f));
        t.drawText(textfit::ellipsize(t, mTo, room, kFontPx, font::sans()), 10.0, by, kFontPx, font::sans());
        glyph::caretDown(t, Rect{w - 22.0, (h - 10.0) * 0.5, 12.0, 10.0}, lerpColor(palette::mutedForeground(), palette::foreground(), hv), 1.3);
    }

    SettingToggle::SettingToggle()
    {
        width.set(40.0);
        height.set(22.0);
    }

    void SettingToggle::advance(double nowMs)
    {
        const double want = on() ? 1.0 : 0.0;
        if (!mPlaced) { mKnob.set(want); mLast = want; mPlaced = true; }
        else if (want != mLast) { mKnob.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mLast = want; }
        mKnob.update(nowMs);
        ToggleSwitch::advance(nowMs);
    }

    void SettingToggle::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value(), k = mKnob.value(), hv = hoverAmount();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::pill(),
                        Paint::filledStroked(lerpColor(palette::switchBackground(), palette::primary(), k),
                                             lerpColor(palette::border(), palette::primary(), k), 1.0));
        if (hv > 0.001) drawRoundedRect(t, Rect{0, 0, w, h}, radius::pill(), Paint::filled(palette::hoverWash(hv)));
        const double d = h - 6.0;
        drawRoundedRect(t, Rect{3.0 + k * (w - 6.0 - d), 3.0, d, d}, radius::pill(), Paint::filled(palette::white()));
    }

    // ── a page: clipped, cross-faded, its own scroll; the sheet paints it ────────────────────────

    class SettingsSheet::Page : public Segment
    {
    public:
        Page(const SettingsSheet &sheet, int s) : mSheet(sheet), mSection(s)
        {
            clipToBounds = true;
            inputTransparent = true; // its controls take input; a click on what the sheet draws goes to the sheet
        }
        interstellar_v1::EasedScroll scroll;
        mutable double contentH = 0.0;      // measured in the paint
        AnimatedProperty fadeAmt{0.0};
        double fadeLast = -1.0;

    protected:
        void onPaint(IRenderTarget &t) const override { mSheet.paintPage(t, mSection, *this); }

    private:
        const SettingsSheet &mSheet;
        int mSection;
    };

    // ── the sheet ─────────────────────────────────────────────────────────────────────────────────

    const char *SettingsSheet::sectionTitle(int s) { return s >= 0 && s < kSections ? kTitles[s] : ""; }

    int SettingsSheet::sectionOf(int row)
    {
        for (int s = 0; s < kSections; ++s)
            for (int r : kPageRows[s])
                if (r == row) return s;
        return -1;
    }

    SettingsSheet::SettingsSheet()
    {
        // fixed choices; the devices come from the model
        for (int r : kRates)
        {
            char b[16];
            std::snprintf(b, sizeof b, "%g kHz", r / 1000.0);
            mChips[kRate].push_back(Chip{b, std::to_string(r)});
        }
        for (int b : kBuffers) mChips[kBuffer].push_back(Chip{std::to_string(b) + " samples", std::to_string(b)});
        for (int d : {-18, -12, -6, 0}) mChips[kClickLevel].push_back(Chip{std::to_string(d) + " dB", std::to_string(d)});
        for (int d : {-18, -12, -6, 0}) mChips[kPreviewLevel].push_back(Chip{std::to_string(d) + " dB", std::to_string(d)});
        for (int b : {100, 120, 124, 126, 128, 140, 150, 174}) mChips[kNewBpm].push_back(Chip{std::to_string(b) + " bpm", std::to_string(b)});
        for (const char *m : {"4/4", "3/4", "6/8", "7/8"}) mChips[kNewSig].push_back(Chip{m, m});
        mChips[kDriver] = {Chip{"PulseAudio", "pulse"}, Chip{"PipeWire \xE2\x80\x94 low latency", "pipewire"}}; // R-DEV-9
        mChips[kMidiIn] = {Chip{"Every MIDI input", "all"}, Chip{"None", "none"}};                         // R-MIDI-2
        mChips[kRecQuant] = {Chip{"Off", "off"}, Chip{"1/4", "1/4"}, Chip{"1/8", "1/8"}, Chip{"1/16", "1/16"}, Chip{"1/32", "1/32"}};
        mChips[kCountIn] = {Chip{"None", "0"}, Chip{"1 bar", "1"}, Chip{"2 bars", "2"}, Chip{"4 bars", "4"}};
        mChips[kAutosave] = {Chip{"Off", "off"}, Chip{"Every 30 s", "30"}, Chip{"Every minute", "60"}, Chip{"Every 2 minutes", "120"}, Chip{"Every 5 minutes", "300"}};
        mChips[kOutput] = mChips[kInput] = {Chip{"System default", ""}};
        for (int s = 0; s < kSections; ++s)
        {
            auto page = std::make_shared<Page>(*this, s);
            page->opacity.set(0.0);
            addChild(page);
            mPages.push_back(page);
            for (int r : kPageRows[s])
            {
                if (isToggle(r))
                {
                    auto tg = std::make_shared<SettingToggle>();
                    tg->onChange = [this, r](bool on) {
                        if (onCommand) onCommand(std::string("settings set ") + kKeys[r] + "=" + (on ? "on" : "off"));
                    };
                    page->addChild(tg);
                    mToggles[r] = tg;
                    continue;
                }
                auto cb = std::make_shared<SettingCombo>();
                cb->width.set(kComboW);
                cb->height.set(kControlH);
                std::vector<std::string> labels;
                for (const auto &c : mChips[r]) labels.push_back(c.label);
                cb->setOptions(labels);
                cb->onChange = [this, r](int i) {
                    if (i >= 0 && i < (int)mChips[r].size() && onCommand)
                        onCommand(std::string("settings set ") + q(std::string(kKeys[r]) + "=" + mChips[r][(size_t)i].value));
                };
                page->addChild(cb);
                mCombos[r] = cb;
            }
        }
    }

    void SettingsSheet::bind(const solaris::AppModel &m)
    {
        const auto &s = m.settings;
        mOutput = s.output;
        mInput = s.input;
        mRate = s.sampleRate;
        mBuffer = s.bufferSize;
        mLatencyMs = s.latencyMs;
        mFolders = s.folders;
        mSettings = s;
        // R-CTRL-1: the mapping rows, keyed by control — a forgotten one stays to shrink out, a new one is appended
        for (auto &r : mMapRows)
        {
            r.leaving = true;
            for (const auto &x : s.mappings)
                if (x.cc == r.m.cc) { r.m = x; r.leaving = false; }
        }
        for (const auto &x : s.mappings)
        {
            bool have = false;
            for (const auto &r : mMapRows) have |= r.m.cc == x.cc && !r.leaving;
            if (!have)
            {
                MapRow r;
                r.m = x;
                mMapRows.push_back(std::move(r));
            }
        }
        // a combo shows the value the setting HAS — one its list lacks (a device not connected now, a tempo set from
        // a shell) is an entry of its own, so the setting is never invisible (R-SET-6)
        for (int r = 0; r < kRows; ++r)
        {
            if (isToggle(r))
            {
                if (mToggles[r]) mToggles[r]->setOn(current(r) == "on"); // the knob eases there (SettingToggle)
                continue;
            }
            std::vector<Chip> chips;
            if (r == kOutput || r == kInput)
            {
                chips.push_back(Chip{"System default", ""});
                const std::string dir = r == kOutput ? "out" : "in";
                for (const auto &d : m.devices)
                    if (d.dir == dir) chips.push_back(Chip{d.name, d.id});
            }
            else if (r == kMidiIn)
            {
                // R-MIDI-2: every input, none, or one keyboard — the ports as the machine lists them
                chips = {Chip{"Every MIDI input", "all"}, Chip{"None", "none"}};
                for (const auto &d : m.midiDevices) chips.push_back(Chip{d.name, d.id});
            }
            else
                for (const auto &c : mChips[r])
                    if (c.label.find(" (set elsewhere)") == std::string::npos) chips.push_back(c); // the fixed list, without an old extra
            const std::string cur = current(r);
            int sel = -1;
            for (size_t i = 0; i < chips.size(); ++i)
                if (chips[i].value == cur) sel = (int)i;
            if (sel < 0)
            {
                std::string label = cur;
                if (r == kOutput || r == kInput) label += " (not connected)";
                else if (r == kNewBpm) label += " bpm (set elsewhere)";
                else if (r == kClickLevel || r == kPreviewLevel) label += " dB (set elsewhere)";
                else label += " (set elsewhere)";
                chips.push_back(Chip{label, cur});
                sel = (int)chips.size() - 1;
            }
            mChips[r] = chips;
            std::vector<std::string> labels;
            for (const auto &c : chips) labels.push_back(c.label);
            if (labels != mCombos[r]->options()) mCombos[r]->setOptions(labels);
            mCombos[r]->setSelectedIndex(sel);
        }
        // R-SET-4: the keymap's chips, keyed by combo — a key that arrives grows in, one that leaves shrinks where it was
        bool same = mKeyRows.size() == s.keys.size();
        for (size_t a = 0; same && a < s.keys.size(); ++a) same = mKeyRows[a].id == s.keys[a].id;
        if (!same)
        {
            mKeyRows.clear();
            for (const auto &k : s.keys) mKeyRows.push_back(KeyRowLive{k.id, {}, AnimatedProperty{0.0}, false, -1.0});
        }
        for (size_t a = 0; a < s.keys.size(); ++a)
        {
            auto &row = mKeyRows[a];
            std::vector<bool> used(row.chips.size(), false);
            std::vector<KeyChipLive> next;
            for (const auto &c : s.keys[a].keys)
            {
                size_t j = 0;
                while (j < row.chips.size() && (used[j] || row.chips[j].combo != c)) ++j;
                if (j < row.chips.size()) { used[j] = true; next.push_back(row.chips[j]); next.back().gone = false; }
                else { KeyChipLive n; n.combo = c; next.push_back(n); }
            }
            for (size_t j = 0; j < row.chips.size(); ++j)
                if (!used[j])
                {
                    KeyChipLive g = row.chips[j];
                    g.gone = true;
                    next.insert(next.begin() + (long)std::min(j, next.size()), g);
                }
            row.chips = std::move(next);
        }
    }

    std::string SettingsSheet::current(int row) const
    {
        switch (row)
        {
        case kOutput: return mOutput;
        case kInput: return mInput;
        case kRate: return std::to_string(mRate);
        case kBuffer: return std::to_string(mBuffer);
        case kMetronome: return mSettings.metronome ? "on" : "off";
        case kClickLevel: return num(mSettings.metronomeLevel);
        case kPreviewLevel: return num(mSettings.auditionLevel);
        case kNewBpm: return num(mSettings.newBpm);
        case kNewSig: return mSettings.newSig;
        case kMotion: return mSettings.reducedMotion ? "on" : "off";
        case kAutosave: return mSettings.autosave > 0 ? std::to_string(mSettings.autosave) : std::string("off");
        case kDriver: return mSettings.driver;
        case kMidiIn: return mSettings.midiInputs;
        case kRecQuant: return mSettings.recordQuantize;
        case kCountIn: return std::to_string(mSettings.countIn);
        default: return std::string();
        }
    }

    // ── geometry ──────────────────────────────────────────────────────────────────────────────────

    Rect SettingsSheet::cardRect() const
    {
        const double W = width.value(), H = height.value();
        const double w = std::min(kCardW, W - 32.0), h = std::min(kCardH, H - 48.0);
        return Rect{std::floor((W - w) * 0.5), std::floor((H - h) * 0.5), w, h};
    }

    Rect SettingsSheet::pageRect() const
    {
        const Rect c = cardRect();
        const double x = c.x + 12.0 + kRailW + 24.0;
        return Rect{x, c.y + kHeaderH, std::max(0.0, c.right() - kPad - x), std::max(0.0, c.bottom() - kFooterH - 8.0 - (c.y + kHeaderH))};
    }

    Rect SettingsSheet::railRect(int s) const
    {
        const Rect c = cardRect();
        return Rect{c.x + 12.0, c.y + kHeaderH + s * (kRailItemH + 2.0), kRailW, kRailItemH};
    }

    double SettingsSheet::pageAmount(int s) const { return s >= 0 && s < kSections ? mPages[(size_t)s]->opacity.value() : 0.0; }

    Rect SettingsSheet::controlRect(int row) const
    {
        if (row < 0 || row >= kRows) return Rect{};
        const Segment *c = mCombos[row] ? (const Segment *)mCombos[row].get() : (const Segment *)mToggles[row].get();
        if (!c) return Rect{};
        const Point p = c->worldTransform().apply(Point{0.0, 0.0});
        return Rect{p.x, p.y, c->width.value(), c->height.value()};
    }

    int SettingsSheet::optionCount(int row) const { return row >= 0 && row < kRows && mCombos[row] ? (int)mCombos[row]->options().size() : 0; }

    std::string SettingsSheet::optionLabel(int row, int i) const
    {
        return i >= 0 && i < optionCount(row) ? mCombos[row]->options()[(size_t)i] : std::string();
    }

    Rect SettingsSheet::optionRect(int row, int i) const
    {
        if (i < 0 || i >= optionCount(row) || !mCombos[row]->isOpen()) return Rect{};
        const auto &cb = *mCombos[row];
        const auto pop = cb.popup();
        const Rect f = controlRect(row);
        return Rect{f.x, f.y + pop.top + i * cb.rowHeight - cb.popupScroll(), f.w, cb.rowHeight};
    }

    bool SettingsSheet::comboOpen(int row) const { return row >= 0 && row < kRows && mCombos[row] && mCombos[row]->isOpen(); }
    double SettingsSheet::labelAmount(int row) const { return row >= 0 && row < kRows && mCombos[row] ? mCombos[row]->labelAmount() : 0.0; }
    std::string SettingsSheet::shownValue(int row) const { return row >= 0 && row < kRows && mCombos[row] ? mCombos[row]->shown() : std::string(); }
    double SettingsSheet::toggleAmount(int row) const { return row >= 0 && row < kRows && mToggles[row] ? mToggles[row]->amount() : 0.0; }

    void SettingsSheet::revealRect(const Rect &r)
    {
        auto &page = *mPages[(size_t)mSection];
        page.scroll.reveal(r.y - (page.y.value() - page.scroll.value()), r.h + 12.0);
    }

    void SettingsSheet::closeCombos()
    {
        for (auto &c : mCombos)
            if (c) c->close();
    }

    bool SettingsSheet::anyComboOpen() const
    {
        for (const auto &c : mCombos)
            if (c && c->isOpen()) return true;
        return false;
    }

    // ── the keymap's helpers (R-SET-4) ────────────────────────────────────────────────────────────

    const solaris::KeyActionModel *SettingsSheet::keyModel(const std::string &id) const
    {
        for (const auto &k : mSettings.keys)
            if (k.id == id) return &k;
        return nullptr;
    }

    void SettingsSheet::listen(const std::string &action, int slot)
    {
        mListenAction = action;
        mListenSlot = slot;
        mListen[slotKey(action, slot)]; // its look eases in from nothing
    }

    double SettingsSheet::listenAmount(const std::string &action, int slot) const
    {
        const auto it = mListen.find(slotKey(action, slot));
        return it == mListen.end() ? 0.0 : it->second.p.value();
    }

    double SettingsSheet::keyChipAmount(const std::string &action, const std::string &combo) const
    {
        for (const auto &row : mKeyRows)
            if (row.id == action)
                for (const auto &c : row.chips)
                    if (c.combo == combo) return c.amt.value();
        return 0.0;
    }

    Rect SettingsSheet::keyChipRect(const std::string &action, int i) const
    {
        const auto it = mKeyChipRects.find(action);
        return it != mKeyChipRects.end() && i >= 0 && i < (int)it->second.size() ? it->second[(size_t)i] : Rect{};
    }

    Rect SettingsSheet::keyAddRect(const std::string &action) const
    {
        const auto it = mKeyAddRects.find(action);
        return it == mKeyAddRects.end() ? Rect{} : it->second;
    }

    // ── time ──────────────────────────────────────────────────────────────────────────────────────

    void SettingsSheet::show() { mShowWanted = true; mCloseWanted = false; stopListening(); }
    void SettingsSheet::close() { mCloseWanted = true; mShowWanted = false; stopListening(); closeCombos(); }

    void SettingsSheet::showSection(int s)
    {
        if (s < 0 || s >= kSections || s == mSection) return;
        closeCombos();
        stopListening();
        mSection = s; // advance cross-fades the pages and slides the rail's highlight
    }

    void SettingsSheet::layoutPages()
    {
        const Rect pr = pageRect();
        for (int s = 0; s < kSections; ++s)
        {
            auto &page = *mPages[(size_t)s];
            page.x.set(pr.x);
            page.y.set(pr.y);
            page.width.set(pr.w);
            page.height.set(pr.h);
            page.scroll.setExtent(0.0, pr.h, page.contentH);
            const double oy = -page.scroll.value();
            int i = 0;
            for (int r : kPageRows[s])
            {
                const double top = kPageTop + i * kRowH;
                Segment *c = mCombos[r] ? (Segment *)mCombos[r].get() : (Segment *)mToggles[r].get();
                c->x.set(pr.w - 8.0 - c->width.value());
                c->y.set(oy + top + (kRowH - c->height.value()) * 0.5);
                // a control scrolled out of the page takes no input (clipping does not cull hit-testing)
                c->visible = page.scroll.bandVisible(top + (kRowH - c->height.value()) * 0.5, c->height.value());
                ++i;
            }
        }
    }

    void SettingsSheet::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (mShowWanted)
        {
            mOpen = true;
            mClosing = false;
            mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mShowWanted = false;
            for (auto &p : mPages) p->scroll.reset();
        }
        if (mCloseWanted && mOpen && !mClosing)
        {
            mClosing = true;
            mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mCloseWanted = false;
        }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        opacity.set(mOpen ? mAppear.value() : 0.0); // the whole sheet fades as one group (FR-32)

        // R-SET-5: the chosen page fades in as the last one fades out; the rail's highlight slides to it
        const double railY = railRect(mSection).y;
        if (!mSectionPlaced)
        {
            for (int s = 0; s < kSections; ++s)
            {
                mPages[(size_t)s]->fadeAmt.set(s == mSection ? 1.0 : 0.0);
                mPages[(size_t)s]->fadeLast = s == mSection ? 1.0 : 0.0;
            }
            mRailY.set(railY);
            mRailLast = railY;
            mSectionPlaced = true;
        }
        for (int s = 0; s < kSections; ++s)
        {
            auto &p = *mPages[(size_t)s];
            const double want = s == mSection ? 1.0 : 0.0;
            if (want != p.fadeLast) { p.fadeAmt.animateTo(want, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); p.fadeLast = want; }
            p.fadeAmt.update(nowMs);
            p.opacity.set(p.fadeAmt.value());
            p.scroll.advance(nowMs);
        }
        if (railY != mRailLast) { mRailY.animateTo(railY, motion::kSlideMs, Easing::EaseOutCubic, nowMs); mRailLast = railY; } // a resize moves it too
        mRailY.update(nowMs);

        // R-SET-4: chips grow in and shrink out; a changed action's dot, the listening look and Reset all ease
        bool anyChanged = false;
        for (auto &row : mKeyRows)
        {
            const auto *km = keyModel(row.id);
            const double want = km && km->keys != km->defaults ? 1.0 : 0.0;
            anyChanged = anyChanged || want > 0.0;
            if (!row.placed) { row.changed.set(want); row.last = want; row.placed = true; }
            else if (want != row.last) { row.changed.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); row.last = want; }
            row.changed.update(nowMs);
            for (auto &c : row.chips)
            {
                const double w = c.gone ? 0.0 : 1.0;
                if (!c.placed) { c.amt.set(mKeysEver ? 0.0 : 1.0); c.last = c.amt.value(); c.placed = true; } // first placement: at rest
                if (w != c.last) { c.amt.animateTo(w, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); c.last = w; }
                c.amt.update(nowMs);
            }
            row.chips.erase(std::remove_if(row.chips.begin(), row.chips.end(),
                                           [](const KeyChipLive &c) { return c.gone && c.amt.value() <= 0.0 && !c.amt.isAnimating(); }),
                            row.chips.end());
        }
        mKeysEver = mKeysEver || !mKeyRows.empty();
        {
            const double want = anyChanged ? 1.0 : 0.0;
            if (!mResetPlaced) { mResetLive.p.set(want); mResetLive.last = want; mResetPlaced = true; }
            else if (want != mResetLive.last) { mResetLive.p.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mResetLive.last = want; }
            mResetLive.p.update(nowMs);
        }
        const std::string on = mListenAction.empty() ? std::string() : slotKey(mListenAction, mListenSlot);
        for (auto it = mListen.begin(); it != mListen.end();)
        {
            auto &l = it->second;
            const double want = it->first == on ? 1.0 : 0.0;
            if (want != l.last) { l.p.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.last = want; }
            l.p.update(nowMs);
            if (want == 0.0 && l.p.value() <= 0.0 && !l.p.isAnimating()) it = mListen.erase(it);
            else ++it;
        }
        mHover.advance(nowMs);
        // R-CTRL-1: the mapping rows grow in and shrink out; the first placement is set
        for (auto &r : mMapRows)
        {
            if (r.fresh)
            {
                if (mMapPlaced) r.amount.animateTo(1.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
                else r.amount.set(1.0);
                r.fresh = false;
            }
            if (r.leaving && !r.amount.isAnimating() && r.amount.value() > 0.0) r.amount.animateTo(0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
            r.amount.update(nowMs);
        }
        mMapRows.erase(std::remove_if(mMapRows.begin(), mMapRows.end(), [](const MapRow &r) { return r.leaving && r.amount.value() <= 0.0; }), mMapRows.end());
        mMapPlaced = true;
        layoutPages();
        Segment::advance(nowMs);
    }

    double SettingsSheet::mapRowAmount(const std::string &cc) const
    {
        for (const auto &r : mMapRows)
            if (r.m.cc == cc) return r.amount.value();
        return 0.0;
    }

    // ── input ─────────────────────────────────────────────────────────────────────────────────────

    int SettingsSheet::hoverId(const Point &p) const
    {
        if (mDone.contains(p)) return kDoneId;
        for (int s = 0; s < kSections; ++s)
            if (railRect(s).contains(p)) return kRailBase + s;
        if (mSection == kFolders)
        {
            if (mAddFolder.contains(p)) return kAddId;
            for (size_t i = 0; i < mRemove.size(); ++i)
                if (mRemove[i].contains(p)) return kRemoveBase + (int)i;
        }
        if (mSection == kMidi)
            for (size_t i = 0; i < mMapRemove.size(); ++i)
                if (mMapRemove[i].contains(p)) return kMapRemoveBase + (int)i;
        if (mSection == kKeyboard)
        {
            if (mKeyReset.contains(p)) return kResetId;
            for (size_t a = 0; a < mKeyRows.size(); ++a)
            {
                if (keyAddRect(mKeyRows[a].id).contains(p)) return kKeyAddBase + (int)a;
                const auto it = mKeyChipRects.find(mKeyRows[a].id);
                if (it != mKeyChipRects.end())
                    for (size_t i = 0; i < it->second.size(); ++i)
                        if (it->second[i].contains(p)) return kKeyChipBase + (int)a * 16 + (int)i;
            }
        }
        return -1;
    }

    bool SettingsSheet::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(hoverId(local));
            return true;
        case Gesture::Type::Scroll:
            mPages[(size_t)mSection]->scroll.scrollBy(g.delta.y);
            return true;
        case Gesture::Type::Click:
        case Gesture::Type::DoubleClick:
        {
            const bool twice = g.type == Gesture::Type::DoubleClick; // only a key chip takes the second press (it listens still)
            closeCombos(); // a click off an open list lets it go, as anywhere
            if (!twice && !cardRect().contains(local)) { close(); return true; }   // the scrim closes, as cosmo's does
            if (!twice && mDone.contains(local)) { close(); return true; }
            for (int s = 0; s < kSections; ++s)
                if (!twice && railRect(s).contains(local)) { showSection(s); return true; }
            if (mSection == kKeyboard)
            {
                // R-SET-4: a key chip listens for the next key, "+" for one more — clicked again, it is still listening
                for (const auto &k : mSettings.keys)
                {
                    const auto it = mKeyChipRects.find(k.id);
                    int slot = -1;
                    if (it != mKeyChipRects.end())
                        for (size_t i = 0; i < it->second.size() && i < k.keys.size(); ++i)
                            if (it->second[i].contains(local)) slot = (int)i;
                    if (slot < 0 && keyAddRect(k.id).contains(local)) slot = (int)k.keys.size();
                    if (slot < 0) continue;
                    listen(k.id, slot);
                    return true;
                }
            }
            if (twice) return true;
            stopListening(); // a click anywhere else lets it go
            if (mSection == kKeyboard && mKeyReset.contains(local))
            {
                std::string line;
                for (const auto &k : mSettings.keys)
                    if (k.keys != k.defaults) line += " key." + k.id + "=default";
                if (!line.empty() && onCommand) onCommand("settings set" + line); // ONE line, every changed action
                return true;
            }
            if (mSection == kMidi) // R-CTRL-1: a mapping forgotten — one line
                for (size_t i = 0; i < mMapRemove.size() && i < mMapRemoveCc.size(); ++i)
                    if (mMapRemove[i].contains(local)) { if (onCommand) onCommand("map remove " + mMapRemoveCc[i]); return true; }
            if (mSection == kFolders)
            {
                if (mAddFolder.contains(local)) { if (onAddFolder) onAddFolder(); return true; }
                for (size_t i = 0; i < mRemove.size() && i < mFolders.size(); ++i)
                    if (mRemove[i].contains(local)) { if (onCommand) onCommand("folder remove " + q(mFolders[i])); return true; }
            }
            return true;
        }
        default:
            return true; // a modal swallows the rest
        }
    }

    bool SettingsSheet::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (!mListenAction.empty())
        {
            // R-SET-4: listening — the next key is the slot's combo; Escape cancels, Backspace removes it
            if (e.type != KeyEvent::Type::Down) return true;
            const auto *km = keyModel(mListenAction);
            if (!km) { stopListening(); return true; }
            const bool bare = !e.ctrl && !e.alt && !e.shift;
            std::vector<std::string> keys = km->keys;
            const size_t slot = (size_t)std::max(0, mListenSlot);
            if (bare && e.keyCode == 27) { stopListening(); return true; }
            if (bare && e.keyCode == 8)
            {
                if (slot >= keys.size()) { stopListening(); return true; } // nothing there to remove
                keys.erase(keys.begin() + (long)slot);
            }
            else
            {
                const std::string combo = solaris::comboText(e.keyCode, e.ctrl, e.alt, e.shift);
                if (combo.empty()) return true; // a key no combo can name: still listening
                if (slot < keys.size()) keys[slot] = combo;
                else keys.push_back(combo);
            }
            const std::string id = mListenAction;
            stopListening();
            if (onCommand) onCommand("settings set " + q("key." + id + "=" + solaris::combosText(keys)));
            return true;
        }
        if (e.type == KeyEvent::Type::Down && e.keyCode == 27 && anyComboOpen()) { closeCombos(); return true; } // Escape: the list first
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 27 || e.keyCode == 13)) close();
        return true; // a modal owns the keyboard
    }

    // ── paint ─────────────────────────────────────────────────────────────────────────────────────

    void SettingsSheet::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::scrim(1.0))); // the group's opacity fades it
        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        t.setFill(palette::foreground());
        t.drawText("Settings", c.x + kPad, c.y + 34.0, 14.0, font::sansSemiBold());
        t.setFill(palette::mutedForeground());
        t.drawText("this machine \xC2\xB7 never a song's", c.x + kPad + t.measureText("Settings", 14.0, font::sansSemiBold()) + 10.0, c.y + 34.0, 10.0, font::sans());

        // the rail: a highlight that slides to the chosen section, each name easing to the foreground with it
        const Rect r0 = railRect(0);
        drawRoundedRect(t, Rect{r0.x, mRailY.value(), r0.w, r0.h}, radius::control(), Paint::filled(palette::primaryAlpha(0.16)));
        drawRoundedRect(t, Rect{r0.x, mRailY.value() + 7.0, 2.0, r0.h - 14.0}, radius::pill(), Paint::filled(palette::primary()));
        for (int s = 0; s < kSections; ++s)
        {
            const Rect r = railRect(s);
            const double hv = mHover.amount(kRailBase + s);
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const double lit = std::clamp(1.0 - std::fabs(mRailY.value() - r.y) / kRailItemH, 0.0, 1.0); // follows the highlight
            t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), std::max(lit, 0.5 * hv)));
            t.drawText(kTitles[s], r.x + 14.0, textfit::baseline(r.y + r.h * 0.5, 12.0), 12.0, font::sans());
        }
        const double sx = c.x + 12.0 + kRailW + 12.0;
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(sx, c.y + kHeaderH - 4.0); t.lineTo(sx, c.bottom() - kFooterH); t.strokePath();
        t.beginPath(); t.moveTo(c.x, c.bottom() - kFooterH); t.lineTo(c.right(), c.bottom() - kFooterH); t.strokePath();

        // ── Done (primary) ──
        mDone = Rect{c.right() - kPad - 88.0, c.bottom() - kFooterH + (kFooterH - 30.0) * 0.5, 88.0, 30.0};
        drawRoundedRect(t, mDone, radius::control(), Paint::filled(palette::primary()));
        const double dhv = mHover.amount(kDoneId);
        if (dhv > 0.001) drawRoundedRect(t, mDone, radius::control(), Paint::filled(palette::hoverWash(dhv)));
        t.setFill(palette::primaryForeground());
        t.drawText("Done", mDone.x + (mDone.w - t.measureText("Done", 12.0, font::sansMedium())) * 0.5, textfit::baseline(mDone.y + mDone.h * 0.5, 12.0), 12.0,
                   font::sansMedium());
    }

    void SettingsSheet::paintPage(IRenderTarget &t, int s, const Page &page) const
    {
        const double w = page.width.value() - 8.0, oy = -page.scroll.value(); // 8 px for the scroll bar
        const double px = page.x.value(), py = page.y.value();
        t.save();
        t.clipRect(0.0, 0.0, page.width.value(), page.height.value()); // clipToBounds clips the CHILDREN, not this paint
        t.setFill(palette::foreground());
        t.drawText(kTitles[s], 0.0, oy + 16.0, 14.0, font::sansSemiBold());
        if (s == kFolders) paintFolders(t, w, oy, px, py);
        else if (s == kKeyboard) paintKeyboard(t, w, oy, px, py);
        else if (s == kMidi) paintMidi(t, w, oy, px, py);
        else paintRows(t, s, w, oy);
        page.scroll.drawBar(t, page.width.value(), 1.0);
        t.restore();
    }

    void SettingsSheet::paintRows(IRenderTarget &t, int s, double w, double oy) const
    {
        // a setting: its name and a note on the left, its control on the right (the page lays the controls out)
        const auto &rows = kPageRows[s];
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const int r = rows[i];
            const double top = oy + kPageTop + (double)i * kRowH;
            std::string note;
            if (r == kOutput) note = "the clock \xE2\x80\x94 every other device follows it";
            if (r == kInput) note = "where a recording comes from";
            if (r == kRate) note = "new songs start here";
            if (r == kMetronome) note = "clicks while playing \xE2\x80\x94 never in a render";
            if (r == kClickLevel) note = "how loud the click is";
            if (r == kPreviewLevel) note = "a sample heard from the browser";
            if (r == kNewBpm) note = "a new song starts at this tempo";
            if (r == kNewSig) note = "\xE2\x80\xA6 and in this meter";
            if (r == kMotion) note = "every tween collapses \xE2\x80\x94 the system's own setting counts too";
            if (r == kAutosave) note = "an unsaved song is kept beside it \xE2\x80\x94 recovered if Solaris stops";
            if (r == kDriver) note = "PipeWire for small buffers and counted xruns \xE2\x80\x94 PulseAudio if it will not open";
            if (r == kMidiIn) note = "the keyboard an armed strip plays";
            if (r == kRecQuant) note = "where a recorded note's start lands";
            if (r == kCountIn) note = "the click before a recording, the song waiting";
            if (r == kBuffer)
            {
                char b[64];
                std::snprintf(b, sizeof b, "%.1f ms of latency", mLatencyMs);
                note = b;
            }
            const double room = w - (isToggle(r) ? 40.0 : kComboW) - 20.0;
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, kLabels[r], room, 12.0, font::sans()), 0.0, top + 23.0, 12.0, font::sans());
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, note, room, 10.0, font::sans()), 0.0, top + 39.0, 10.0, font::sans());
            if (i + 1 < rows.size())
            {
                t.setStroke(palette::border(), 1.0);
                t.beginPath(); t.moveTo(0.0, top + kRowH); t.lineTo(w, top + kRowH); t.strokePath();
            }
        }
        mPages[(size_t)s]->contentH = kPageTop + (double)rows.size() * kRowH + 8.0;
    }

    void SettingsSheet::paintMidi(IRenderTarget &t, double w, double oy, double px, double py) const
    {
        // R-CTRL-1: each MIDI control and the number it moves — one the open song lacks dimmed, saying so; × forgets it
        t.setFill(palette::mutedForeground());
        t.drawText("controls and what they move", t.measureText(kTitles[kMidi], 14.0, font::sansSemiBold()) + 10.0, oy + 16.0, 10.0, font::sans());
        mMapRemove.clear();
        mMapRemoveCc.clear();
        double fy = oy + kPageTop;
        if (mMapRows.empty())
        {
            t.setFill(fade(palette::mutedForeground(), 0.8));
            t.drawText(textfit::ellipsize(t, "None yet \xE2\x80\x94 right-click a number \xE2\x80\xBA Learn MIDI CC, and move a control.", w, kFontPx, font::sans()), 0.0,
                       textfit::baseline(fy + kFolderRowH * 0.5, kFontPx), kFontPx, font::sans());
            fy += kFolderRowH;
        }
        for (size_t i = 0; i < mMapRows.size(); ++i)
        {
            const MapRow &mr = mMapRows[i];
            const double a = mr.amount.value(), h = kFolderRowH * a;
            if (h <= 0.01) continue;
            const Rect row{0.0, fy, w, kFolderRowH};
            const Rect rm{row.right() - 22.0, row.y + (row.h - 18.0) * 0.5, 18.0, 18.0};
            t.save();
            t.clipRect(0.0, fy, w, h); // growing in, shrinking out: the row's height eased, its content revealed
            if (!mr.leaving)
            {
                mMapRemove.push_back(Rect{rm.x + px, rm.y + py, rm.w, rm.h});
                mMapRemoveCc.push_back(mr.m.cc);
            }
            const double hv = mr.leaving ? 0.0 : mHover.amount(kMapRemoveBase + (int)mMapRemove.size() - 1);
            if (i + 1 < mMapRows.size())
            {
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(row.x, fy + h); t.lineTo(row.right(), fy + h); t.strokePath();
            }
            const double base = textfit::baseline(row.y + row.h * 0.5, 10.0);
            t.setFill(fade(palette::foreground(), a));
            t.drawText("CC " + mr.m.cc, row.x, base, 10.0, font::mono());
            const double ax = row.x + 72.0; // u(22): "CC 16/127" fits
            t.setFill(fade(mr.m.inert ? palette::mutedForeground() : palette::foreground(), a));
            const std::string what = mr.m.address + (mr.m.inert ? "  \xC2\xB7 not in this song" : "");
            t.drawText(textfit::ellipsize(t, what, std::max(0.0, rm.x - 8.0 - ax), 10.0, font::mono()), ax, base, 10.0, font::mono());
            if (hv > 0.001) drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color xc = fade(lerpColor(palette::mutedForeground(), palette::destructive(), hv), a);
            glyph::line(t, rm.x + 5.5, rm.y + 5.5, rm.right() - 5.5, rm.bottom() - 5.5, xc, 1.3);
            glyph::line(t, rm.right() - 5.5, rm.y + 5.5, rm.x + 5.5, rm.bottom() - 5.5, xc, 1.3);
            t.restore();
            fy += h;
        }
        mPages[kMidi]->contentH = (fy + 12.0) - oy;
    }

    void SettingsSheet::paintFolders(IRenderTarget &t, double w, double oy, double px, double py) const
    {
        t.setFill(palette::mutedForeground());
        t.drawText("the browser lists these first", t.measureText(kTitles[kFolders], 14.0, font::sansSemiBold()) + 10.0, oy + 16.0, 10.0, font::sans());
        mRemove.clear();
        double fy = oy + kPageTop;
        if (mFolders.empty())
        {
            t.setFill(fade(palette::mutedForeground(), 0.8));
            t.drawText("None yet \xE2\x80\x94 add the folders you keep samples in.", 0.0, textfit::baseline(fy + kFolderRowH * 0.5, kFontPx), kFontPx, font::sans());
            fy += kFolderRowH;
        }
        for (size_t i = 0; i < mFolders.size(); ++i)
        {
            const Rect row{0.0, fy, w, kFolderRowH};
            const Rect rm{row.right() - 22.0, row.y + (row.h - 18.0) * 0.5, 18.0, 18.0};
            mRemove.push_back(Rect{rm.x + px, rm.y + py, rm.w, rm.h});
            const double hv = mHover.amount(kRemoveBase + (int)i);
            if (i + 1 < mFolders.size())
            {
                t.setStroke(palette::border(), 1.0);
                t.beginPath(); t.moveTo(row.x, row.bottom()); t.lineTo(row.right(), row.bottom()); t.strokePath();
            }
            t.setFill(palette::foreground());
            t.drawText(tailFit(t, mFolders[i], w - 34.0, 10.0, font::mono()), row.x, textfit::baseline(row.y + row.h * 0.5, 10.0), 10.0, font::mono());
            if (hv > 0.001) drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color xc = lerpColor(palette::mutedForeground(), palette::destructive(), hv);
            glyph::line(t, rm.x + 5.5, rm.y + 5.5, rm.right() - 5.5, rm.bottom() - 5.5, xc, 1.3);
            glyph::line(t, rm.right() - 5.5, rm.y + 5.5, rm.x + 5.5, rm.bottom() - 5.5, xc, 1.3);
            fy += kFolderRowH;
        }
        const std::string lbl = "Add folder\xE2\x80\xA6";
        const double bw = t.measureText(lbl, kFontPx, font::sans()) + 40.0;
        const Rect add{0.0, fy + 8.0, bw, kChipH};
        mAddFolder = Rect{add.x + px, add.y + py, add.w, add.h};
        const double hv = mHover.amount(kAddId);
        drawRoundedRect(t, add, radius::control(),
                        Paint::filledStroked(palette::primaryAlpha(0.10 * hv), lerpColor(palette::border(), palette::primary(), 0.5 + 0.5 * hv), 1.0));
        glyph::plus(t, Rect{add.x + 10.0, add.y + (kChipH - 11.0) * 0.5, 11.0, 11.0}, palette::foreground(), 1.4);
        t.setFill(palette::foreground());
        t.drawText(lbl, add.x + 28.0, textfit::baseline(add.y + kChipH * 0.5, kFontPx), kFontPx, font::sans());
        mPages[kFolders]->contentH = (fy + 8.0 + kChipH + 12.0) - oy;
    }

    void SettingsSheet::paintKeyboard(IRenderTarget &t, double w, double oy, double px, double py) const
    {
        // R-SET-4: an area's micro-label, then an action a line — its label, its keys as chips, "+" at the right edge
        mKeyChipRects.clear();
        mKeyAddRects.clear();
        {
            const std::string lbl = "Reset to defaults";
            const double bw = t.measureText(lbl, kFontPx, font::sans()) + 22.0, rl = mResetLive.p.value();
            const Rect reset{w - bw, oy + 1.0, bw, 22.0};
            mKeyReset = Rect{reset.x + px, reset.y + py, reset.w, reset.h};
            const double ra = 0.4 + 0.6 * rl; // nothing changed: there, but quiet
            drawRoundedRect(t, reset, radius::control(), Paint::filledStroked(fade(palette::secondary(), ra), fade(palette::border(), ra), 1.0));
            const double hv = mHover.amount(kResetId) * rl;
            if (hv > 0.001) drawRoundedRect(t, reset, radius::control(), Paint::filled(palette::hoverWash(hv)));
            t.setFill(fade(palette::foreground(), ra));
            t.drawText(lbl, reset.x + 11.0, textfit::baseline(reset.y + reset.h * 0.5, kFontPx), kFontPx, font::sans());
            const double nx = t.measureText(kTitles[kKeyboard], 14.0, font::sansSemiBold()) + 10.0;
            t.setFill(fade(palette::mutedForeground(), 0.8));
            t.drawText(textfit::ellipsize(t, "click a key and press the new one \xC2\xB7 Backspace removes it", std::max(0.0, reset.x - nx - 10.0), 10.0, font::sans()),
                       nx, oy + 16.0, 10.0, font::sans());
        }
        const double wListen = t.measureText(kListening, kFontPx, font::sans()) + 18.0;
        // a key chip: its combo, or — listening — "Press a key…", cross-faded; its width follows both
        auto keyChip = [&](const Rect &b, const std::string &combo, double L, double amt, double hv) {
            drawRoundedRect(t, b, radius::control(), Paint::filledStroked(fade(palette::secondary(), amt), fade(lerpColor(palette::border(), palette::primary(), L), amt), 1.0));
            if (L > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(fade(palette::primaryAlpha(0.14 * L), amt)));
            if (hv > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(palette::hoverWash(hv * amt)));
            t.save();
            t.clipRect(b.x, b.y, b.w, b.h);
            const double by = textfit::baseline(b.y + b.h * 0.5, kFontPx);
            if (!combo.empty() && L < 0.999)
            {
                t.setFill(fade(palette::foreground(), amt * (1.0 - L)));
                t.drawText(combo, b.x + (b.w - t.measureText(combo, kFontPx, font::sans())) * 0.5, by, kFontPx, font::sans());
            }
            if (L > 0.001)
            {
                t.setFill(fade(palette::foreground(), amt * L));
                t.drawText(kListening, b.x + (b.w - (wListen - 18.0)) * 0.5, by, kFontPx, font::sans());
            }
            t.restore();
        };
        double y = oy + kPageTop;
        std::string area;
        for (size_t ai = 0; ai < mSettings.keys.size() && ai < mKeyRows.size(); ++ai)
        {
            const auto &k = mSettings.keys[ai];
            const auto &row = mKeyRows[ai];
            if (k.area != area)
            {
                if (!area.empty()) y += kAreaGap - kKeyRowGap;
                area = k.area;
                t.setFill(palette::mutedForeground());
                t.drawText(upper(area), 0.0, y + 4.0, 9.0, font::sansSemiBold(), 0.13 * 9.0);
                y += kRowLabelGap;
            }
            const double top = y;
            y += kChipH + kKeyRowGap;
            const Rect add{w - kChipH, top, kChipH, kChipH};
            mKeyAddRects[k.id] = Rect{add.x + px, add.y + py, add.w, add.h};
            // the chips' live widths: a chip arriving or leaving takes its room with it, so its neighbours slide
            struct Placed { const KeyChipLive *chip; int slot; double w, L, amt; };
            std::vector<Placed> ps;
            int model = 0;
            double total = 0.0;
            for (const auto &ch : row.chips)
            {
                const int slot = ch.gone ? -1 : model++;
                const double L = slot >= 0 ? listenAmount(k.id, slot) : 0.0, amt = ch.amt.value();
                const double wc = t.measureText(ch.combo, kFontPx, font::sans()) + 18.0;
                ps.push_back(Placed{&ch, slot, (wc + (wListen - wc) * L) * amt, L, amt});
                total += ps.back().w + 6.0 * amt;
            }
            const double Ln = listenAmount(k.id, (int)k.keys.size()); // "+" listening: a new chip before it
            total += wListen * Ln + 6.0 * Ln;
            double x = add.x - total;
            auto &rects = mKeyChipRects[k.id];
            for (const auto &pp : ps)
            {
                const Rect b{x, top, pp.w, kChipH};
                if (pp.slot >= 0) rects.push_back(Rect{b.x + px, b.y + py, b.w, b.h});
                if (pp.w > 0.5) keyChip(b, pp.chip->combo, pp.L, pp.amt, pp.slot >= 0 ? mHover.amount(kKeyChipBase + (int)ai * 16 + pp.slot) : 0.0);
                x += pp.w + 6.0 * pp.amt;
            }
            if (Ln > 0.001) keyChip(Rect{x, top, wListen * Ln, kChipH}, std::string(), 1.0, Ln, 0.0);
            // "+"
            const double hv = mHover.amount(kKeyAddBase + (int)ai);
            drawRoundedRect(t, add, radius::control(), Paint::filledStroked(palette::secondary(), palette::border(), 1.0));
            if (hv > 0.001) drawRoundedRect(t, add, radius::control(), Paint::filled(palette::hoverWash(hv)));
            glyph::plus(t, Rect{add.x + (kChipH - 10.0) * 0.5, add.y + (kChipH - 10.0) * 0.5, 10.0, 10.0},
                        lerpColor(palette::mutedForeground(), palette::foreground(), hv), 1.3);
            // the action's name, in what the chips leave; a dot before it when it is not its defaults
            const double ca = row.changed.value();
            const double lx = 12.0;
            if (ca > 0.001) drawRoundedRect(t, Rect{1.0, top + kChipH * 0.5 - 2.5, 5.0, 5.0}, radius::pill(), Paint::filled(fade(palette::primary(), ca)));
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, k.label, std::max(0.0, add.x - total - lx - 12.0), kFontPx, font::sans()), lx,
                       textfit::baseline(top + kChipH * 0.5, kFontPx), kFontPx, font::sans());
        }
        mPages[kKeyboard]->contentH = (y + 8.0) - oy;
    }
}
}
