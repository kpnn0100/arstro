/*
 *  interstellar_v1 — OutputSpec: the Deliver tab's right column — what to render, as what, where.
 *
 *  R-RENDER-1 says a render NAMES its timeline and never implies "the current one", so the first
 *  thing here is a radio list of every version (indented by depth, the one being edited tagged
 *  "editing" — preselected, but not tied: picking another re-derives the default path). Then the
 *  whole output spec of R-RENDER-6, each choice a flag of the one line the Render button sends:
 *
 *      render --timeline <tl> --out <path> --format h264|h265|prores|dnxhr|png-seq
 *             [--profile …] [--quality <crf>] [--speed …] [--bits 10] [--res WxH] [--fps n|a/b]
 *             [--range a:b]
 *
 *   FORMAT    codec (cosmo SegmentedControl); then the rows that codec has — ProRes or DNxHR
 *             PROFILE, or H.264/H.265 QUALITY (Draft · Good · High · Master = CRF 28 · 23 · 18 · 12)
 *             and SPEED, and H.265's DEPTH; a PNG sequence says what it is instead; then COLOUR, the
 *             output transform (Rec.709 · 2.4 · sRGB · P3 · PQ · HLG — HDR moves an 8-bit codec to
 *             H.265 10-bit, and an 8-bit choice made later moves the colour back to Rec.709).
 *   SIZE      Full · ½ · ¼ of the project (a render never upscales or reframes), and the RATE — a
 *             stepper over the project's rate and the standard ones, the NTSC rates as exact
 *             fractions (24000/1001, not 23.976).
 *   RANGE     Whole · In–Out, with Set In / Set Out at the playhead.
 *   AUDIO     a sentence: the sound the render will carry — the master as AAC or 24-bit PCM, or that
 *             the timeline is silent, or that a PNG sequence carries none (R-AUD-9).
 *   OUTPUT    the path (follows the default until edited) and Render; a summary line under it
 *             says what will be written: size, rate, range, frames.
 *
 *  A row the chosen codec does not have COLLAPSES (its height and opacity ease together), so a
 *  codec change re-lays the column smoothly instead of popping rows in and out. Defaults are left
 *  out of the line (the service's defaults are the same), so a plain render stays a plain line.
 *  The column scrolls when it outgrows the window (R6); the version list scrolls inside it first.
 *
 *  A chosen version with dangling deltas gets a destructive note under the picker — rendering it
 *  is allowed, but not silently.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../../cosmo/widgets/SegmentedControl.h"
#include "../../../cosmo/widgets/PillButton.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class OutputSpec : public artboard::Segment
    {
    public:
        static constexpr double kRowH = 24.375;
        static constexpr int kMaxVisibleRows = 6;

        OutputSpec();
        void bind(const interstellar::AppModel &m);
        void layout();

        const std::string &renderTimeline() const { return mTimeline; }
        std::string format() const;
        std::string outPath() const { return mPath->text; }
        std::string renderLine() const;
        artboard::Rect timelineRowRect(int i) const;
        std::shared_ptr<cosmo_v2::PillButton> renderButton() { return mRender; }
        std::shared_ptr<cosmo_v2::SegmentedControl> formatPicker() { return mCodec; }
        std::shared_ptr<artboard::TextBox> pathField() { return mPath; }

        // the spec's controls, for tests and shots
        enum RowId { ProresProfile, DnxProfile, Quality, Speed, Depth, PngNote, InOut, kRows };
        std::shared_ptr<cosmo_v2::SegmentedControl> proresProfile() { return mProres; }
        std::shared_ptr<cosmo_v2::SegmentedControl> dnxProfile() { return mDnx; }
        std::shared_ptr<cosmo_v2::SegmentedControl> qualityPicker() { return mQuality; }
        std::shared_ptr<cosmo_v2::SegmentedControl> speedPicker() { return mSpeed; }
        std::shared_ptr<cosmo_v2::SegmentedControl> depthPicker() { return mDepth; }
        std::shared_ptr<cosmo_v2::SegmentedControl> colourPicker() { return mColour; }   // R-COLOR-4
        std::shared_ptr<cosmo_v2::SegmentedControl> sizePicker() { return mSize; }
        std::shared_ptr<cosmo_v2::SegmentedControl> rangePicker() { return mRange; }
        std::shared_ptr<cosmo_v2::PillButton> setInButton() { return mSetIn; }
        std::shared_ptr<cosmo_v2::PillButton> setOutButton() { return mSetOut; }
        /** The frame-rate stepper's two buttons (local; dir -1 = slower, +1 = faster). */
        artboard::Rect rateStepRect(int dir) const;
        std::string rateLabel() const;
        /** The LIVE eased presence of a codec-dependent row (0 collapsed … 1 shown). */
        double rowAmount(RowId r) const { return mRows[r].amt.value(); }
        const EasedScroll &columnScroll() const { return mColScroll; }
        /** What the line will write, in words: "960×540 · 25 fps · whole timeline · 372 frames". */
        std::string summary() const;
        /** The AUDIO line: what sound this render will carry (R-AUD-9). */
        std::string audioSentence() const;

        std::function<void(const std::string &line)> onCommand;
        // ── R-DLV-3: render presets ──
        /** Render with preset `name` ("" = Custom: the controls below). Touching any control goes back to Custom. */
        void setPreset(const std::string &name);
        const std::string &preset() const { return mPreset; }
        /** The controls' spec as render flags — what Save Preset… keeps (no timeline, path or range). */
        std::string specFlags() const;
        std::shared_ptr<cosmo_v2::PillButton> presetButton() { return mPresetBtn; }
        std::shared_ptr<cosmo_v2::PillButton> savePresetButton() { return mSavePreset; }
        std::function<void(artboard::Rect world)> onPresetMenu;
        // ── R-DLV-2: burn-ins, five: record TC (bottom left), source TC (bottom right), clip (top left),
        //    source (top right), text (top centre) — chosen from a menu on the OUTPUT header's line ──
        enum Burn { BurnTc, BurnSrcTc, BurnClip, BurnSource, BurnText, kBurns };
        void setBurn(int b, bool on) { if (b >= 0 && b < kBurns) mBurnOn[b] = on; }
        bool burn(int b) const { return b >= 0 && b < kBurns && mBurnOn[b]; }
        void setBurnText(const std::string &t) { mBurnText = t; mBurnOn[BurnText] = !t.empty(); }
        const std::string &burnText() const { return mBurnText; }
        std::shared_ptr<cosmo_v2::PillButton> burnButton() { return mBurnBtn; }
        std::function<void(artboard::Rect world)> onBurnMenu;
        std::function<void(const std::string &current)> onBurnText;   // the Text item asks for its words
        // ── R-DLV-1: the timeline's captions on this render — burned in, a subtitle track, an .srt beside
        //    it — from a menu on the RANGE header's line; dimmed (eased) when the timeline has none ──
        enum CapWay { CapBurn, CapTrack, CapSidecar, kCapWays };
        void setCaptionWay(int k, bool on) { if (k >= 0 && k < kCapWays) mCapOn[k] = on; }
        bool captionWay(int k) const { return k >= 0 && k < kCapWays && mCapOn[k]; }
        int timelineCaptions() const;
        std::shared_ptr<cosmo_v2::PillButton> captionsButton() { return mCapBtn; }
        double captionsAvailable() const { return mCapAvail.value(); }
        std::function<void(artboard::Rect world)> onCaptionsMenu;
        std::function<void(const std::string &flags)> onSavePreset;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double listTop() const;
        double listH() const;
        std::string defaultPath() const;
        void refreshDefaultPath();
        void outputSizeFor(int &w, int &h) const;
        double outputFps() const;
        std::string fpsArg() const;
        bool rowWanted(int r) const;
        std::shared_ptr<cosmo_v2::SegmentedControl> segmented(std::vector<std::string> labels, int selected);

        std::vector<interstellar::TimelineModel> mTimelines;
        std::string mTimeline, mCurrent, mProjectDir;
        int mProjW = 0, mProjH = 0;
        double mProjFps = 24.0, mDuration = 0.0, mPlayhead = 0.0;
        double mIn = 0.0, mOut = -1.0;                    // the In–Out range (seconds); mOut < 0 = the end

        std::shared_ptr<cosmo_v2::SegmentedControl> mCodec, mProres, mDnx, mQuality, mSpeed, mDepth, mSize, mRange;
        std::shared_ptr<cosmo_v2::SegmentedControl> mColour;
        std::shared_ptr<cosmo_v2::PillButton> mSetIn, mSetOut;
        std::shared_ptr<cosmo_v2::PillButton> mPresetBtn, mSavePreset;   // R-DLV-3
        std::string mPreset;
        std::vector<int> mPresetSnap;                     // the controls when the preset was chosen
        std::vector<int> controlState() const;
        double mPresetY = 0;
        artboard::AnimatedProperty mPresetAmt{0.0};      // how far the controls have dimmed for a preset
        bool mBurnOn[kBurns] = {false, false, false, false, false};
        std::string mBurnText;
        std::shared_ptr<cosmo_v2::PillButton> mBurnBtn;
        bool mCapOn[kCapWays] = {false, false, false};
        std::shared_ptr<cosmo_v2::PillButton> mCapBtn;
        artboard::AnimatedProperty mCapAvail{0.0};
        bool mCapAvailApplied = false, mCapAvailInit = false;
        std::string captionsFlag() const;
        std::string burnFlag() const;
        bool mPresetApplied = false;
    public:
        double presetAmount() const { return mPresetAmt.value(); }
    private:
        std::shared_ptr<artboard::TextBox> mPath;
        std::shared_ptr<cosmo_v2::PillButton> mRender;
        int mRateIndex = 0;                               // into the rate list; 0 = the project's
        std::string mLastDefault;
        EasedScroll mScroll;                              // the version list
        EasedScroll mColScroll;                           // the whole column
        cosmo_v2::HoverFade mHover, mSel, mStepHover;
        int mNoteCount = 0;                               // the chosen version's dangling deltas, last seen
        bool mNoteWanted = false, mNoteApplied = false, mNoteInit = false;
        artboard::AnimatedProperty mNoteAmt{0.0};

        struct Row
        {
            artboard::AnimatedProperty amt{0.0};
            bool applied = false, init = false;
        };
        Row mRows[kRows];

        // where layout put things (paint draws the headers, labels and self-drawn rows there)
        double mHdrFormatY = 0, mHdrSizeY = 0, mHdrRangeY = 0, mHdrOutputY = 0, mNoteY = 0;
        double mColourY = 0;
        double mRowY[kRows] = {}, mSizeY = 0, mRateY = 0, mRangeY = 0, mAudioY = 0, mSummaryY = 0, mListY = 0;
        double mContentH = 0;
    };
}
}
