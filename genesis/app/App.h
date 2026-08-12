/*
 *  Genesis — the application root.
 *
 *  Owns the authored Document, the live preview Runtime, and the five panels. Everything a
 *  panel needs to do — mutate the document, select a shape, fire a signal, show a status —
 *  goes through this object, so there is exactly one place that knows how an edit turns
 *  into a rebuilt preview and a re-validated document.
 *
 *  Layout follows the desktop rules: fixed chrome from named constants, fixed rails, the
 *  canvas absorbs the slack, and everything reflows on resize.
 */
#pragma once
#include "Document.h"
#include "Runtime.h"
#include "Theme.h"
#include "Verifier.h"
#include <artboard/artboard.h>
#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <vector>

namespace genesis
{
namespace ui
{
    class Chrome;
    class ShapeTree;
    class CanvasView;
    class Inspector;
    class ReactionsPanel;
    class Modal;
    class HomeScreen;

    class App : public artboard::Segment
    {
    public:
        App();
        ~App();

        /** Reflow for a new window size (R4). */
        void setSize(double w, double h);
        void advance(double nowMs) override;
        void render(artboard::IRenderTarget &t,
                    const artboard::Transform &parent = artboard::Transform::identity()) const override;

        // ---- screens ----
        // The launcher and the editor are one window with two screens, cross-faded — the
        // same shape cosmo has, so "where am I" is never a surprise.
        enum class Screen { Home, Editor };
        Screen screen() const { return mScreen; }
        void showHome();
        void showEditor();
        /** 0 = fully home, 1 = fully editor. */
        double editorAmount() const { return mScreenMix.value(); }

        // ---- document ----
        Document &doc() { return mDoc; }
        const Document &doc() const { return mDoc; }
        const std::string &path() const { return mPath; }
        bool dirty() const { return mDirty; }
        const std::vector<Diagnostic> &diagnostics() const { return mDiagnostics; }

        /** Re-validate, rebuild the preview, and refresh every panel. Call after ANY edit —
         *  it is the single funnel, so no panel can leave the app half-updated. */
        void documentChanged();
        /** Undo/redo over whole-document snapshots. Rapid edits inside kCoalesceMs collapse
         *  into one entry, so typing an expression is one undo step, not thirty. */
        bool canUndo() const { return !mUndo.empty(); }
        bool canRedo() const { return !mRedo.empty(); }
        void undo();
        void redo();
        void newDocument(const std::string &base, const std::string &name);
        bool openDocument(const std::string &path);
        bool saveDocument();
        bool saveDocumentAs(const std::string &path);
        bool exportCode();
        void startVerify();
        void finishVerify();

        // ---- selection ----
        const std::string &selectedShape() const { return mSelected; }
        void selectShape(const std::string &id);
        int selectedReaction() const { return mSelectedReaction; }
        void selectReaction(int index);

        // ---- preview ----
        Runtime &runtime() { return mRuntime; }
        const Runtime &runtime() const { return mRuntime; }
        double nowMs() const { return mNowMs; }
        /** Replay the preview from its start state — used after an edit and by the transport. */
        void restartPreview();
        /** Simulate the pointer resting on the component (the canvas's Hover switch). */
        void setPreviewHover(bool on);
        bool previewHover() const;
        bool previewOk() const { return mPreviewOk; }
        const std::string &previewError() const { return mPreviewError; }

        // ---- status ----
        void status(const std::string &message, StatusLevel level = StatusLevel::Info);
        const std::string &statusText() const { return mStatus; }
        StatusLevel statusLevel() const { return mStatusLevel; }
        double statusAge() const { return mNowMs - mStatusAt; }

        /** Show a modal (owned by the app so it always draws in the overlay pass). */
        Modal *modal() { return mModal.get(); }

        // Named accessors rather than child indices: the child ORDER is a z-order decision,
        // and nothing outside layout() should depend on it.
        HomeScreen *home() { return mHome.get(); }
        Chrome *chrome() { return mChrome.get(); }
        ShapeTree *tree() { return mTree.get(); }
        CanvasView *canvas() { return mCanvas.get(); }
        Inspector *inspector() { return mInspector.get(); }
        ReactionsPanel *reactions() { return mReactions.get(); }
        /** The editor's five panels, in layout order — for tiling checks and bulk fades. */
        std::vector<artboard::Segment *> editorPanels();

        const VerifyResult &lastVerify() const { return mVerify; }
        bool verifyRunning() const { return mVerifyRunning; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void layout();
        void rebuildAfterDocumentSwap();

        Document mDoc;
        std::string mPath;
        bool mDirty = false;
        std::vector<Diagnostic> mDiagnostics;
        Document mCommitted;                 // the document as of the last pushed snapshot
        std::vector<Document> mUndo, mRedo;
        double mLastPushMs = -1e9;
        static constexpr double kCoalesceMs = 600.0;
        static constexpr size_t kUndoDepth = 200;

        Runtime mRuntime;
        bool mPreviewOk = false;
        std::string mPreviewError;

        std::string mSelected;
        int mSelectedReaction = 0;

        double mW = 1280.0, mH = 800.0;
        double mLastDesignW = -1.0, mLastDesignH = -1.0;
        double mNowMs = 0.0;

        std::string mStatus = "Ready";
        StatusLevel mStatusLevel = StatusLevel::Info;
        double mStatusAt = 0.0;
        artboard::Property mStatusFade{0.0};

        VerifyResult mVerify;
        // The compile half runs on a worker; the compare half needs the UI thread (it builds
        // Segments, which touch process-wide focus/hover state). advance() collects the
        // result, so the window keeps drawing while the compiler works (design rule R2).
        bool mVerifyRunning = false;
        std::thread mVerifyThread;
        std::atomic<bool> mVerifyDone{false};
        CompiledRun mVerifyRun;
        Document mVerifyDoc;
        VerifyPlan mVerifyPlan;
        double mVerifyStartedMs = 0.0;

        std::shared_ptr<Chrome> mChrome;
        std::shared_ptr<ShapeTree> mTree;
        std::shared_ptr<CanvasView> mCanvas;
        std::shared_ptr<Inspector> mInspector;
        std::shared_ptr<ReactionsPanel> mReactions;
        std::shared_ptr<HomeScreen> mHome;
        std::shared_ptr<Modal> mModal;
        Screen mScreen = Screen::Home;
        artboard::Property mScreenMix{0.0};
    };
}
}
