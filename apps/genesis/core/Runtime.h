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
        /** The LIVE value of a field, right now — what the preview is actually drawing, after
         *  bindings, animations and any release blend. This is the answer to "the expression says
         *  one thing, the shape is somewhere else": read it instead of inferring it. False when
         *  the shape or field is unknown, or the preview has not been built.
         *
         *  `whence` reports where the value came from, because that is usually the real question:
         *  a field standing still while its binding changes is owned by motion, and one that
         *  ignores a resize has not been handed back. `Target` is the middle case G-6b adds — the
         *  field is resting on a completed track's `to`, re-evaluated every frame, so it WILL move
         *  when that expression does; only `Owned` is deaf to a resize. */
        enum class Source { Binding, Animating, Releasing, Target, Owned };
        bool fieldValue(const std::string &shapeId, const std::string &field, double &out,
                        Source *whence = nullptr) const;
        static const char *sourceName(Source s);
        /** How far a field is through being handed back to its binding: 0..1, or -1 when it is not
         *  being released at all (G-22). */
        double releaseProgress(const std::string &shapeId, const std::string &field) const;
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

        /** When each step of a chain runs, and for how long (G-27).
         *
         *  A chain is authored as durations and delays but watched as a timeline, and deriving one
         *  from the other by hand is exactly the arithmetic that goes wrong — one delay buried in
         *  one track moves everything after it. Computed from the LIVE values, because `ms` and
         *  `delay` are Gene expressions: a `speed` param re-times the component and these follow. */
        struct StepTiming
        {
            double startMs = 0.0;      // when this step begins, from the reaction firing
            double durationMs = 0.0;   // how long until it completes and hands on
            bool endless = false;      // every live track repeats forever: it never completes,
                                       // so nothing after it ever runs (G-5)
        };
        std::vector<StepTiming> stepTimings(const std::string &shapeId,
                                            const std::string &signal) const;
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
            /** A field whose track target is FOLLOWED rather than snapshotted (G-6b). `to` is an
             *  expression, and it can read a field that is animating at the same time — so
             *  `driver` eases 0 -> 1 over the track and the field is set to
             *  `lerp(start, <to, evaluated now>, driver)`. That is identical to a plain tween when
             *  the target is constant, arrives exactly ON it when it is not, and — because the
             *  entry OUTLIVES the driver — keeps the field on that expression once the track has
             *  come to rest there, until another track takes the field.
             *
             *  `releases` is the one `to` that also hands the field back to layout on completion:
             *  the bare name `original` (G-22). Then, and only then, the entry is dropped. */
            struct Release
            {
                double start = 0.0;
                artboard::Property driver{0.0};
                std::string to;          // the track's target expression, re-evaluated every frame
                double current = 0.0;    // `current` inside it: the fire-time snapshot (G-5)
                bool releases = false;   // bare `original`: clear the own-flag and drop this entry
            };
            std::map<std::string, Release> releasing;
        };
        struct ReactionState
        {
            int token = 0;
            int pending = 0;
            bool running = false;
            bool queued = false;
            /** Passes completed through the reaction's loop range (G-26). Reset when the chain is
             *  started, not when a step is played, or a restart would inherit the old count. */
            int loopsDone = 0;
            /** G-28: the IDEAL start of the current step — when the previous step was DUE to end,
             *  not the frame on which it was seen to end. Tracks are started from this, so a step
             *  that began late is already partway through on its first frame and the chain returns
             *  to schedule. Starting from the frame time instead loses the overshoot once per step,
             *  which is what makes two objects with equal totals but different step counts drift. */
            double stepAtMs = 0.0;
            double stepDurMs = 0.0;   // the ideal length of the step now running
        };

        ShapeNode *node(const std::string &id);
        const ShapeNode *node(const std::string &id) const;
        artboard::Property *propertyFor(const std::string &shapeId, const std::string &field);

        void buildTree();
        void layout(double transitionMs);
        void applyStyles();
        void bindProp(artboard::Property &p, double v, double ms, bool owned);
        /** Blend every field that is on its way back to its binding (G-22). */
        void applyReleases();
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
        /** The same scope with `current` supplied explicitly. A followed target is re-evaluated
         *  long after the property has moved on, and `current` must still mean what it meant when
         *  the reaction fired (G-5) — reading it back off the property would make the target chase
         *  the field it is moving. */
        gene::Scope trackScopeAt(const std::string &owner, double currentValue,
                                 const std::string &originalExpr) const;

        /** Evaluate one track's numbers for the speed arithmetic (G-25), in the scope of the shape
         *  that owns the field. `stepIndex`/`trackIndex` locate it inside `steps` so a blank `from`
         *  resolves the way `Document::resolvedFromExpr` says it must. */
        TrackMotion motionOf(const std::vector<Step> &steps, int stepIndex, int trackIndex,
                             const std::string &owner, double currentValue) const;
        /** The Tween curve + slopes for a track, honouring `easing == "Custom"` and continuity.
         *  `from`/`to`/`dur` are the tween's OWN endpoints as already evaluated by the caller —
         *  the real ones, so the authored speed is the speed the field actually reaches. Only the
         *  neighbours are resolved statically, since a track that has not started yet has no live
         *  value to read. The emitter derives them the same way, or `--verify` catches it. */
        void curveFor(const std::vector<Step> &steps, int stepIndex, int trackIndex,
                      const std::string &owner, double currentValue,
                      double from, double to, double durationMs,
                      artboard::Easing &easing, double &slopeIn, double &slopeOut) const;
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
        /** Every (shape, field) some track can follow, in DOCUMENT order x FIELD-TABLE order —
         *  precomputed, because the per-frame driver tick may not iterate `ShapeNode::releasing`
         *  directly: a completion callback fired inside that loop starts the next step, which
         *  INSERTS into the very map being walked. Whether the new driver were then ticked this
         *  frame would depend on `std::map`'s alphabetical order, while the emitter walks a static
         *  list in field-table order — so the two would disagree by one frame's progress. Walking
         *  this list instead makes them identical and removes the mutation-during-iteration. */
        std::vector<std::pair<std::string, std::string>> mFollowed;
        int mSeq = 0;
        bool mInLayout = false;
    };
}
