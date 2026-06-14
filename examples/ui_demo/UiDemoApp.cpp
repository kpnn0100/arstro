#include "UiDemoApp.h"
#include <cstdio>

namespace arstro
{
namespace examples
{
    using namespace artboard;

    namespace
    {
        BoxStyle makeBox(const Color &fill, const Color &stroke, double radius)
        {
            return BoxStyle{Paint::filledStroked(fill, stroke, 1.0), radius};
        }

        double normalizeSlider(const Slider &slider)
        {
            const double span = slider.maximum() - slider.minimum();
            if (span <= 0.0)
                return 0.0;
            return (slider.value() - slider.minimum()) / span;
        }
    }

    UiDemoApp::UiDemoApp(double width, double height)
        : mWidth(width), mHeight(height), mTheme(Theme::basicTheme()), mBoard(Size{width, height})
    {
        mBoard.setBackground(Color::hex(0x16181f));
        buildScene();
        mRecognizer.setSink([this](const Gesture &g) {
            if (mRoot)
                mRoot->onGesture(g);
        });
    }

    void UiDemoApp::buildScene()
    {
        mRoot = std::make_shared<RectangleSegment>();
        mRoot->style = makeBox(Color::hex(0x20242d), Color::hex(0x474d5a), 18.0);
        mRoot->x.set(24.0);
        mRoot->y.set(24.0);
        mRoot->width.set(mWidth - 48.0);
        mRoot->height.set(mHeight - 48.0);

        mHeader = std::make_shared<LabelSegment>();
        mHeader->text = "Artboard Segment Demo";
        mHeader->style = TextStyle{Color::hex(0xf6f1e8), 22.0};
        mHeader->x.set(28.0);
        mHeader->y.set(24.0);
        mHeader->inputTransparent = true;
        mRoot->addChild(mHeader);

        auto subtitle = std::make_shared<LabelSegment>();
        subtitle->text = "All controls are Segment objects; the preview card contains a nested child group.";
        subtitle->style = TextStyle{Color::hex(0xb7bcc8), 13.0};
        subtitle->x.set(28.0);
        subtitle->y.set(54.0);
        subtitle->inputTransparent = true;
        mRoot->addChild(subtitle);

        mSliderCaption = std::make_shared<LabelSegment>();
        mSliderCaption->text = "Group offset";
        mSliderCaption->style = mTheme.label;
        mSliderCaption->x.set(28.0);
        mSliderCaption->y.set(102.0);
        mSliderCaption->inputTransparent = true;
        mRoot->addChild(mSliderCaption);

        mSlider = std::make_shared<Slider>(mTheme.slider);
        mSlider->setRange(0.0, 100.0);
        mSlider->setValue(35.0);
        mSlider->x.set(28.0);
        mSlider->y.set(126.0);
        mSlider->width.set(280.0);
        mSlider->height.set(30.0);
        mRoot->addChild(mSlider);

        mCheckbox = std::make_shared<Checkbox>("Show nested child group", mTheme.checkbox);
        mCheckbox->setChecked(true);
        mCheckbox->x.set(28.0);
        mCheckbox->y.set(182.0);
        mCheckbox->width.set(250.0);
        mCheckbox->height.set(28.0);
        mRoot->addChild(mCheckbox);

        mTextBoxCaption = std::make_shared<LabelSegment>();
        mTextBoxCaption->text = "Preview title";
        mTextBoxCaption->style = mTheme.label;
        mTextBoxCaption->x.set(28.0);
        mTextBoxCaption->y.set(238.0);
        mTextBoxCaption->inputTransparent = true;
        mRoot->addChild(mTextBoxCaption);

        mTextBox = std::make_shared<TextBox>(mTheme.textBox);
        mTextBox->placeholder = "Type into the preview";
        mTextBox->text = "Segment card";
        mTextBox->x.set(28.0);
        mTextBox->y.set(262.0);
        mTextBox->width.set(280.0);
        mTextBox->height.set(36.0);
        mRoot->addChild(mTextBox);

        mButton = std::make_shared<Button>("Animate parent", mTheme.button);
        mButton->x.set(28.0);
        mButton->y.set(322.0);
        mButton->width.set(180.0);
        mButton->height.set(36.0);
        mButton->onClick = [this]() {
            mCompact = !mCompact;
            mPreviewPanel->x.animateTo(mCompact ? 466.0 : 378.0, 280.0, Easing::EaseInOutCubic, mNowMs);
            mPreviewPanel->y.animateTo(mCompact ? 126.0 : 92.0, 280.0, Easing::EaseInOutCubic, mNowMs);
        };
        mRoot->addChild(mButton);

        mStatus = std::make_shared<LabelSegment>();
        mStatus->style = TextStyle{Color::hex(0xd0d4db), 13.0};
        mStatus->x.set(28.0);
        mStatus->y.set(382.0);
        mStatus->inputTransparent = true;
        mRoot->addChild(mStatus);

        mPreviewPanel = std::make_shared<RectangleSegment>();
        mPreviewPanel->style = makeBox(Color::hex(0x2b3040), Color::hex(0xf8b040), 16.0);
        mPreviewPanel->x.set(378.0);
        mPreviewPanel->y.set(92.0);
        mPreviewPanel->width.set(280.0);
        mPreviewPanel->height.set(220.0);
        mRoot->addChild(mPreviewPanel);

        mPreviewHeader = std::make_shared<LabelSegment>();
        mPreviewHeader->style = TextStyle{Color::hex(0xf6f1e8), 18.0};
        mPreviewHeader->x.set(18.0);
        mPreviewHeader->y.set(18.0);
        mPreviewHeader->inputTransparent = true;
        mPreviewPanel->addChild(mPreviewHeader);

        mPreviewBody = std::make_shared<LabelSegment>();
        mPreviewBody->text = "Move the slider, hide the child group, then animate the parent card.";
        mPreviewBody->style = TextStyle{Color::hex(0xc2c7d0), 13.0};
        mPreviewBody->x.set(18.0);
        mPreviewBody->y.set(48.0);
        mPreviewBody->inputTransparent = true;
        mPreviewPanel->addChild(mPreviewBody);

        mBadge = std::make_shared<RectangleSegment>();
        mBadge->style = makeBox(Color::hex(0x39435a), Color::hex(0x8aa0bf), 14.0);
        mBadge->x.set(24.0);
        mBadge->y.set(108.0);
        mBadge->width.set(176.0);
        mBadge->height.set(78.0);
        mPreviewPanel->addChild(mBadge);

        mBadgeDot = std::make_shared<CircleSegment>();
        mBadgeDot->style = BoxStyle{Paint::filled(Color::hex(0xf8b040)), 999.0};
        mBadgeDot->x.set(14.0);
        mBadgeDot->y.set(22.0);
        mBadgeDot->width.set(28.0);
        mBadgeDot->height.set(28.0);
        mBadgeDot->inputTransparent = true;
        mBadge->addChild(mBadgeDot);

        mBadgeLabel = std::make_shared<LabelSegment>();
        mBadgeLabel->text = "child follows parent";
        mBadgeLabel->style = TextStyle{Color::hex(0xf1f3f7), 13.0};
        mBadgeLabel->x.set(52.0);
        mBadgeLabel->y.set(24.0);
        mBadgeLabel->inputTransparent = true;
        mBadge->addChild(mBadgeLabel);

        mBoard.add(mRoot);
        syncState();
    }

