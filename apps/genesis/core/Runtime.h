/*
 *  Genesis — Runtime: the live preview interpreter.
 *
 *  Builds a real artboard::Segment tree from a Document and runs the authored bindings
 *  and reactions against it, so the editor's canvas shows the actual component rather
 *  than a sketch of it. The root really IS the authored base class (VisualLoop, Button,
 *  …), so base behaviour — cycle signals, press visuals, the progress spring — is the
 *  real thing, not a simulation.
 *
 *  This is the SECOND implementation of Gene semantics; `CppEmitter` is the first. Two
 *  implementations of one semantics drift, and a design tool that lies about the result
 *  is worthless — so `Verifier` diffs this runtime's RecordingTarget op stream against
 *  the compiled class's. Every rule here (bind-unless-owned, step chaining on completion,
 *  infinite tracks never completing, cancellation policy) mirrors the emitter exactly;
 *  change one and you must change both, and the verifier will tell you if you didn't.
 */
#pragma once
#include "Document.h"
#include "gene/Gene.h"
#include <artboard/artboard.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace genesis
{
    /** Degrees + from/to (what an author types) -> Artboard's radians + signed sweep. Shared
     *  with the emitter so the preview and the generated code agree exactly. */
    artboard::Arc arcFromDegrees(double startDeg, double endDeg, double innerRatio);

    class Runtime
    {
    public:
        Runtime();
        ~Runtime();

        /** Rebuild the whole tree from `doc`. False + `error` when the document has errors. */
        bool build(const Document &doc, std::string *error);
        /** The live root; null before a successful build(). */
        artboard::Segment *root() const { return mRootSegment; }
        const Document &document() const { return mDoc; }

        void setSize(double w, double h);
        void advance(double nowMs);
        double nowMs() const { return mNowMs; }

        /** Fire an authored signal directly — the editor's "test this reaction" action. */
        void fire(const std::string &signal);
        /** True while any object's chain for `signal` is running. */
        bool isRunning(const std::string &signal) const;
        /** True while THIS object's chain for `signal` is running. */
        bool isRunning(const std::string &shapeId, const std::string &signal) const;
        /** Total length of a reaction in ms: the sum over steps of the longest finite track
         *  (delay + duration). An infinite track counts as one cycle, so a looping step still
         *  has a scrubbable length. 0 when the signal has no reaction. */
        double reactionDurationMs(const std::string &shapeId, const std::string &signal) const;
        /** Replay `signal` and advance to `t` (0..1) through its own timeline, so the editor
         *  can scrub one reaction without a global clock. */
        void scrub(const std::string &shapeId, const std::string &signal, double t);

        // ---- driving the base, from the preview transport ----
        void loopStart();
        void loopStop();
        bool loopRunning() const;
        void setProgress(double v);
        void setIndeterminate(bool on);
        void setPressed(bool on);          // Button: synthesised press
        void setSliderValue(double v);     // Slider
        void setChecked(bool on);          // Checkbox: click only if it is not already there
        /** Checkbox: always dispatch the click. A verify step must mean the same thing on
         *  both sides, so the plan's `check` action toggles rather than assigns. */
        void toggleChecked();
        void setHovered(bool on);

        // ---- live param overrides (the inspector's sliders) ----
        void setParamNumber(const std::string &name, double v);
        void setParamColor(const std::string &name, const artboard::Color &c);
        void setParamText(const std::string &name, const std::string &s);
        bool paramNumber(const std::string &name, double &out) const;
        bool paramColor(const std::string &name, artboard::Color &out) const;

        /** Per-shape live segment, for hit-testing / selection in the canvas. */
        artboard::Segment *segmentFor(const std::string &shapeId) const;
        /** The shape whose segment is (or contains) `seg`; "" when none. */
        std::string shapeIdFor(const artboard::Segment *seg) const;

        /** Non-fatal notes from the last build (unpreviewable raw{}, etc.). */
        const std::vector<std::string> &notes() const { return mNotes; }

        // ---- internals used by the base-class hosts (public so the hosts can call them) ----
        void hostSignal(const std::string &signal);
        bool readBase(const std::string &name, double &out) const;
        /** Stamp the frame clock, BEFORE the base ticks its properties: a tween completing
         *  during that tick starts the next step, and it reads this clock. */
        void hostPreAdvance(double nowMs) { mNowMs = nowMs; }
        void hostAdvance(double nowMs);

    private:
        struct ShapeNode
        {
            std::string id;
            ShapeKind kind = ShapeKind::Rect;
            std::shared_ptr<artboard::Segment> seg;
            std::map<std::string, artboard::Property> styleProps;   // strokeWidth, cornerRadius, ...
            std::map<std::string, artboard::Color> colors;          // fill, stroke
            std::map<std::string, bool> owned;                      // fields motion has taken over
        };
        struct ReactionState
        {
            int token = 0;
            int pending = 0;
            bool running = false;
            bool queued = false;
        };

        ShapeNode *node(const std::string &id);
        const ShapeNode *node(const std::string &id) const;
        artboard::Property *propertyFor(const std::string &shapeId, const std::string &field);

        void buildTree();
        void layout(double transitionMs);
        void applyStyles();
        void bindProp(artboard::Property &p, double v, double ms, bool owned);
        /** Reactions are keyed by (object, signal): several objects may react to one signal,
         *  and each keeps its own token, pending count and cancellation state. */
        static std::string reactionKey(const std::string &shapeId, const std::string &signal);
        void startReaction(const std::string &owner, const Reaction &r);
        void playStep(const std::string &owner, const Reaction &r, size_t stepIndex);

        gene::Scope layoutScope(const std::string &owner) const;
        gene::Scope liveScope(const std::string &owner) const;
        /** liveScope plus `current` — the target field's value at fire time. */
        /** liveScope plus `current` (the target's value now) and `original` (the value of the
         *  target field's own binding, re-evaluated now) — G-6, G-22. */
        gene::Scope trackScope(const std::string &owner, const artboard::Property &p,
                               const std::string &originalExpr) const;
        bool lookupIdent(const std::string &name, gene::Value &out) const;
        bool lookupTheme(const std::string &role, gene::Value &out) const;

        double evalNumber(const std::string &src, const gene::Scope &sc, double fallback) const;
        bool evalColor(const std::string &src, const gene::Scope &sc, artboard::Color &out) const;

        Document mDoc;
        std::shared_ptr<artboard::Segment> mRootOwner;
        artboard::Segment *mRootSegment = nullptr;
        std::vector<ShapeNode> mNodes;
        std::vector<std::pair<std::string, std::string>> mOrder;   // (shape, field) evaluation order
        mutable std::map<std::string, gene::Value> mLocals;        // during layout
        std::map<std::string, ReactionState> mReactions;
        std::map<std::string, double> mParamNumbers;
        std::map<std::string, artboard::Color> mParamColors;
        std::map<std::string, std::string> mParamTexts;
        std::vector<std::string> mNotes;
        double mNowMs = 0.0;
        double mLastW = -1.0, mLastH = -1.0;
        bool mAttached = false;
        bool mLayoutEveryFrame = false;
        int mSeq = 0;
        bool mInLayout = false;
    };
}
