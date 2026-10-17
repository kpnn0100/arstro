/*
 *  solaris_ui — ExportSheet: File › Export… — a sheet for ONE `render` line (R-EXPORT-1).
 *
 *  Cosmo's modal look, as the Settings sheet's (the scrim, the card, the 150/120 ms fade): a combo box a choice —
 *  Format, Sample rate, Bit depth, Quality, Loudness, Ceiling — and toggles for a stem per strip and, when the song has a
 *  picture, for writing it with the mix (R-VIDEO-1: then the Format is MP4 or MKV, the line `render … --video`). A row that does
 *  not apply to the format chosen (a bit depth for MP3, a quality for FLAC, a rate for Opus, a ceiling with no
 *  loudness target) greys out, eased, and takes no input. The sheet holds no setting: Export… asks the host to
 *  name the file (the extension the format's) and the App sends `render --out <file> --format …` — the line a
 *  shell would type, so the CLI and the window export the same files.
 */
#pragma once
#include "SettingsSheet.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class ExportSheet : public artboard::Segment
    {
    public:
        enum Row { kFormat = 0, kRate, kBits, kQuality, kLoudness, kCeiling, kStems, kVideo, kRows }; // kVideo: R-VIDEO-1
        static constexpr double kCardW = 520.0;
        static constexpr double kPad = 24.0;
        static constexpr double kRowH = 46.0;      // a choice: its name and note on the left, its control on the right
        static constexpr double kComboW = 220.0, kControlH = 28.0;

        ExportSheet();
        /** Open it for this song: its name for the file, its rate for "the song's". Choices stay as last left. */
        void show(const solaris::AppModel &m);
        void close();
        bool isOpen() const { return mOpen && !mClosing; }
        double appearAmount() const { return mAppear.value(); } // LIVE: 0 shut … 1 open

        /** What Export sends after `render --out <file>`: `--format …` and every choice that applies. */
        std::string flags() const;
        std::string extension() const;              // ".wav", ".flac", ".mp3", ".ogg", ".opus" — ".mp4", ".mkv" with the video
        /** Export…: the flags and the file name to suggest; the App asks the host for the path. */
        std::function<void(const std::string &flags, const std::string &suggested)> onExport;

        // what a test reaches (window coordinates, from the last layout)
        artboard::Rect controlRect(int row) const;
        int optionCount(int row) const;
        std::string optionLabel(int row, int i) const;
        artboard::Rect optionRect(int row, int i) const; // a row of the OPEN list
        bool comboOpen(int row) const;
        std::string shownValue(int row) const;
        double rowAmount(int row) const;            // LIVE: 1 applies … 0 greyed
        bool applies(int row) const;                // its target
        artboard::Rect exportRect() const { return mExport; }
        artboard::Rect cancelRect() const { return mCancel; }

        void advance(double nowMs) override;
        bool handleKey(const artboard::KeyEvent &e) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &) const override { return mOpen && !mClosing; }

    private:
        struct Opt
        {
            std::string label, value;
        };
        artboard::Rect cardRect() const;
        void setOptions(int row, std::vector<Opt> opts, const std::string &select);
        std::string value(int row) const;
        void formatChanged();                       // the quality list, and a choice the new format cannot take
        void videoChanged();                        // R-VIDEO-1: with the video, the formats are MP4 and MKV; without, the audio's
        bool withVideo() const { return mHasVideo && mVideo->on(); }
        artboard::Segment *control(int row) const;  // a row's combo or toggle
        void closeCombos();
        void layoutControls();

        std::vector<Opt> mOpts[kRows];
        std::shared_ptr<SettingCombo> mCombos[kRows];
        std::shared_ptr<SettingToggle> mStems, mVideo;
        std::vector<Opt> mAudioFormats;             // the Format list without the video
        std::string mAudioFormat = "wav";           // the audio format left when the video was turned on
        bool mHasVideo = false;                     // the song has a picture (R-VIDEO-1)
        artboard::AnimatedProperty mRowOn[kRows];
        double mRowLast[kRows];
        bool mRowsPlaced = false;
        std::string mName = "Untitled";
        int mSongRate = 48000;
        bool mOpen = false, mClosing = false, mShowWanted = false, mCloseWanted = false;
        artboard::AnimatedProperty mAppear{0.0};
        mutable artboard::Rect mExport, mCancel;
        cosmo_v2::HoverFade mHover;
    };
}
}
