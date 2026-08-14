/*
 *  Genesis — the authored document.
 *
 *  A Component is: a name, an authorable BASE to inherit from, a design size, a list of
 *  PARAMS (the knobs a consumer sets), a tree of SHAPES, and a list of REACTIONS. That is
 *  the whole model — five nouns, no sixth.
 *
 *  Every numeric/colour field of a shape is a Gene EXPRESSION, never a raw number, so a
 *  component is responsive by construction. Field names, types, defaults, which shape
 *  kinds they apply to, and which Artboard Property they drive live in ONE table
 *  (`fieldDefs()`) that the inspector, the emitter, the runtime, and the validator all
 *  read — so a new field is a table row, not five edits.
 */
#pragma once
#include "Json.h"
#include <string>
#include <vector>

namespace genesis
{
    enum class ShapeKind { Rect, Circle, Path, Label };

    /** Bit mask over ShapeKind, for FieldDef::kinds. */
    enum ShapeMask
    {
        kRect = 1 << 0,
        kCircle = 1 << 1,
        kPath = 1 << 2,
        kLabel = 1 << 3,
        kAll = kRect | kCircle | kPath | kLabel
    };

    enum class FieldType { Number, Color };

    struct FieldDef
    {
        const char *name;
        FieldType type;
        const char *defaultExpr;
        unsigned kinds;
        bool animatable;
        /** The artboard::Segment Property this drives, or "" when it is a style value the
         *  generated class must hold in its own Property and push into the style. */
        const char *segmentProperty;
        const char *doc;
    };

    /** The single field table. */
    const std::vector<FieldDef> &fieldDefs();
    const FieldDef *findField(const std::string &name);
    /** The fields that apply to `kind`, in inspector order. */
    std::vector<const FieldDef *> fieldsFor(ShapeKind kind);

    std::string shapeKindName(ShapeKind k);
    bool parseShapeKind(const std::string &s, ShapeKind &out);

    /** One command of an authored path; every coordinate is a Gene expression. */
    struct PathCmd
    {
        char op = 'M';                    // M L Q C Z
        std::vector<std::string> args;    // 2 / 2 / 4 / 6 / 0 expressions
        static int argCount(char op);
    };

    enum class ParamType { Number, Color, Text };

    struct Param
    {
        std::string name;
        ParamType type = ParamType::Number;
        std::string defaultExpr = "0";   // Gene source (Number/Color) or a literal string (Text)
        double minimum = 0.0;            // Number only, for the inspector's slider
        double maximum = 1.0;
        bool hasRange = false;
    };

    enum class Cancel { Restart, IgnoreIfRunning, Queue };
    std::string cancelName(Cancel c);
    bool parseCancel(const std::string &s, Cancel &out);

    /** The pseudo-field that means "every animatable field of this object" (G-22). */
    extern const char *const kAllFields;
    /** True when `expr` is the bare name `original` (G-22). `original + 4` is not: it ends
     *  somewhere the binding does not describe. */
    bool isBareOriginal(const std::string &expr);

    /** One animated field inside a step. Durations are Gene expressions too, so a `speed`
     *  param genuinely re-times the whole component. */
    struct Track
    {
        /** A bare field name ("opacity") targets the OWNING shape — the common case now that
         *  reactions belong to a shape. A qualified "other.opacity" still reaches a different
         *  shape, so a reaction can drive its siblings. A field of `all` stands for every
         *  animatable field of that object and is expanded by `Document::expandSteps`. */
        std::string target;
        std::string from;                // "" = start from the field's current value
        std::string to = "0";
        std::string durationMs = "200";
        std::string delayMs = "0";
        std::string easing = "EaseOutCubic";
        int repeat = 0;                  // extra cycles; -1 = forever
        bool yoyo = false;
    };

    /** True when this track ends by handing its field back to its BINDING (G-22): its `to` is the
     *  bare name `original`, AND it actually comes to rest there. It rests at `to` unless it never
     *  completes (`repeat == -1`) or it is a yoyo whose last cycle is the odd, reversed one — the
     *  same condition `artboard::Tween` uses to pick its resting endpoint. One predicate, because
     *  the interpreter and the emitter must clear the own-flag at the same moment or they diverge
     *  on the next resize. */
    bool releasesToBinding(const Track &t);

    /** Tracks that start together. Step N+1 begins when step N completes. */
    struct Step
    {
        std::vector<Track> tracks;
    };

    /** A reaction belongs to a SHAPE: selecting an object shows its reactions and nothing
     *  else. The signal is still a component-level event, so several shapes may each react to
     *  the same one — they all run. */
    struct Reaction
    {
        std::string signal;              // a BaseDef signal name
        Cancel cancel = Cancel::Restart;
        std::vector<Step> steps;
    };

