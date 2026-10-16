#include "SettingsSheet.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include "KeyCombo.h"
#include <algorithm>
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
        constexpr double kRowLabelGap = 13.0;   // label baseline → its chips: a label belongs to what is BELOW it
        constexpr double kRowGap = 26.0;        // chips → the next label: wider, so the rows read as groups
        constexpr double kFolderRowH = 26.0;
        constexpr int kDoneId = 900, kAddId = 901, kResetId = 902, kRemoveBase = 1000, kKeyChipBase = 2000, kKeyAddBase = 4000;
        constexpr double kKeyRowGap = 4.0;      // R-SET-4: the keymap's rows sit close — one action a line
        const char *const kListening = "Press a key\xE2\x80\xA6";
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        const char *kLabels[SettingsSheet::kRows] = {"OUTPUT", "INPUT", "SAMPLE RATE", "BUFFER", "METRONOME", "CLICK LEVEL",
                                                     "PREVIEW LEVEL", "TEMPO", "METER", "REDUCED MOTION"};
        const char *kKeys[SettingsSheet::kRows] = {"output", "input", "sampleRate", "bufferSize", "metronome", "metronomeLevel",
                                                   "auditionLevel", "newBpm", "newSig", "reducedMotion"};
        // the sheet's sections (R-SET-3), in order; the sample folders sit before Interface
        struct Section { const char *title; std::vector<int> rows; bool folders, keys; };
        const Section kSections[] = {
            {"Audio", {SettingsSheet::kOutput, SettingsSheet::kInput, SettingsSheet::kRate, SettingsSheet::kBuffer}, false, false},
            {"Playback", {SettingsSheet::kMetronome, SettingsSheet::kClickLevel, SettingsSheet::kPreviewLevel}, false, false},
            {"New songs", {SettingsSheet::kNewBpm, SettingsSheet::kNewSig}, false, false},
            {"Sample folders", {}, true, false},
            {"Interface", {SettingsSheet::kMotion}, false, false},
            {"Keyboard", {}, false, true}, // R-SET-4
        };
        std::string upper(std::string s)
        {
            for (auto &ch : s) ch = (char)std::toupper((unsigned char)ch);
            return s;
        }
        constexpr double kSectionGap = 30.0; // a section title sits this far below the last chips above it
        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%g", v);
            return b;
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
    }

    SettingsSheet::SettingsSheet()
    {
        // rate and buffer choices are fixed; device chips come from the model
        for (int r : kRates)
        {
            char b[16];
            std::snprintf(b, sizeof b, "%g kHz", r / 1000.0);
            mChips[kRate].push_back(Chip{b, std::to_string(r)});
        }
        for (int b : kBuffers) mChips[kBuffer].push_back(Chip{std::to_string(b), std::to_string(b)});
        mChips[kMetronome] = {Chip{"Off", "off"}, Chip{"On", "on"}};
        for (int d : {-18, -12, -6, 0}) mChips[kClickLevel].push_back(Chip{std::to_string(d) + " dB", std::to_string(d)});
        for (int d : {-18, -12, -6, 0}) mChips[kPreviewLevel].push_back(Chip{std::to_string(d) + " dB", std::to_string(d)});
        for (int b : {100, 120, 124, 126, 128, 140, 150, 174}) mChips[kNewBpm].push_back(Chip{std::to_string(b), std::to_string(b)});
        for (const char *m : {"4/4", "3/4", "6/8", "7/8"}) mChips[kNewSig].push_back(Chip{m, m});
        mChips[kMotion] = {Chip{"Off", "off"}, Chip{"On", "on"}};
        for (int r = 0; r < kRows; ++r) mChosen[r].assign(mChips[r].size(), AnimatedProperty{0.0});
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
        for (int r : {kOutput, kInput})
        {
            std::vector<Chip> chips = {Chip{"System default", ""}};
            const std::string dir = r == kOutput ? "out" : "in";
            for (const auto &d : m.devices)
                if (d.dir == dir) chips.push_back(Chip{d.name, d.id});
            // a device chosen on another day but not listed now still shows, so the choice is never invisible
            const std::string &cur = r == kOutput ? mOutput : mInput;
            bool listed = cur.empty();
            for (const auto &c : chips) listed = listed || c.value == cur;
            if (!listed) chips.push_back(Chip{cur + " (not connected)", cur});
            if (chips.size() != mChips[r].size()) mChosen[r].resize(chips.size(), AnimatedProperty{0.0});
            mChips[r] = chips;
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
        default: return std::string();
        }
    }

    int SettingsSheet::chosen(int row) const
    {
        const std::string v = current(row);
        for (size_t i = 0; i < mChips[row].size(); ++i)
            if (mChips[row][i].value == v) return (int)i;
        return -1;
    }

    double SettingsSheet::chosenAmount(int row, int chip) const
    {
        return row >= 0 && row < kRows && chip >= 0 && chip < (int)mChosen[row].size() ? mChosen[row][(size_t)chip].value() : 0.0;
    }

    Rect SettingsSheet::chipRect(int row, int chip) const
    {
        return row >= 0 && row < kRows && chip >= 0 && chip < (int)mChipRects[row].size() ? mChipRects[row][(size_t)chip] : Rect{};
    }

    void SettingsSheet::revealRect(const Rect &r)
    {
        const Rect c = cardRect();
        mScroll.reveal(r.y - (c.y - mScroll.value()), r.h + kPad);
    }

    void SettingsSheet::show() { mShowWanted = true; mCloseWanted = false; stopListening(); }
    void SettingsSheet::close() { mCloseWanted = true; mShowWanted = false; stopListening(); }

    void SettingsSheet::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (mShowWanted)
        {
            mOpen = true;
            mClosing = false;
            mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mShowWanted = false;
            mScroll.reset();
        }
        if (mCloseWanted && mOpen && !mClosing)
        {
            mClosing = true;
            mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mCloseWanted = false;
        }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        for (int r = 0; r < kRows; ++r)
        {
            const int c = chosen(r);
            for (size_t i = 0; i < mChosen[r].size(); ++i)
            {
                auto &p = mChosen[r][i];
                const double want = (int)i == c ? 1.0 : 0.0;
                // a chip that becomes chosen fills over 200 ms, whoever chose it (§1)
                if (std::fabs(p.value() - want) > 1e-6 && !p.isAnimating()) p.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
                p.update(nowMs);
            }
        }
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
        mScroll.advance(nowMs);
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    Rect SettingsSheet::cardRect() const
    {
        const double W = width.value(), H = height.value();
        const double w = std::min(kCardW, W - 32.0);
        const double h = std::min(mContentH, H - 64.0);
        return Rect{(W - w) * 0.5, std::max(32.0, (H - h) * 0.5), w, std::max(120.0, h)};
    }

    int SettingsSheet::hoverId(const Point &p) const
    {
        if (mDone.contains(p)) return kDoneId;
        if (mAddFolder.contains(p)) return kAddId;
        if (mKeyReset.contains(p)) return kResetId;
        for (size_t a = 0; a < mKeyRows.size(); ++a)
        {
            if (keyAddRect(mKeyRows[a].id).contains(p)) return kKeyAddBase + (int)a;
            const auto it = mKeyChipRects.find(mKeyRows[a].id);
            if (it != mKeyChipRects.end())
                for (size_t i = 0; i < it->second.size(); ++i)
                    if (it->second[i].contains(p)) return kKeyChipBase + (int)a * 16 + (int)i;
        }
        for (size_t i = 0; i < mRemove.size(); ++i)
            if (mRemove[i].contains(p)) return kRemoveBase + (int)i;
        int id = 0;
        for (int r = 0; r < kRows; ++r)
            for (size_t i = 0; i < mChipRects[r].size(); ++i, ++id)
                if (mChipRects[r][i].contains(p)) return id;
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
            mScroll.scrollBy(g.delta.y);
            return true;
        case Gesture::Type::Click:
        case Gesture::Type::DoubleClick:
        {
            const bool twice = g.type == Gesture::Type::DoubleClick; // only a key chip takes the second press (it listens still)
            if (!twice && !mCard.contains(local)) { close(); return true; }   // the scrim closes, as cosmo's does
            if (!twice && mDone.contains(local)) { close(); return true; }
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
            if (twice) return true;
            stopListening(); // a click anywhere else lets it go
            if (mKeyReset.contains(local))
            {
                std::string line;
                for (const auto &k : mSettings.keys)
                    if (k.keys != k.defaults) line += " key." + k.id + "=default";
                if (!line.empty() && onCommand) onCommand("settings set" + line); // ONE line, every changed action
                return true;
            }
            if (mAddFolder.contains(local)) { if (onAddFolder) onAddFolder(); return true; }
            for (size_t i = 0; i < mRemove.size() && i < mFolders.size(); ++i)
                if (mRemove[i].contains(local)) { if (onCommand) onCommand("folder remove " + q(mFolders[i])); return true; }
            for (int r = 0; r < kRows; ++r)
                for (size_t i = 0; i < mChipRects[r].size() && i < mChips[r].size(); ++i)
                    if (mChipRects[r][i].contains(local))
                    {
                        if (onCommand) onCommand(std::string("settings set ") + q(std::string(kKeys[r]) + "=" + mChips[r][i].value));
                        return true;
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
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 27 || e.keyCode == 13)) close();
        return true; // a modal owns the keyboard
    }

    void SettingsSheet::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::scrim(a)));

        // ── the content, measured: sections, each a title then its rows; chip rows wrap at the inner width ──
        const double cw = std::min(kCardW, W - 32.0), inner = cw - 2 * kPad;
        double y = kPad + 16.0 + 22.0;                       // title baseline + gap
        std::vector<Rect> rowRects[kRows];
        double labelY[kRows];
        std::vector<double> sectionY;
        double foldersY = 0, keysY = 0;
        std::vector<std::pair<std::string, double>> areaLabels; // R-SET-4: an area's micro-label, then its actions
        std::vector<double> keyRowY;
        for (const auto &sec : kSections)
        {
            sectionY.push_back(y);
            y += 22.0;                                       // the section title, then its first label
            if (sec.keys)
            {
                keysY = sectionY.back();
                std::string area;
                for (const auto &k : mSettings.keys)
                {
                    if (k.area != area)
                    {
                        if (!area.empty()) y += kRowGap - kKeyRowGap;
                        area = k.area;
                        areaLabels.emplace_back(upper(area), y);
                        y += kRowLabelGap;
                    }
                    keyRowY.push_back(y);
                    y += kChipH + kKeyRowGap;
                }
                y += kSectionGap - kKeyRowGap;
                continue;
            }
            if (sec.folders)
            {
                foldersY = y - 8.0;                          // the list reads from just under the title (its note sits beside it)
                y += std::max<size_t>(1, mFolders.size()) * kFolderRowH + 8.0 + kChipH + kSectionGap - 8.0;
                continue;
            }
            for (int r : sec.rows)
            {
                labelY[r] = y;
                y += kRowLabelGap;
                double x = 0;
                for (const auto &c : mChips[r])
                {
                    const std::string lbl = textfit::ellipsize(t, c.label, inner, kFontPx, font::sans());
                    const double w = std::min(inner, t.measureText(lbl, kFontPx, font::sans()) + 22.0);
                    if (x > 0 && x + w > inner) { x = 0; y += kChipH + 6.0; }
                    rowRects[r].push_back(Rect{x, y, w, kChipH});
                    x += w + 6.0;
                }
                y += kChipH + kRowGap;
            }
            y += kSectionGap - kRowGap;
        }
        const double doneY = y;
        mContentH = y + kChipH + kPad;

        const Rect c = cardRect();
        mCard = c;
        mScroll.setExtent(c.y, c.h, mContentH);
        const double oy = c.y - mScroll.value();                 // content → screen
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));
        t.save();
        t.clipRect(c.x, c.y, c.w, c.h);
        t.setFill(fade(palette::foreground(), a));
        t.drawText("Settings", c.x + kPad, oy + kPad + 16.0, 14.0, font::sansSemiBold());
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText("this machine \xC2\xB7 never a song's", c.x + kPad + t.measureText("Settings", 14.0, font::sansSemiBold()) + 10.0, oy + kPad + 16.0,
                   10.0, font::sans());

        // a section label: the UPPERCASE word tracked +0.13 (the micro-label formula), then a plain note
        auto label = [&](const std::string &word, const std::string &note, double baseY) {
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(word, c.x + kPad, baseY, 9.0, font::sansSemiBold(), 0.13 * 9.0);
            if (note.empty()) return;
            const double nx = c.x + kPad + t.measureText(word, 9.0, font::sansSemiBold(), 0.13 * 9.0) + 10.0;
            t.setFill(fade(palette::mutedForeground(), 0.8 * a));
            t.drawText(textfit::ellipsize(t, note, std::max(0.0, c.x + kPad + inner - nx), 10.0, font::sans()), nx, baseY, 10.0, font::sans());
        };
        // a section title: 12 px SemiBold, a hairline above all but the first
        for (size_t k = 0; k < sectionY.size(); ++k)
        {
            if (k > 0)
            {
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(c.x + kPad, oy + sectionY[k] - 14.0); t.lineTo(c.x + kPad + inner, oy + sectionY[k] - 14.0); t.strokePath();
            }
            t.setFill(fade(palette::foreground(), a));
            t.drawText(kSections[k].title, c.x + kPad, oy + sectionY[k] + 4.0, 12.0, font::sansSemiBold());
            if (kSections[k].folders)
            {
                t.setFill(fade(palette::mutedForeground(), 0.8 * a));
                t.drawText("the browser lists these first", c.x + kPad + t.measureText(kSections[k].title, 12.0, font::sansSemiBold()) + 10.0,
                           oy + sectionY[k] + 4.0, 10.0, font::sans());
            }
        }
        int hid = 0;
        for (int r = 0; r < kRows; ++r)
        {
            std::string note;
            if (r == kOutput) note = "the clock \xE2\x80\x94 every other device follows it";
            if (r == kRate) note = "new songs start here";
            if (r == kMetronome) note = "clicks while playing \xE2\x80\x94 never in a render";
            if (r == kNewBpm) note = "a new song starts at this tempo";
            if (r == kMotion) note = "every tween collapses \xE2\x80\x94 the system's own setting counts too";
            if (r == kBuffer)
            {
                char b[64];
                std::snprintf(b, sizeof b, "%.1f ms of latency", mLatencyMs);
                note = b;
            }
            label(kLabels[r], note, oy + labelY[r]);
            mChipRects[r].clear();
            for (size_t i = 0; i < rowRects[r].size(); ++i, ++hid)
            {
                const Rect b{c.x + kPad + rowRects[r][i].x, oy + rowRects[r][i].y, rowRects[r][i].w, rowRects[r][i].h};
                mChipRects[r].push_back(b);
                const double ch = i < mChosen[r].size() ? mChosen[r][i].value() : 0.0;
                drawRoundedRect(t, b, radius::control(), Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
                if (ch > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(fade(palette::primary(), a * ch)));
                const double hv = mHover.amount(hid) * a;
                if (hv > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(palette::hoverWash(hv)));
                const std::string lab = textfit::ellipsize(t, mChips[r][i].label, b.w - 22.0, kFontPx, font::sans());
                t.setFill(fade(lerpColor(palette::foreground(), palette::primaryForeground(), ch), a));
                t.drawText(lab, b.x + (b.w - t.measureText(lab, kFontPx, font::sans())) * 0.5, textfit::baseline(b.y + b.h * 0.5, kFontPx), kFontPx,
                           ch > 0.5 ? font::sansMedium() : font::sans());
            }
        }

        // ── sample folders ──
        mRemove.clear();
        double fy = oy + foldersY;
        if (mFolders.empty())
        {
            t.setFill(fade(palette::mutedForeground(), 0.8 * a));
            t.drawText("None yet \xE2\x80\x94 add the folders you keep samples in.", c.x + kPad, textfit::baseline(fy + kFolderRowH * 0.5, kFontPx), kFontPx, font::sans());
            fy += kFolderRowH;
        }
        for (size_t i = 0; i < mFolders.size(); ++i)
        {
            const Rect row{c.x + kPad, fy, inner, kFolderRowH};
            const Rect rm{row.right() - 22.0, row.y + (row.h - 18.0) * 0.5, 18.0, 18.0};
            mRemove.push_back(rm);
            const double hv = mHover.amount(kRemoveBase + (int)i) * a;
            if (i + 1 < mFolders.size())
            {
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(row.x, row.bottom()); t.lineTo(row.right(), row.bottom()); t.strokePath();
            }
            t.setFill(fade(palette::foreground(), a));
            t.drawText(tailFit(t, mFolders[i], inner - 34.0, 10.0, font::mono()), row.x, textfit::baseline(row.y + row.h * 0.5, 10.0), 10.0, font::mono());
            if (hv > 0.001) drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color xc = fade(lerpColor(palette::mutedForeground(), palette::destructive(), hv), a);
            glyph::line(t, rm.x + 5.5, rm.y + 5.5, rm.right() - 5.5, rm.bottom() - 5.5, xc, 1.3);
            glyph::line(t, rm.right() - 5.5, rm.y + 5.5, rm.x + 5.5, rm.bottom() - 5.5, xc, 1.3);
            fy += kFolderRowH;
        }
        {
            const std::string lbl = "Add folder\xE2\x80\xA6";
            const double w = t.measureText(lbl, kFontPx, font::sans()) + 40.0;
            mAddFolder = Rect{c.x + kPad, fy + 8.0, w, kChipH};
            const double hv = mHover.amount(kAddId) * a;
            drawRoundedRect(t, mAddFolder, radius::control(),
                            Paint::filledStroked(fade(palette::primaryAlpha(0.10 * hv), a), fade(lerpColor(palette::border(), palette::primary(), 0.5 + 0.5 * hv), a), 1.0));
            glyph::plus(t, Rect{mAddFolder.x + 10.0, mAddFolder.y + (kChipH - 11.0) * 0.5, 11.0, 11.0}, fade(palette::foreground(), a), 1.4);
            t.setFill(fade(palette::foreground(), a));
            t.drawText(lbl, mAddFolder.x + 28.0, textfit::baseline(mAddFolder.y + kChipH * 0.5, kFontPx), kFontPx, font::sans());
        }

        // ── the keyboard (R-SET-4): an action a line — its label, its keys as chips, "+" at the right edge ──
        mKeyChipRects.clear();
        mKeyAddRects.clear();
        {
            const std::string lbl = "Reset to defaults";
            const double w = t.measureText(lbl, kFontPx, font::sans()) + 22.0, rl = mResetLive.p.value();
            const double base = oy + keysY + 4.0;            // the section title's baseline
            mKeyReset = Rect{c.x + kPad + inner - w, base - 15.0, w, 22.0};
            const double ra = a * (0.4 + 0.6 * rl);         // nothing changed: there, but quiet
            drawRoundedRect(t, mKeyReset, radius::control(), Paint::filledStroked(fade(palette::secondary(), ra), fade(palette::border(), ra), 1.0));
            const double hv = mHover.amount(kResetId) * a * rl;
            if (hv > 0.001) drawRoundedRect(t, mKeyReset, radius::control(), Paint::filled(palette::hoverWash(hv)));
            t.setFill(fade(palette::foreground(), ra));
            t.drawText(lbl, mKeyReset.x + 11.0, textfit::baseline(mKeyReset.y + mKeyReset.h * 0.5, kFontPx), kFontPx, font::sans());
            const double nx = c.x + kPad + t.measureText("Keyboard", 12.0, font::sansSemiBold()) + 10.0;
            t.setFill(fade(palette::mutedForeground(), 0.8 * a));
            t.drawText(textfit::ellipsize(t, "click a key and press the new one \xC2\xB7 Backspace removes it",
                                          std::max(0.0, mKeyReset.x - nx - 10.0), 10.0, font::sans()),
                       nx, base, 10.0, font::sans());
        }
        for (const auto &al : areaLabels) label(al.first, "", oy + al.second);
        const double wListen = t.measureText(kListening, kFontPx, font::sans()) + 18.0;
        // a key chip: its combo, or — listening — "Press a key…", cross-faded; its width follows both
        auto keyChip = [&](const Rect &b, const std::string &combo, double L, double amt, double hv) {
            const double al = a * amt;
            drawRoundedRect(t, b, radius::control(), Paint::filledStroked(fade(palette::secondary(), al), fade(lerpColor(palette::border(), palette::primary(), L), al), 1.0));
            if (L > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(fade(palette::primaryAlpha(0.14 * L), a * amt)));
            if (hv > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(palette::hoverWash(hv * amt)));
            t.save();
            t.clipRect(b.x, b.y, b.w, b.h);
            const double by = textfit::baseline(b.y + b.h * 0.5, kFontPx);
            if (!combo.empty() && L < 0.999)
            {
                t.setFill(fade(palette::foreground(), al * (1.0 - L)));
                t.drawText(combo, b.x + (b.w - t.measureText(combo, kFontPx, font::sans())) * 0.5, by, kFontPx, font::sans());
            }
            if (L > 0.001)
            {
                t.setFill(fade(palette::foreground(), al * L));
                t.drawText(kListening, b.x + (b.w - (wListen - 18.0)) * 0.5, by, kFontPx, font::sans());
            }
            t.restore();
        };
        for (size_t ai = 0; ai < mSettings.keys.size() && ai < keyRowY.size() && ai < mKeyRows.size(); ++ai)
        {
            const auto &k = mSettings.keys[ai];
            const auto &row = mKeyRows[ai];
            const double top = oy + keyRowY[ai];
            const Rect add{c.x + kPad + inner - kChipH, top, kChipH, kChipH};
            mKeyAddRects[k.id] = add;
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
                if (pp.slot >= 0) rects.push_back(b);
                if (pp.w > 0.5) keyChip(b, pp.chip->combo, pp.L, pp.amt, pp.slot >= 0 ? mHover.amount(kKeyChipBase + (int)ai * 16 + pp.slot) * a : 0.0);
                x += pp.w + 6.0 * pp.amt;
            }
            if (Ln > 0.001) keyChip(Rect{x, top, wListen * Ln, kChipH}, std::string(), 1.0, Ln, 0.0);
            // "+"
            const double hv = mHover.amount(kKeyAddBase + (int)ai) * a;
            drawRoundedRect(t, add, radius::control(), Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
            if (hv > 0.001) drawRoundedRect(t, add, radius::control(), Paint::filled(palette::hoverWash(hv)));
            glyph::plus(t, Rect{add.x + (kChipH - 10.0) * 0.5, add.y + (kChipH - 10.0) * 0.5, 10.0, 10.0},
                        fade(lerpColor(palette::mutedForeground(), palette::foreground(), hv), a), 1.3);
            // the action's name, in what the chips leave; a dot in the gutter when it is not its defaults
            const double ca = row.changed.value() * a;
            if (ca > 0.001) drawRoundedRect(t, Rect{c.x + kPad - 12.0, top + kChipH * 0.5 - 2.5, 5.0, 5.0}, radius::pill(), Paint::filled(fade(palette::primary(), ca)));
            t.setFill(fade(palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, k.label, std::max(0.0, add.x - total - (c.x + kPad) - 12.0), kFontPx, font::sans()), c.x + kPad,
                       textfit::baseline(top + kChipH * 0.5, kFontPx), kFontPx, font::sans());
        }

        // ── Done (primary) ──
        mDone = Rect{c.right() - kPad - 88.0, oy + doneY, 88.0, kChipH + 4.0};
        drawRoundedRect(t, mDone, radius::control(), Paint::filled(fade(palette::primary(), a)));
        const double dhv = mHover.amount(kDoneId) * a;
        if (dhv > 0.001) drawRoundedRect(t, mDone, radius::control(), Paint::filled(palette::hoverWash(dhv)));
        t.setFill(fade(palette::primaryForeground(), a));
        t.drawText("Done", mDone.x + (mDone.w - t.measureText("Done", 12.0, font::sansMedium())) * 0.5, textfit::baseline(mDone.y + mDone.h * 0.5, 12.0), 12.0,
                   font::sansMedium());
        t.restore();
        mScroll.drawBar(t, c.right() - 4.0, a);
    }
}
}
