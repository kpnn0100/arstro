#include "App.h"
#include "TextUtil.h"
#include "codegen/CppEmitter.h"
#include "widgets/CanvasView.h"
#include "widgets/Chrome.h"
#include "widgets/Inspector.h"
#include "Recents.h"
#include "widgets/HomeScreen.h"
#include "widgets/Modal.h"
#include "widgets/ReactionsPanel.h"
#include "widgets/ShapeTree.h"
#include <algorithm>

namespace genesis
{
namespace ui
{
    namespace
    {
        std::string dirOf(const std::string &path)
        {
            const size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
        }
    }

    App::App()
    {
        mDoc = Document::starter("VisualLoop", "CoolVisualLoop");

        mChrome = std::make_shared<Chrome>(*this);
        mTree = std::make_shared<ShapeTree>(*this);
        mCanvas = std::make_shared<CanvasView>(*this);
        mInspector = std::make_shared<Inspector>(*this);
        mReactions = std::make_shared<ReactionsPanel>(*this);
        mHome = std::make_shared<HomeScreen>();
        mModal = std::make_shared<Modal>(*this);
        mHome->onNewComponent = [this](const std::string &base) {
            newDocument(base, "My" + base);
            showEditor();
        };
        mHome->onOpen = [this] { mModal->openBrowse(); };
        mHome->onOpenRecent = [this](const std::string &path) {
            if (openDocument(path)) showEditor();
        };
        mHome->setRecents(Recents::load());
        addChild(mHome);
        addChild(mChrome);
        addChild(mTree);
        addChild(mCanvas);
        addChild(mInspector);
        addChild(mReactions);
        addChild(mModal);   // last: it owns the overlay pass

        if (!mDoc.shapes.empty())
            mSelected = mDoc.shapes.front().id;
        mCommitted = mDoc;
        documentChanged();
        mUndo.clear();
        mRedo.clear();
        status("Ready — a starter VisualLoop is loaded", StatusLevel::Info);
    }

    std::vector<artboard::Segment *> App::editorPanels()
    {
        return {mChrome.get(), mTree.get(), mCanvas.get(), mReactions.get(), mInspector.get()};
    }

    void App::showHome()
    {
        if (mScreen == Screen::Home) return;
        mScreen = Screen::Home;
        mHome->setRecents(Recents::load());   // reflect anything saved since we left
        mScreenMix.animateTo(0.0, artboard::motion::kDurationMedium2,
                             artboard::Easing::EmphasizedAccel, mNowMs);
        layout();
    }

    void App::showEditor()
    {
        if (mScreen == Screen::Editor) return;
        mScreen = Screen::Editor;
        mScreenMix.animateTo(1.0, artboard::motion::kDurationMedium4,
                             artboard::Easing::EmphasizedDecel, mNowMs);
        layout();
    }

    // ───────────────────────── the single edit funnel ─────────────────────────