    struct Shape
    {
        std::string id;
        ShapeKind kind = ShapeKind::Rect;
        std::string parent;               // "" = a direct child of the component
        std::vector<std::pair<std::string, std::string>> fields;  // ordered: name -> Gene source
        std::vector<PathCmd> path;        // Path only
        std::string text;                 // Label only (literal, or a text param name in {braces})
        std::vector<Reaction> reactions;  // this object's own reactions

        std::string field(const std::string &name) const;   // "" when unset -> the default applies
        std::string effectiveField(const std::string &name) const;  // set value, else the default
        void setField(const std::string &name, const std::string &expr);
    };

    struct Diagnostic
    {
        enum class Severity { Error, Warning };
        Severity severity = Severity::Error;
        std::string where;               // "ring.x", "reaction loopStart step 1", ...
        std::string message;
        bool isError() const { return severity == Severity::Error; }
    };

    class Document
    {
    public:
        std::string name = "MyComponent";
        std::string base = "VisualLoop";
        std::string nameSpace = "app";
        double designW = 160.0;
        double designH = 160.0;
        std::vector<Param> params;
        std::vector<Shape> shapes;        // parents always precede their children

        // ---- lookup ----
        const Shape *findShape(const std::string &id) const;
        Shape *findShape(const std::string &id);
        const Param *findParam(const std::string &name) const;
        /** The id of `shapeId`'s parent, or "" when it is top-level (its parent is the
         *  component itself). */
        std::string parentOf(const std::string &shapeId) const;
        /** Split a track target into (shape, field); a bare field belongs to `owner`. */
        static void splitTarget(const std::string &target, const std::string &owner,
                                std::string &shape, std::string &field);
        /** Every (shape, reaction) pair in document order — what the emitter and the runtime
         *  iterate, since a signal can now be handled by several objects at once. */
        std::vector<std::pair<const Shape *, const Reaction *>> allReactions() const;

        // ---- what moves (G-21, G-22) ----
        /** `r`'s steps with every `all` target expanded into one track per animatable field of
         *  the object it names, in field-table order (G-22). THE one expansion: the emitter, the
         *  interpreter and the validator all call it, so the compiled class and the preview
         *  cannot disagree about what a reaction does. */
        std::vector<Step> expandSteps(const Reaction &r, const std::string &ownerId) const;
        /** The fields of `shapeId` that some track animates, in field-table order (G-21). There
         *  is no animate mark: a field is animated because a track targets it, and a track on
         *  ANY object may target this one, which is why this is a document-level question. */
        std::vector<std::string> animatedFields(const std::string &shapeId) const;
        bool isAnimated(const std::string &shapeId, const std::string &field) const;
        int shapeIndex(const std::string &id) const;
        /** Ids of `parent`'s direct children, in document order ("" = component root). */
        std::vector<std::string> childrenOf(const std::string &parent) const;
        /** A shape id not already in use, derived from `stem`. */
        std::string uniqueShapeId(const std::string &stem) const;

        // ---- mutation ----
        /** Add `s`, de-duplicating its id; returns the id actually assigned. */
        std::string addShape(Shape s);
        /** Copy `id` and its whole subtree, naming the copies `<id>_copy` (then `_copy2`,
         *  `_copy3`, …). Parent links and every reaction target are remapped onto the copies,
         *  so the duplicate animates itself rather than the original. Returns the new root's
         *  id, or "" if `id` is unknown. */
        std::string duplicateShape(const std::string &id);
        void removeShape(const std::string &id);   // also removes descendants + their tracks
        /** Move a track inside `r` (G-23). `toStep == steps.size()` appends a NEW final step, so
         *  a drag below the last row turns simultaneous tracks into sequential ones. A step left
         *  empty is removed. Returns false when the move would change nothing, so a drag that
         *  goes nowhere never reaches the undo history. */
        static bool moveTrack(Reaction &r, int fromStep, int fromTrack, int toStep, int toIndex);

        // ---- persistence ----
        Json toJson() const;
        static Document fromJson(const Json &j, std::string *error);
        static Document load(const std::string &path, std::string *error);
        bool save(const std::string &path, std::string *error) const;

        /** Parse + type + reference + cycle check of the whole document. Errors block
         *  export; warnings are the design checklist (unhandled expected signals, states
         *  never drawn). Empty means "ready to export". */
        std::vector<Diagnostic> validate() const;
        /** Convenience: true when validate() produced no Severity::Error. */
        bool isExportable() const;

        /** A minimal starter for `base`, so a new project is never a blank canvas. */
        static Document starter(const std::string &base, const std::string &name);
    };
}