    void UiDemoApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        RawPointer::Kind rawKind = kind == 0 ? RawPointer::Kind::Down
                               : kind == 2 ? RawPointer::Kind::Up
                                           : RawPointer::Kind::Move;
        PointerButton pointerButton = button == 2 ? PointerButton::Right
                                      : button == 1 ? PointerButton::Middle
                                                    : PointerButton::Left;
        mNowMs = timeMs;
        mRecognizer.feed(RawPointer{rawKind, Point{x, y}, pointerButton, timeMs});
    }

    void UiDemoApp::keyDown(int keyCode, bool shift, bool ctrl, bool alt)
    {
        if (mRoot)
            mRoot->dispatchKey(KeyEvent{KeyEvent::Type::Down, keyCode, "", shift, ctrl, alt});
    }

    void UiDemoApp::keyUp(int keyCode, bool shift, bool ctrl, bool alt)
    {
        if (mRoot)
            mRoot->dispatchKey(KeyEvent{KeyEvent::Type::Up, keyCode, "", shift, ctrl, alt});
    }

    void UiDemoApp::textInput(const std::string &text)
    {
        if (mRoot)
            mRoot->dispatchKey(KeyEvent{KeyEvent::Type::Text, 0, text, false, false, false});
    }

    void UiDemoApp::syncState()
    {
        const double normalized = normalizeSlider(*mSlider);
        mBadge->visible = mCheckbox->checked();
        mBadge->x.set(24.0 + normalized * 56.0);
        mBadgeDot->x.set(14.0 + normalized * 72.0);
        mPreviewHeader->text = mTextBox->text.empty() ? "Segment card" : mTextBox->text;

        char status[128];
        std::snprintf(status, sizeof(status), "offset %.0f%% · child group %s · focused control shared by one index",
                      mSlider->value(), mCheckbox->checked() ? "visible" : "hidden");
        mStatus->text = status;
        mStatus->style.color = mCheckbox->checked() ? Color::hex(0xd0d4db) : Color::hex(0xf8b040);
    }

    void UiDemoApp::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        if (mRoot)
            mRoot->advance(nowMs);
        syncState();
        mBoard.render(target, nowMs);
    }

    Point UiDemoApp::sliderPoint(double normalized) const
    {
        if (!mSlider)
            return Point{};
        if (normalized < 0.0)
            normalized = 0.0;
        if (normalized > 1.0)
            normalized = 1.0;
        return mSlider->worldTransform().apply(Point{mSlider->width.value() * normalized, mSlider->height.value() * 0.5});
    }

    Point UiDemoApp::checkboxPoint() const
    {
        return mCheckbox ? mCheckbox->worldTransform().apply(Point{mCheckbox->height.value() * 0.5, mCheckbox->height.value() * 0.5}) : Point{};
    }

    Point UiDemoApp::textBoxPoint() const
    {
        return mTextBox ? mTextBox->worldTransform().apply(Point{18.0, mTextBox->height.value() * 0.5}) : Point{};
    }

    Point UiDemoApp::buttonPoint() const
    {
        return mButton ? mButton->worldTransform().apply(Point{mButton->width.value() * 0.5, mButton->height.value() * 0.5}) : Point{};
    }

    double UiDemoApp::sliderValue() const
    {
        return mSlider ? mSlider->value() : 0.0;
    }

    bool UiDemoApp::badgeVisible() const
    {
        return mBadge && mBadge->visible;
    }

    double UiDemoApp::previewPanelX() const
    {
        return mPreviewPanel ? mPreviewPanel->x.value() : 0.0;
    }

    std::string UiDemoApp::previewTitle() const
    {
        return mPreviewHeader ? mPreviewHeader->text : std::string{};
    }
}
}