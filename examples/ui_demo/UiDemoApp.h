#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include <memory>
#include <string>

namespace arstro
{
namespace examples
{
    class UiDemoApp
    {
    public:
        UiDemoApp(double width, double height);

        void pointer(int kind, double x, double y, int button, double timeMs);
        void keyDown(int keyCode, bool shift = false, bool ctrl = false, bool alt = false);
        void keyUp(int keyCode, bool shift = false, bool ctrl = false, bool alt = false);
        void textInput(const std::string &text);
        void render(artboard::IRenderTarget &target, double nowMs);

        artboard::Point sliderPoint(double normalized) const;
        artboard::Point checkboxPoint() const;
        artboard::Point textBoxPoint() const;
        artboard::Point buttonPoint() const;

        double sliderValue() const;
        bool badgeVisible() const;
        double previewPanelX() const;
        std::string previewTitle() const;

    private:
        void buildScene();
        void syncState();

        double mWidth = 0.0;
        double mHeight = 0.0;
        double mNowMs = 0.0;
        bool mCompact = false;
        artboard::Theme mTheme;
        artboard::Artboard mBoard;
        artboard::GestureRecognizer mRecognizer;

        std::shared_ptr<artboard::RectangleSegment> mRoot;
        std::shared_ptr<artboard::LabelSegment> mHeader;
        std::shared_ptr<artboard::LabelSegment> mStatus;
        std::shared_ptr<artboard::LabelSegment> mSliderCaption;
        std::shared_ptr<artboard::LabelSegment> mTextBoxCaption;
        std::shared_ptr<artboard::Slider> mSlider;
        std::shared_ptr<artboard::Checkbox> mCheckbox;
        std::shared_ptr<artboard::TextBox> mTextBox;
        std::shared_ptr<artboard::Button> mButton;

        std::shared_ptr<artboard::RectangleSegment> mPreviewPanel;
        std::shared_ptr<artboard::LabelSegment> mPreviewHeader;
        std::shared_ptr<artboard::LabelSegment> mPreviewBody;
        std::shared_ptr<artboard::RectangleSegment> mBadge;
        std::shared_ptr<artboard::CircleSegment> mBadgeDot;
        std::shared_ptr<artboard::LabelSegment> mBadgeLabel;
    };
}
}