    void App::documentChanged()
    {
        // Snapshot the previous state for undo. Two guards keep the stack useful: nothing is
        // pushed when nothing actually changed, and edits closer together than kCoalesceMs
        // replace the top entry instead of adding one — so typing an expression is one step.
        const std::string before = mCommitted.toJson().dump();
        const std::string after = mDoc.toJson().dump();
        if (before != after)
        {
            const bool coalesce = !mUndo.empty() && (mNowMs - mLastPushMs) < kCoalesceMs;
            if (!coalesce)
            {
                mUndo.push_back(mCommitted);
                if (mUndo.size() > kUndoDepth)
                    mUndo.erase(mUndo.begin());
            }
            mLastPushMs = mNowMs;
            mRedo.clear();      // a fresh edit forks the future
            mCommitted = mDoc;
        }
        mDirty = true;
        mDiagnostics = mDoc.validate();

        // The preview frame follows the DESIGN size only when that actually changes, so a
        // frame the author dragged to a test size is not reset by every unrelated edit.
        if (mCanvas && (mDoc.designW != mLastDesignW || mDoc.designH != mLastDesignH))
        {
            mLastDesignW = mDoc.designW;
            mLastDesignH = mDoc.designH;
            mCanvas->setFrameSize(mDoc.designW, mDoc.designH);
        }

        std::string err;
        mPreviewOk = mRuntime.build(mDoc, &err);
        mPreviewError = err;
        if (mPreviewOk)
        {
            mRuntime.setSize(mCanvas ? mCanvas->frameW() : mDoc.designW,
                             mCanvas ? mCanvas->frameH() : mDoc.designH);
            mRuntime.advance(mNowMs);
            restartPreview();
        }

        if (mTree) mTree->refresh();
        if (mInspector) mInspector->refresh();
        if (mReactions) mReactions->refresh();

        // Errors are the loudest thing in the app: they block export, so they must be seen.
        int errors = 0, warnings = 0;
        for (const auto &d : mDiagnostics)
            (d.isError() ? errors : warnings)++;
        if (errors)
            status(std::to_string(errors) + (errors == 1 ? " error" : " errors") + " — export blocked",
                   StatusLevel::Bad);
        else if (warnings)
            status(std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings"), StatusLevel::Warn);
        layout();
    }

    void App::undo()
    {
        if (mUndo.empty())
        {
            status("Nothing to undo", StatusLevel::Warn);
            return;
        }
        mRedo.push_back(mDoc);
        mDoc = mUndo.back();
        mUndo.pop_back();
        mCommitted = mDoc;
        mLastPushMs = -1e9;
        rebuildAfterDocumentSwap();
        status("Undo", StatusLevel::Info);
    }

    void App::redo()
    {
        if (mRedo.empty())
        {
            status("Nothing to redo", StatusLevel::Warn);
            return;
        }
        mUndo.push_back(mDoc);
        mDoc = mRedo.back();
        mRedo.pop_back();
        mCommitted = mDoc;
        mLastPushMs = -1e9;
        rebuildAfterDocumentSwap();
        status("Redo", StatusLevel::Info);
    }

    /** Everything documentChanged() does, WITHOUT pushing another snapshot. */
    void App::rebuildAfterDocumentSwap()
    {
        mDirty = true;
        mDiagnostics = mDoc.validate();
        if (!mDoc.findShape(mSelected))
            mSelected.clear();
        if (mSelectedReaction >= (int)mDoc.reactions.size())
            mSelectedReaction = (int)mDoc.reactions.size() - 1;
        if (mCanvas)
        {
            mLastDesignW = mDoc.designW;
            mLastDesignH = mDoc.designH;
            mCanvas->setFrameSize(mDoc.designW, mDoc.designH);
            mCanvas->refresh();
        }
        std::string err;
        mPreviewOk = mRuntime.build(mDoc, &err);
        mPreviewError = err;
        if (mPreviewOk)
        {
            mRuntime.setSize(mCanvas ? mCanvas->frameW() : mDoc.designW,
                             mCanvas ? mCanvas->frameH() : mDoc.designH);
            mRuntime.advance(mNowMs);
            restartPreview();
        }
        if (mTree) mTree->refresh();
        if (mInspector) mInspector->refresh();
        if (mReactions) mReactions->refresh();
        layout();
    }

    void App::restartPreview()
    {
        if (!mPreviewOk) return;
        // Put the component into the state that shows its authored motion: a loop runs, a
        // progress bar starts empty, a control sits idle.
        if (mDoc.base == "VisualLoop")
        {
            mRuntime.loopStop();
            mRuntime.advance(mNowMs);
            mRuntime.loopStart();
        }
        else if (mDoc.base == "ProgressIndicator")
            mRuntime.setProgress(0.0);
        mRuntime.advance(mNowMs);
    }

    void App::setPreviewHover(bool on)
    {
        if (mCanvas) mCanvas->setHoverSim(on);
    }
    bool App::previewHover() const { return mCanvas && mCanvas->hoverSim(); }

    // ───────────────────────── document actions ─────────────────────────

    void App::newDocument(const std::string &base, const std::string &name)
    {
        mDoc = Document::starter(base, name);
        mPath.clear();
        mSelected = mDoc.shapes.empty() ? std::string() : mDoc.shapes.front().id;
        mSelectedReaction = 0;
        if (mCanvas) mCanvas->refresh();
        mCommitted = mDoc;
        documentChanged();
        mUndo.clear();
        mRedo.clear();
        mDirty = false;
        status("New " + base + " component: " + mDoc.name, StatusLevel::Good);
    }

    bool App::openDocument(const std::string &path)
    {
        std::string err;
        Document d = Document::load(path, &err);
        if (!err.empty())
        {
            status("Could not open: " + err, StatusLevel::Bad);
            return false;
        }
        mDoc = std::move(d);
        mPath = path;
        mSelected = mDoc.shapes.empty() ? std::string() : mDoc.shapes.front().id;
        mSelectedReaction = 0;
        if (mCanvas) mCanvas->refresh();
        mCommitted = mDoc;
        documentChanged();
        mUndo.clear();
        mRedo.clear();
        mDirty = false;
        Recents::remember(mPath, mDoc.name, mDoc.base);
        status("Opened " + path, StatusLevel::Good);
        return true;
    }

    bool App::saveDocument()
    {
        if (mPath.empty())
        {
            mModal->openSaveAs();
            return false;
        }
        return saveDocumentAs(mPath);
    }

    bool App::saveDocumentAs(const std::string &path)
    {
        std::string p = path;
        if (p.size() < 8 || p.compare(p.size() - 8, 8, ".genesis") != 0)
            p += ".genesis";
        std::string err;
        if (!mDoc.save(p, &err))
        {
            status("Could not save: " + err, StatusLevel::Bad);
            return false;
        }
        mPath = p;
        mDirty = false;
        Recents::remember(mPath, mDoc.name, mDoc.base);
        status("Saved " + p, StatusLevel::Good);
        return true;
    }

    bool App::exportCode()
    {
        const EmittedCode code = emitCpp(mDoc);
        if (!code.ok())
        {
            mModal->openReport("Export blocked", {code.error}, StatusLevel::Bad);
            status("Export blocked: " + code.error, StatusLevel::Bad);
            return false;
        }
        const std::string dir = mPath.empty() ? std::string(".") : dirOf(mPath);
        std::string err;
        if (!writeEmitted(code, dir, &err))
        {
            status("Could not write: " + err, StatusLevel::Bad);
            return false;
        }
        std::vector<std::string> lines{
            "Wrote " + dir + "/" + code.headerName,
            "Wrote " + dir + "/" + code.sourceName,
            "",
            "The generated class depends on artboard only — no Genesis runtime ships with it."};
        // Warnings are reported at export because that is when they matter: a component that
        // never reacts to a signal its base emits is unfinished, not broken.
        for (const auto &d : mDiagnostics)
            if (!d.isError())
                lines.push_back(" " + d.where + ": " + d.message);
        mModal->openReport("Exported " + mDoc.name, lines, StatusLevel::Good);
        status("Exported " + code.headerName + " and " + code.sourceName, StatusLevel::Good);
        return true;
    }

    App::~App()
    {
        if (mVerifyThread.joinable())
            mVerifyThread.join();
    }

    void App::startVerify()
    {
        if (mVerifyRunning)
            return;
        if (mVerifyThread.joinable())
            mVerifyThread.join();
        mVerifyRunning = true;
        mVerifyDone = false;
        mVerifyStartedMs = mNowMs;
        // Snapshot the document: the worker must not read one the author is still editing.
        mVerifyDoc = mDoc;
        mVerifyPlan = VerifyPlan::defaultFor(mVerifyDoc);
        status("Verifying — compiling the generated class…", StatusLevel::Info);
        const VerifyConfig cfg = VerifyConfig::defaults();
        mVerifyThread = std::thread([this, cfg] {
            mVerifyRun = verifyCompile(mVerifyDoc, mVerifyPlan, cfg);
            mVerifyDone = true;   // published last: advance() only reads mVerifyRun after this
        });
    }

    /** Called from advance() once the worker has finished: the comparison half needs the UI
     *  thread, and so does showing the report. */
    void App::finishVerify()
    {
        if (mVerifyThread.joinable())
            mVerifyThread.join();
        mVerifyRunning = false;
        mVerifyDone = false;
        mVerify = verifyCompare(mVerifyDoc, mVerifyPlan, mVerifyRun);

        std::vector<std::string> lines{mVerify.summary()};
        if (!mVerify.available)
        {
            lines.push_back("");
            lines.push_back("Verification needs a C++ compiler and a built artboard_core.");
            lines.push_back("Treat this as UNVERIFIED, not as a pass.");
            mModal->openReport("Verification unavailable", lines, StatusLevel::Warn);
            status(mVerify.summary(), StatusLevel::Warn);
            return;
        }
        if (mVerify.matched)
        {
            lines.push_back("");
            lines.push_back("The preview and the exported code drew the same thing at every sampled frame.");
            mModal->openReport("Verified", lines, StatusLevel::Good);
            status(mVerify.summary(), StatusLevel::Good);
            return;
        }
        for (size_t i = 0; i < mVerify.differences.size() && i < 8; ++i)
        {
            const auto &d = mVerify.differences[i];
            lines.push_back(" " + std::to_string((int)d.atMs) + "ms op " + std::to_string(d.opIndex) +
                            " " + d.field);
            lines.push_back("   preview " + d.preview + " / compiled " + d.compiled);
        }
        mModal->openReport("Verification FAILED", lines, StatusLevel::Bad);
        status(mVerify.summary(), StatusLevel::Bad);
    }

    // ───────────────────────── selection ─────────────────────────

    void App::selectShape(const std::string &id)
    {
        if (mSelected == id) return;
        mSelected = id;
        if (mInspector) mInspector->refresh();
        layout();
    }

    void App::selectReaction(int index)
    {
        mSelectedReaction = index;
        if (mReactions) mReactions->refresh();
        layout();
    }

    void App::status(const std::string &message, StatusLevel level)
    {
        mStatus = message;
        mStatusLevel = level;
        mStatusAt = mNowMs;
        mStatusFade.set(0.0);
        mStatusFade.animateTo(1.0, artboard::motion::kDurationShort3, artboard::Easing::EaseOutCubic, mNowMs);
    }

    // ───────────────────────── layout + frame ─────────────────────────

    void App::setSize(double w, double h)
    {
        mW = std::max(720.0, w);
        mH = std::max(520.0, h);
        width.set(mW);
        height.set(mH);
        layout();
    }

    void App::layout()
    {
        if (!mChrome) return;
        if (mHome)
        {
            mHome->x.set(0);
            mHome->y.set(0);
            mHome->layout(mW, mH);
        }
        // Fixed chrome + rails from named constants; the canvas absorbs every spare pixel.
        // The rails shrink (never below a usable minimum) before the canvas is squeezed.
        const double chromeH = metrics::chromeH();
        const double squeeze = std::min(1.0, std::max(0.62, (mW - 720.0) / 560.0 * 0.38 + 0.62));
        const double railW = std::max(150.0, metrics::railW() * squeeze);
        const double inspW = std::max(190.0, metrics::inspectorW() * squeeze);
        const double panelH = std::max(140.0, std::min(metrics::panelH(), mH * 0.32));
        const double bodyH = mH - chromeH;
        const double centreW = std::max(220.0, mW - railW - inspW);

        mChrome->layout(mW);
        mChrome->x.set(0);
        mChrome->y.set(0);

        mTree->x.set(0);
        mTree->y.set(chromeH);
        mTree->layout(railW, bodyH);

        mCanvas->x.set(railW);
        mCanvas->y.set(chromeH);
        mCanvas->layout(centreW, bodyH - panelH);

        mReactions->x.set(railW);
        mReactions->y.set(chromeH + bodyH - panelH);
        mReactions->layout(centreW, panelH);

        mInspector->x.set(railW + centreW);
        mInspector->y.set(chromeH);
        mInspector->layout(inspW, bodyH);

        mModal->x.set(0);
        mModal->y.set(0);
        mModal->layout(mW, mH);
    }

    void App::advance(double nowMs)
    {
        mNowMs = nowMs;
        mStatusFade.update(nowMs);
        mScreenMix.update(nowMs);
        // The two screens cross-fade as one group each (FR-32), so neither ever pops and
        // whichever is faded out stops taking input.
        const double mix = mScreenMix.value();
        if (mHome)
            mHome->opacity.set(1.0 - mix);
        for (artboard::Segment *panel : editorPanels())
            if (panel) panel->opacity.set(mix);
        if (mVerifyRunning && mVerifyDone)
            finishVerify();
        if (mPreviewOk && mCanvas)
        {
            mRuntime.setSize(mCanvas->frameW(), mCanvas->frameH());
            mRuntime.advance(nowMs);
        }
        Segment::advance(nowMs);
    }

    void App::render(artboard::IRenderTarget &t, const artboard::Transform &parent) const
    {
        // Publish the live target so every panel measures text with the metrics it will draw
        // with — the basis of "text never overflows".
        setMeasureTarget(&t);
        artboard::Segment::render(t, parent);
        renderOverlay(t, parent);
        setMeasureTarget(nullptr);
    }

    void App::onPaint(artboard::IRenderTarget &t) const
    {
        artboard::drawRoundedRect(t, {0, 0, width.value(), height.value()}, 0.0,
                                  artboard::Paint::filled(palette::background()));
    }
}
}
