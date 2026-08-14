/*
 *  Genesis — unit tests (MiniTest, the harness Artboard uses).
 *
 *  Everything here is headless: the document model, the Gene language (parse / fold /
 *  interpret / emit), the code emitter, and the preview runtime — asserted through
 *  artboard::RecordingTarget so behaviour is proved without a device.
 */
#include "MiniTest.h"
#include "BaseCatalog.h"
#include "Document.h"
#include "Json.h"
#include "Runtime.h"
#include "Theme.h"
#include "Verifier.h"
#include "codegen/CppEmitter.h"
#include "gene/Gene.h"
#include <cmath>
#include <string>

using namespace genesis;
using K = artboard::DrawOp::Kind;

namespace
{
    /** Evaluate a Gene source with no scope beyond the built-ins. */
    double evalNum(const std::string &src, double w = 0, double h = 0)
    {
        std::string err;
        gene::NodePtr n = gene::parse(src, &err);
        if (!n) return -12345.0;
        gene::Scope sc;
        sc.w = w;
        sc.h = h;
        gene::Value v;
        if (!gene::evaluate(n, sc, v, &err)) return -12345.0;
        return v.isColor ? -12345.0 : v.num;
    }
    std::string emit(const std::string &src)
    {
        gene::NodePtr n = gene::parse(src, nullptr);
        if (!n) return "<parse error>";
        n = gene::fold(n);
        gene::CppNames names;
        std::string err;
        const std::string out = gene::emitCpp(n, names, &err);
        return err.empty() ? out : "<" + err + ">";
    }
}

// ───────────────────────── Json ─────────────────────────
TEST(Json_roundtrips_values_and_preserves_key_order)
{
    Json o = Json::object();
    o.set("zebra", Json::number(1));
    o.set("apple", Json::string("a\"b\n"));
    o.set("flag", Json::boolean(true));
    Json arr = Json::array();
    arr.push(Json::number(1.5));
    arr.push(Json::makeNull());
    o.set("list", arr);

    const std::string text = o.dump();
    // Insertion order is preserved so a saved document diffs cleanly.
    CHECK(text.find("zebra") < text.find("apple"));

    std::string err;
    Json back = Json::parse(text, &err);
    CHECK(err.empty());
    CHECK(back["zebra"].asNumber() == 1);
    CHECK(back["apple"].asString() == "a\"b\n");
    CHECK(back["flag"].asBool());
    CHECK(back["list"].size() == 2);
    CHECK_NEAR(back["list"].at(0).asNumber(), 1.5, 1e-12);
    CHECK(back["list"].at(1).isNull());
    CHECK(back["missing"].isNull());       // missing keys never throw
    CHECK(back["list"].at(99).isNull());
}
TEST(Json_reports_errors_with_a_line_number)
{
    std::string err;
    Json::parse("{\n  \"a\": 1,\n  \"b\" 2\n}", &err);
    CHECK(!err.empty());
    CHECK(err.find("line 3") != std::string::npos);
    Json::parse("[1, 2] extra", &err);
    CHECK(err.find("trailing") != std::string::npos);
    Json ok = Json::parse("{ \"a\": 1 } // a note", &err);   // line comments are tolerated
    CHECK(err.empty());
    CHECK(ok["a"].asNumber() == 1);
}
TEST(Json_number_formatting_is_stable)
{
    CHECK(Json::number(42).dump() == "42");
    CHECK(Json::number(0.5).dump() == "0.5");
    CHECK(Json::number(-3).dump() == "-3");
}

// ───────────────────────── Gene: parse + evaluate ─────────────────────────
TEST(Gene_arithmetic_precedence_and_grouping)
{
    CHECK_NEAR(evalNum("1 + 2 * 3"), 7.0, 1e-12);
    CHECK_NEAR(evalNum("(1 + 2) * 3"), 9.0, 1e-12);
    CHECK_NEAR(evalNum("10 - 3 - 2"), 5.0, 1e-12);       // left-associative
    CHECK_NEAR(evalNum("2 * 3 % 4"), 2.0, 1e-12);
    CHECK_NEAR(evalNum("-2 + 5"), 3.0, 1e-12);
    CHECK_NEAR(evalNum("!0"), 1.0, 1e-12);
    CHECK_NEAR(evalNum("1 < 2 ? 10 : 20"), 10.0, 1e-12);
    CHECK_NEAR(evalNum("1 > 2 ? 10 : 20"), 20.0, 1e-12);
    CHECK_NEAR(evalNum("1 && 0 || 1"), 1.0, 1e-12);
}
TEST(Gene_division_by_zero_is_zero_not_nan)
{
    // A preview must never produce a NaN and paint nothing; the emitter mirrors this.
    CHECK_NEAR(evalNum("5 / 0"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("5 % 0"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("sqrt(-4)"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("log(0)"), 0.0, 1e-12);
}
TEST(Gene_builtin_identifiers_and_functions)
{
    CHECK_NEAR(evalNum("w", 100, 40), 100.0, 1e-12);
    CHECK_NEAR(evalNum("h", 100, 40), 40.0, 1e-12);
    CHECK_NEAR(evalNum("minSide", 100, 40), 40.0, 1e-12);
    CHECK_NEAR(evalNum("maxSide", 100, 40), 100.0, 1e-12);
    CHECK_NEAR(evalNum("aspect", 100, 40), 2.5, 1e-12);
    CHECK_NEAR(evalNum("aspect", 100, 0), 0.0, 1e-12);
    CHECK_NEAR(evalNum("PI"), 3.14159265358979324, 1e-12);
    CHECK_NEAR(evalNum("turns(1)"), 6.28318530717958648, 1e-12);
    CHECK_NEAR(evalNum("deg(PI)"), 180.0, 1e-9);
    CHECK_NEAR(evalNum("rad(180)"), 3.14159265358979324, 1e-9);
    CHECK_NEAR(evalNum("pct(50)"), 0.5, 1e-12);
    CHECK_NEAR(evalNum("clamp(5, 0, 1)"), 1.0, 1e-12);
    CHECK_NEAR(evalNum("clamp(-5, 0, 1)"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("lerp(10, 20, 0.25)"), 12.5, 1e-12);
    CHECK_NEAR(evalNum("sign(-3)"), -1.0, 1e-12);
    CHECK_NEAR(evalNum("sign(0)"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("mod(7, 4)"), 3.0, 1e-12);
    CHECK_NEAR(evalNum("atan2(0, 1)"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("round(2.6)"), 3.0, 1e-12);
    CHECK_NEAR(evalNum("floor(2.6)"), 2.0, 1e-12);
    CHECK_NEAR(evalNum("ceil(2.1)"), 3.0, 1e-12);
    CHECK_NEAR(evalNum("abs(-2)"), 2.0, 1e-12);
    CHECK_NEAR(evalNum("pow(2, 10)"), 1024.0, 1e-12);
    CHECK_NEAR(evalNum("exp(0)"), 1.0, 1e-12);
    CHECK_NEAR(evalNum("cos(0) + sin(0) + tan(0)"), 1.0, 1e-12);
    CHECK_NEAR(evalNum("max(min(3, 4), 2)"), 3.0, 1e-12);
}
TEST(Gene_arithmetic_macros_are_total)
{
    // div pairs with mod: div(a,b)*b + mod(a,b) == a, for every non-zero b.
    CHECK_NEAR(evalNum("div(7, 2)"), 3.0, 1e-12);
    CHECK_NEAR(evalNum("div(-7, 2)"), -3.0, 1e-12);      // truncated, not floored
    CHECK_NEAR(evalNum("div(7, 2) * 2 + mod(7, 2)"), 7.0, 1e-12);
    CHECK_NEAR(evalNum("div(-7, 2) * 2 + mod(-7, 2)"), -7.0, 1e-12);
    CHECK_NEAR(evalNum("div(5, 0)"), 0.0, 1e-12);        // total, like every other function

    CHECK_NEAR(evalNum("hypot(3, 4)"), 5.0, 1e-12);
    CHECK_NEAR(evalNum("dist(1, 1, 4, 5)"), 5.0, 1e-12);

    CHECK_NEAR(evalNum("snap(13, 5)"), 15.0, 1e-12);
    CHECK_NEAR(evalNum("snap(12, 5)"), 10.0, 1e-12);
    CHECK_NEAR(evalNum("snap(13, 0)"), 13.0, 1e-12);     // no grid: unchanged

    CHECK_NEAR(evalNum("wrap(370, 0, 360)"), 10.0, 1e-12);
    CHECK_NEAR(evalNum("wrap(-10, 0, 360)"), 350.0, 1e-12);   // negatives wrap UP
    CHECK_NEAR(evalNum("wrap(5, 0, 0)"), 0.0, 1e-12);         // empty range: the low end

    CHECK_NEAR(evalNum("remap(5, 0, 10, 0, 100)"), 50.0, 1e-12);
    CHECK_NEAR(evalNum("remap(5, 0, 0, 7, 9)"), 7.0, 1e-12);  // empty input range: the low end

    CHECK_NEAR(evalNum("step(5, 4)"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("step(5, 6)"), 1.0, 1e-12);
    CHECK_NEAR(evalNum("smoothstep(0, 10, 5)"), 0.5, 1e-12);
    CHECK_NEAR(evalNum("smoothstep(0, 10, -3)"), 0.0, 1e-12);
    CHECK_NEAR(evalNum("smoothstep(0, 10, 99)"), 1.0, 1e-12);
    CHECK_NEAR(evalNum("smoothstep(4, 4, 9)"), 1.0, 1e-12);   // zero-width edge: a hard step

    CHECK_NEAR(evalNum("asin(2)"), evalNum("asin(1)"), 1e-12);    // clamped, never NaN
    CHECK_NEAR(evalNum("acos(-9)"), evalNum("acos(-1)"), 1e-12);
    CHECK_NEAR(evalNum("atan(0)"), 0.0, 1e-12);

    CHECK_NEAR(evalNum("pi"), 3.14159265358979324, 1e-12);
    CHECK_NEAR(evalNum("tau"), 6.28318530717958648, 1e-12);
    CHECK_NEAR(evalNum("e"), 2.71828182845904524, 1e-12);
    CHECK_NEAR(evalNum("PI"), evalNum("pi"), 1e-12);              // the older spellings still work
    CHECK_NEAR(evalNum("TAU"), evalNum("tau"), 1e-12);
}
TEST(Gene_new_macros_emit_and_arity_is_checked)
{
    CHECK(emit("div(w, 3)") == "genesisDiv(w, 3.0)");
    CHECK(emit("snap(w, 8)") == "genesisSnap(w, 8.0)");
    CHECK(emit("wrap(w, 0, 360)") == "genesisWrap(w, 0.0, 360.0)");
    CHECK(emit("remap(w, 0, 1, 0, 100)") == "genesisRemap(w, 0.0, 1.0, 0.0, 100.0)");
    CHECK(emit("smoothstep(0, 1, w)") == "genesisSmoothstep(0.0, 1.0, w)");
    CHECK(emit("step(1, w)") == "(((w) < (1.0)) ? 0.0 : 1.0)");
    CHECK(emit("hypot(w, h)") == "std::hypot(w, h)");
    CHECK(emit("pi") == "3.14159265358979324");
    // Constant folding still applies to the new ones.
    CHECK(emit("div(9, 2)") == "4.0");
    CHECK(emit("snap(13, 5)") == "15.0");

    std::string err;
    CHECK(gene::parse("div(1)", &err) == nullptr);
    CHECK(err.find("takes 2") != std::string::npos);
    CHECK(gene::parse("remap(1, 2, 3)", &err) == nullptr);
    CHECK(gene::parse("dist(1, 2)", &err) == nullptr);
}
TEST(Gene_colours_are_a_second_value_type)
{
    std::string err;
    gene::Scope sc;
    gene::Value v;
    CHECK(gene::evaluate(gene::parse("#ff8800", &err), sc, v, &err));
    CHECK(v.isColor);
    CHECK_NEAR(v.r, 1.0, 1e-9);
    CHECK_NEAR(v.g, 0x88 / 255.0, 1e-9);
    CHECK_NEAR(v.a, 1.0, 1e-9);

    CHECK(gene::evaluate(gene::parse("#f80", &err), sc, v, &err));
    CHECK_NEAR(v.g, 0x88 / 255.0, 1e-9);

    CHECK(gene::evaluate(gene::parse("#ff880040", &err), sc, v, &err));
    CHECK_NEAR(v.a, 0x40 / 255.0, 1e-9);

    CHECK(gene::evaluate(gene::parse("fade(#ffffff, 0.5)", &err), sc, v, &err));
    CHECK_NEAR(v.a, 0.5, 1e-9);

    CHECK(gene::evaluate(gene::parse("mix(#000000, #ffffff, 0.25)", &err), sc, v, &err));
    CHECK_NEAR(v.r, 0.25, 1e-9);

    CHECK(gene::evaluate(gene::parse("rgba(255, 0, 0, 128)", &err), sc, v, &err));
    CHECK_NEAR(v.a, 128 / 255.0, 1e-9);
    CHECK(gene::evaluate(gene::parse("rgb(255, 0, 0)", &err), sc, v, &err));
    CHECK_NEAR(v.a, 1.0, 1e-9);

    // Arithmetic on a colour is a type error, not a silent coercion.
    CHECK(!gene::evaluate(gene::parse("#ffffff * 2", &err), sc, v, &err));
    CHECK(!err.empty());
}
TEST(Gene_rejects_malformed_input_with_a_message)
{
    std::string err;
    CHECK(gene::parse("1 +", &err) == nullptr);
    CHECK(!err.empty());
    CHECK(gene::parse("min(1)", &err) == nullptr);            // wrong arity
    CHECK(err.find("takes 2") != std::string::npos);
    CHECK(gene::parse("nosuch(1)", &err) == nullptr);
    CHECK(err.find("unknown function") != std::string::npos);
    CHECK(gene::parse("#zz", &err) == nullptr);
    CHECK(gene::parse("1 2", &err) == nullptr);
    CHECK(gene::parse("(1", &err) == nullptr);
    CHECK(gene::parse("1 ? 2", &err) == nullptr);
    CHECK(gene::parse("a.", &err) == nullptr);
    CHECK(gene::parse("raw{ unclosed", &err) == nullptr);
    CHECK(gene::parse("", &err) == nullptr);
    CHECK(gene::parse("@", &err) == nullptr);
}
TEST(Gene_scope_lookup_and_unknown_names)
{
    gene::Scope sc;
    sc.w = 10;
    sc.h = 20;
    sc.lookupIdent = [](const std::string &n, gene::Value &v) {
        if (n == "speed") { v = gene::Value::number(2.0); return true; }
        return false;
    };
    sc.lookupMember = [](const std::string &o, const std::string &f, gene::Value &v) {
        if (o == "ring" && f == "w") { v = gene::Value::number(7.0); return true; }
        return false;
    };
    std::string err;
    gene::Value v;
    CHECK(gene::evaluate(gene::parse("speed * ring.w", &err), sc, v, &err));
    CHECK_NEAR(v.num, 14.0, 1e-12);
    CHECK(!gene::evaluate(gene::parse("nope", &err), sc, v, &err));
    CHECK(err.find("unknown name") != std::string::npos);
    CHECK(!gene::evaluate(gene::parse("ring.nope", &err), sc, v, &err));
    CHECK(err.find("unknown field") != std::string::npos);
}
TEST(Gene_raw_escape_is_exported_but_not_previewable)
{
    std::string err;
    gene::NodePtr n = gene::parse("raw{ myHelper(w) } + 1", &err);
    CHECK(n != nullptr);
    CHECK(gene::containsRaw(n));
    gene::Scope sc;
    gene::Value v;
    CHECK(!gene::evaluate(n, sc, v, &err));      // the interpreter cannot run C++
    CHECK(emit("raw{ myHelper(w) } + 1") == "(myHelper(w)) + 1.0");
}
TEST(Gene_collects_names_for_dependency_analysis)
{
    gene::NodePtr n = gene::parse("min(self.w, ring.h) * speed + base.value", nullptr);
    std::vector<std::pair<std::string, std::string>> members;
    gene::collectMembers(n, members);
    CHECK(members.size() == 3);
    std::vector<std::string> idents;
    gene::collectIdents(n, idents);
    CHECK(idents.size() == 1);
    CHECK(idents[0] == "speed");
    CHECK(!gene::functionNames().empty());
    CHECK(!gene::builtinIdents().empty());
}

// ───────────────────────── Gene: folding + C++ emission ─────────────────────────
TEST(Gene_folds_constants_so_the_generated_line_reads_cleanly)
{
    gene::NodePtr n = gene::fold(gene::parse("2 * 3 + 4", nullptr));
    CHECK(gene::isConstant(n));
    CHECK(emit("2 * 3 + 4") == "10.0");
    // A folded colour is still a colour.
    gene::NodePtr c = gene::fold(gene::parse("fade(#ffffff, 0.5)", nullptr));
    CHECK(gene::isConstant(c));
    // Nothing to fold when a name is involved.
    CHECK(!gene::isConstant(gene::fold(gene::parse("w * 2", nullptr))));
}
TEST(Gene_emits_minimum_parentheses_and_shortest_literals)
{
    CHECK(emit("w * 0.7") == "w * 0.7");                    // not 0.69999999999999996
    CHECK(emit("(w + h) * 2") == "(w + h) * 2.0");          // kept: needed
    CHECK(emit("w + h * 2") == "w + h * 2.0");              // dropped: precedence agrees
    CHECK(emit("w / 2") == "w / 2.0");                      // literal divisor: no zero guard
    CHECK(emit("minSide") == "std::min(w, h)");
    CHECK(emit("1 - w") == "1.0 - w");
}
TEST(Gene_emits_a_zero_guard_only_when_the_divisor_could_be_zero)
{
    // The interpreter defines x/0 as 0; the compiled code must agree, but only pays for
    // the test when the divisor is not a known non-zero literal.
    const std::string guarded = emit("w / h");
    CHECK(guarded.find("== 0.0 ? 0.0") != std::string::npos);
    CHECK(emit("w / 4").find("== 0.0") == std::string::npos);
}
TEST(Gene_emitted_comparisons_and_logic_yield_doubles)
{
    CHECK(emit("w < h") == "(w < h ? 1.0 : 0.0)");
    CHECK(emit("w && h") == "((w != 0.0) && (h != 0.0) ? 1.0 : 0.0)");
    CHECK(emit("w < h ? 1 : 2") == "(w < h ? 1.0 : 0.0) != 0.0 ? 1.0 : 2.0");
}
TEST(Gene_emitter_reports_unknown_names)
{
    CHECK(emit("mystery").find("unknown name") != std::string::npos);
    CHECK(emit("thing.field").find("unknown field") != std::string::npos);
}

// ───────────────────────── Theme + BaseCatalog ─────────────────────────
TEST(BaseCatalog_describes_every_authorable_base)
{
    CHECK(bases().size() == 5);
    for (const auto &b : bases())
    {
        CHECK(!b.name.empty());
        CHECK(!b.cppClass.empty());
        CHECK(!b.summary.empty());
        CHECK(!b.mustDraw.empty());
        // Every base carries the common signals and reads on top of its own.
        bool hasHover = false, hasResize = false;
        for (const auto &s : b.signals)
        {
            if (s.name == "hoverEnter") hasHover = true;
            if (s.name == "resize") hasResize = true;
        }
        CHECK(hasHover);
        CHECK(hasResize);
        bool readsHover = false;
        for (const auto &r : b.reads)
            if (r.name == "hover") readsHover = true;
        CHECK(readsHover);
    }
    CHECK(findBase("VisualLoop") != nullptr);
    CHECK(findBase("Nope") == nullptr);
    CHECK(isEasingName("EaseOutCubic"));
    CHECK(!isEasingName("EaseOutNope"));
    CHECK(easingNames().size() == 30);
}
TEST(Theme_roles_resolve)
{
    double r, g, b, a;
    CHECK(themeColor("accent", r, g, b, a));
    CHECK(!themeColor("chartreuse", r, g, b, a));
    CHECK(themeColors().size() >= 10);
}

// ───────────────────────── Document ─────────────────────────
TEST(Document_field_table_drives_everything)
{
    CHECK(findField("opacity") != nullptr);
    CHECK(findField("nope") == nullptr);
    CHECK(std::string(findField("w")->segmentProperty) == "width");
    CHECK(std::string(findField("strokeWidth")->segmentProperty).empty());  // a style value
    // cornerRadius is a rect-only field; fontSize is label-only.
    bool rectHasCorner = false, circleHasCorner = false, labelHasFont = false;
    for (const auto *f : fieldsFor(ShapeKind::Rect)) if (std::string(f->name) == "cornerRadius") rectHasCorner = true;
    for (const auto *f : fieldsFor(ShapeKind::Circle)) if (std::string(f->name) == "cornerRadius") circleHasCorner = true;
    for (const auto *f : fieldsFor(ShapeKind::Label)) if (std::string(f->name) == "fontSize") labelHasFont = true;
    CHECK(rectHasCorner);
    CHECK(!circleHasCorner);
    CHECK(labelHasFont);
    CHECK(PathCmd::argCount('C') == 6);
    CHECK(PathCmd::argCount('Z') == 0);
    CHECK(PathCmd::argCount('X') == -1);
    ShapeKind k;
    CHECK(parseShapeKind("circle", k) && k == ShapeKind::Circle);
    CHECK(!parseShapeKind("blob", k));
    CHECK(shapeKindName(ShapeKind::Path) == "path");
    Cancel c;
    CHECK(parseCancel("queue", c) && c == Cancel::Queue);
    CHECK(!parseCancel("nope", c));
    CHECK(cancelName(Cancel::IgnoreIfRunning) == "ignoreIfRunning");
}
TEST(Document_shape_fields_fall_back_to_defaults)
{
    Shape s;
    s.kind = ShapeKind::Rect;
    CHECK(s.field("w").empty());
    CHECK(s.effectiveField("w") == "w");        // the default expression
    s.setField("w", "42");
    CHECK(s.effectiveField("w") == "42");
    s.setField("w", "43");                       // overwrite, not duplicate
    CHECK(s.fields.size() == 1);
    CHECK(!s.isAnimated("opacity"));
    s.setAnimated("opacity", true);
    s.setAnimated("opacity", true);
    CHECK(s.animated.size() == 1);
    s.setAnimated("opacity", false);
    CHECK(s.animated.empty());
}
TEST(Document_starter_is_valid_and_exportable)
{
    const Document d = Document::starter("VisualLoop", "CoolVisualLoop");
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());
    CHECK(d.isExportable());
    CHECK(d.name == "CoolVisualLoop");
    CHECK(d.shapes.front().reactions.size() == 2);
    CHECK(d.shapes.front().reactions[0].steps.size() == 2);   // fade, THEN spin
}
TEST(Document_json_round_trips_byte_for_byte)
{
    const Document d = Document::starter("VisualLoop", "CoolVisualLoop");
    const std::string once = d.toJson().dump();
    std::string err;
    const Document back = Document::fromJson(Json::parse(once, &err), &err);
    CHECK(err.empty());
    const std::string twice = back.toJson().dump();
    CHECK(once == twice);                        // determinism: a save is stable
    CHECK(back.shapes.size() == d.shapes.size());
    CHECK(back.allReactions().size() == d.allReactions().size());
    CHECK(back.params.size() == d.params.size());
    CHECK(back.shapes[0].animated.size() == d.shapes[0].animated.size());
}
TEST(Document_lookup_and_child_queries)
{
    Document d = Document::starter("VisualLoop", "C");
    CHECK(d.findShape("ring") != nullptr);
    CHECK(d.findShape("nope") == nullptr);
    CHECK(d.shapeIndex("ring") == 0);
    CHECK(d.shapeIndex("nope") == -1);
    CHECK(d.findParam("accent") != nullptr);
    CHECK(d.childrenOf("").size() == 1);
    CHECK(d.uniqueShapeId("ring") == "ring2");
    CHECK(d.uniqueShapeId("fresh") == "fresh");
    Shape s;
    s.id = "ring";
    d.addShape(s);
    CHECK(d.shapes.size() == 2);
    CHECK(d.shapes[1].id == "ring2");            // addShape de-duplicates
}
TEST(Document_removing_a_shape_removes_descendants_and_their_tracks)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape child;
    child.id = "dot";
    child.parent = "ring";
    child.setAnimated("opacity", true);
    d.addShape(child);
    Reaction r;
    r.signal = "cycle";
    Step st;
    st.tracks.push_back({"dot.opacity", "", "1", "100", "0", "Linear", 0, false});
    r.steps.push_back(st);
    d.findShape("ring")->reactions.push_back(r);

    d.removeShape("ring");
    CHECK(d.shapes.empty());                     // the child went with its parent
    // The orphaned track went too: emitting it would reference a deleted member.
    for (const auto &sh : d.shapes)
        for (const auto &re : sh.reactions)
            for (const auto &st : re.steps)
                CHECK(st.tracks.empty());
}
TEST(Document_parent_reads_the_object_that_holds_this_one)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape dot;
    dot.id = "dot";
    dot.parent = "ring";
    dot.kind = ShapeKind::Circle;
    dot.setField("w", "parent.w / 4");        // sized against its container, unnamed
    dot.setField("h", "parent.h / 4");
    dot.setField("x", "(parent.w - self.w) / 2");
    dot.setField("y", "(parent.h - self.h) / 2");
    dot.setField("fill", "accent");
    d.addShape(dot);
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    rt.setSize(200, 200);
    rt.advance(0.0);
    // ring is minSide*0.7 = 140, so the dot is a quarter of that and centred in it.
    CHECK_NEAR(rt.segmentFor("ring")->width.value(), 140.0, 1e-6);
    CHECK_NEAR(rt.segmentFor("dot")->width.value(), 35.0, 1e-6);
    CHECK_NEAR(rt.segmentFor("dot")->x.value(), (140.0 - 35.0) / 2.0, 1e-6);

    const EmittedCode c = emitCpp(d);
    CHECK(c.ok());
    CHECK(c.source.find("ring_w / 4.0") != std::string::npos);   // resolved to the parent local
}
TEST(Document_parent_of_a_top_level_object_is_the_component)
{
    Document d = Document::starter("VisualLoop", "C");
    // The ring is top-level, so `parent` is the component: w and h, and nothing else.
    d.shapes.front().setField("w", "parent.w / 2");
    d.shapes.front().setField("h", "parent.h / 2");
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    rt.setSize(240, 120);
    rt.advance(0.0);
    CHECK_NEAR(rt.segmentFor("ring")->width.value(), 120.0, 1e-6);
    CHECK_NEAR(rt.segmentFor("ring")->height.value(), 60.0, 1e-6);

    // Any other field there is an error, named as such rather than silently zero.
    d.shapes.front().setField("w", "parent.opacity");
    bool named = false;
    for (const auto &diag : d.validate())
        if (diag.isError() && diag.message.find("parent.opacity") != std::string::npos) named = true;
    CHECK(named);
}
TEST(Document_a_cycle_through_parent_is_caught)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape dot;
    dot.id = "dot";
    dot.parent = "ring";
    dot.kind = ShapeKind::Circle;
    dot.setField("w", "parent.w");
    d.addShape(dot);
    d.findShape("ring")->setField("w", "dot.w");   // ring <- dot <- parent(ring)
    bool cycle = false;
    for (const auto &diag : d.validate())
        if (diag.isError() && diag.message.find("cycle") != std::string::npos) cycle = true;
    CHECK(cycle);
}
TEST(Document_current_reads_the_target_at_fire_time)
{
    Document d = Document::starter("VisualLoop", "C");
    d.findShape("ring")->reactions.clear();
    Reaction r;
    r.signal = "cycle";
    Step st;
    // "ten more than wherever it is" — firing repeatedly STEPS the value.
    st.tracks.push_back({"opacity", "", "current + 0.25", "100", "0", "Linear", 0, false});
    r.steps.push_back(st);
    d.findShape("ring")->reactions.push_back(r);
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.0, 1e-9);

    rt.fire("cycle");
    rt.advance(0.0);
    rt.advance(100.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.25, 1e-9);
    rt.fire("cycle");                       // relative, so it steps again from where it got to
    rt.advance(100.0);
    rt.advance(200.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.5, 1e-9);

    const EmittedCode c = emitCpp(d);
    CHECK(c.ok());
    // `current` compiles to the target property's live value, not a captured constant.
    CHECK(c.source.find("mRing->opacity.value() + 0.25") != std::string::npos);

    // It is NOT in scope in a field binding, where there is no target to speak of.
    Document bad = d;
    bad.findShape("ring")->setField("w", "current * 2");
    bool rejected = false;
    for (const auto &diag : bad.validate())
        if (diag.isError() && diag.message.find("current") != std::string::npos) rejected = true;
    CHECK(rejected);
}
TEST(Document_reactions_belong_to_their_object)
{
    const Document d = Document::starter("VisualLoop", "C");
    // Selecting an object is what scopes the panel, so a reaction has to live ON one.
    CHECK(d.shapes.front().reactions.size() == 2);
    CHECK(d.allReactions().size() == 2);
    for (const auto &pair : d.allReactions())
        CHECK(pair.first->id == "ring");

    // A bare target means "my own field"; a qualified one still reaches a sibling.
    std::string shape, field;
    Document::splitTarget("opacity", "ring", shape, field);
    CHECK(shape == "ring");
    CHECK(field == "opacity");
    Document::splitTarget("halo.opacity", "ring", shape, field);
    CHECK(shape == "halo");
    CHECK(field == "opacity");
}
TEST(Document_several_objects_can_react_to_one_signal)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape dot;
    dot.id = "dot";
    dot.kind = ShapeKind::Circle;
    dot.setField("fill", "accent");
    dot.setAnimated("opacity", true);
    Reaction r;
    r.signal = "loopStart";
    Step st;
    st.tracks.push_back({"opacity", "0", "1", "200", "0", "Linear", 0, false});
    r.steps.push_back(st);
    dot.reactions.push_back(r);
    d.addShape(dot);

    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    // A signal is a component-level event: both objects handle it, and the generated hook
    // starts both.
    int loopStartHandlers = 0;
    for (const auto &pair : d.allReactions())
        if (pair.second->signal == "loopStart") ++loopStartHandlers;
    CHECK(loopStartHandlers == 2);

    const EmittedCode c = emitCpp(d);
    CHECK(c.ok());
    CHECK(c.source.find("playRingLoopStartStep0();") != std::string::npos);
    CHECK(c.source.find("playDotLoopStartStep0();") != std::string::npos);

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    rt.loopStart();
    rt.advance(0.0);
    CHECK(rt.isRunning("ring", "loopStart"));
    CHECK(rt.isRunning("dot", "loopStart"));
    rt.advance(200.0);
    CHECK_NEAR(rt.segmentFor("dot")->opacity.value(), 1.0, 1e-9);
}
TEST(Document_duplicate_copies_the_subtree_and_its_reactions)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape child;
    child.id = "dot";
    child.parent = "ring";
    child.kind = ShapeKind::Circle;
    child.setField("fill", "accent");
    child.setAnimated("opacity", true);
    Reaction r;
    r.signal = "cycle";
    Step st;
    st.tracks.push_back({"opacity", "0", "1", "120", "0", "Linear", 0, false});
    r.steps.push_back(st);
    child.reactions.push_back(r);
    d.addShape(child);

    const std::string copy = d.duplicateShape("ring");
    CHECK(copy == "ring_copy");
    CHECK(d.shapes.size() == 4);                       // ring, dot, ring_copy, dot_copy
    CHECK(d.findShape("ring_copy") != nullptr);
    CHECK(d.findShape("dot_copy") != nullptr);
    // The inner parent link follows the copy rather than pointing back at the original.
    CHECK(d.findShape("dot_copy")->parent == "ring_copy");
    // The copy brings its own reactions, so it animates itself.
    CHECK(d.findShape("ring_copy")->reactions.size() == 2);
    CHECK(d.findShape("dot_copy")->reactions.size() == 1);
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    // A second copy is _copy2, a third _copy3 — the same numbering ids already use.
    CHECK(d.duplicateShape("ring") == "ring_copy2");
    CHECK(d.duplicateShape("ring") == "ring_copy3");
    CHECK(d.duplicateShape("nosuch").empty());

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));                          // and the whole thing still builds
}
TEST(Document_duplicate_remaps_only_targets_inside_the_copied_subtree)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape outside;
    outside.id = "halo";
    outside.kind = ShapeKind::Circle;
    outside.setField("fill", "accent");
    outside.setAnimated("opacity", true);
    d.addShape(outside);

    // ring's reaction drives BOTH itself and the outside object.
    Step st;
    st.tracks.push_back({"opacity", "0", "1", "100", "0", "Linear", 0, false});
    st.tracks.push_back({"halo.opacity", "0", "1", "100", "0", "Linear", 0, false});
    Reaction r;
    r.signal = "cycle";
    r.steps.push_back(st);
    d.findShape("ring")->reactions.push_back(r);

    d.duplicateShape("ring");
    const Shape *copy = d.findShape("ring_copy");
    CHECK(copy != nullptr);
    const Reaction &copied = copy->reactions.back();
    // Its own field stays bare (so it means the COPY), and the external target is untouched.
    CHECK(copied.steps[0].tracks[0].target == "opacity");
    CHECK(copied.steps[0].tracks[1].target == "halo.opacity");
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());
}
TEST(Document_migrates_a_legacy_top_level_reaction_list)
{
    // Documents written before reactions belonged to objects keep loading: each reaction goes
    // to the object its first track drives, which is the object it was always about.
    const std::string legacy = R"({
      "genesis": 1,
      "component": { "name": "Old", "base": "VisualLoop", "namespace": "app", "designSize": [80, 80] },
      "params": [],
      "shapes": [
        { "id": "ring", "type": "circle",
          "bind": { "w": "minSide", "h": "minSide", "stroke": "#ffffff", "opacity": "0" },
          "animated": ["opacity"] }
      ],
      "reactions": [
        { "on": "loopStart", "steps": [ [ { "target": "ring.opacity", "to": "1", "ms": 200,
                                            "easing": "Linear" } ] ] }
      ]
    })";
    std::string err;
    const Document d = Document::fromJson(Json::parse(legacy, &err), &err);
    CHECK(err.empty());
    CHECK(d.shapes.size() == 1);
    CHECK(d.shapes.front().reactions.size() == 1);      // moved onto the object it drives
    CHECK(d.allReactions().size() == 1);
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    // And it re-saves in the new shape: no top-level list.
    CHECK(d.toJson()["reactions"].isNull());
    CHECK(d.toJson()["shapes"].at(0)["reactions"].size() == 1);
}
TEST(Document_validate_catches_every_class_of_authoring_mistake)
{
    auto errorsOf = [](const Document &d) {
        int n = 0;
        for (const auto &diag : d.validate())
            if (diag.isError()) ++n;
        return n;
    };
    {   // unknown base
        Document d = Document::starter("VisualLoop", "C");
        d.base = "Nope";
        CHECK(errorsOf(d) > 0);
    }
    {   // duplicate ids
        Document d = Document::starter("VisualLoop", "C");
        d.shapes.push_back(d.shapes[0]);
        CHECK(errorsOf(d) > 0);
    }
    {   // unknown parent
        Document d = Document::starter("VisualLoop", "C");
        d.shapes[0].parent = "ghost";
        CHECK(errorsOf(d) > 0);
    }
    {   // unparsable expression
        Document d = Document::starter("VisualLoop", "C");
        d.shapes[0].setField("w", "1 +");
        CHECK(errorsOf(d) > 0);
    }
    {   // unknown name in an expression
        Document d = Document::starter("VisualLoop", "C");
        d.shapes[0].setField("w", "mystery * 2");
        CHECK(errorsOf(d) > 0);
    }
    {   // a binding cycle
        Document d = Document::starter("VisualLoop", "C");
        d.shapes[0].setField("w", "self.h");
        d.shapes[0].setField("h", "self.w");
        CHECK(errorsOf(d) > 0);
    }
    {   // a reaction on a signal the base does not have
        Document d = Document::starter("VisualLoop", "C");
        d.shapes.front().reactions[0].signal = "onFire";
        CHECK(errorsOf(d) > 0);
    }
    {   // a track targeting a field that was never marked animated
        Document d = Document::starter("VisualLoop", "C");
        d.shapes[0].setAnimated("opacity", false);
        CHECK(errorsOf(d) > 0);
    }
    {   // a track targeting a colour
        Document d = Document::starter("VisualLoop", "C");
        d.shapes.front().reactions[0].steps[0].tracks[0].target = "ring.fill";
        CHECK(errorsOf(d) > 0);
    }
    {   // a malformed target
        Document d = Document::starter("VisualLoop", "C");
        d.shapes.front().reactions[0].steps[0].tracks[0].target = "ghost.";
        CHECK(errorsOf(d) > 0);
    }
    {   // an unknown shape in a target
        Document d = Document::starter("VisualLoop", "C");
        d.shapes.front().reactions[0].steps[0].tracks[0].target = "ghost.opacity";
        CHECK(errorsOf(d) > 0);
    }
    {   // an unknown easing
        Document d = Document::starter("VisualLoop", "C");
        d.shapes.front().reactions[0].steps[0].tracks[0].easing = "EaseOutBanana";
        CHECK(errorsOf(d) > 0);
    }
    {   // a bad path command
        Document d = Document::starter("VisualLoop", "C");
        Shape p;
        p.id = "arc";
        p.kind = ShapeKind::Path;
        p.path.push_back({'M', {"0"}});          // M needs 2
        d.addShape(p);
        CHECK(errorsOf(d) > 0);
    }
    {   // animating a non-animatable field
        Document d = Document::starter("VisualLoop", "C");
        d.shapes[0].setAnimated("fill", true);
        CHECK(errorsOf(d) > 0);
    }
    {   // a param shadowing a built-in name
        Document d = Document::starter("VisualLoop", "C");
        Param p;
        p.name = "w";
        d.params.push_back(p);
        CHECK(errorsOf(d) > 0);
    }
}
TEST(Document_validate_warns_about_unhandled_expected_signals)
{
    Document d = Document::starter("VisualLoop", "C");
    for (auto &sh : d.shapes) sh.reactions.clear();
    bool warned = false;
    for (const auto &diag : d.validate())
        if (!diag.isError() && diag.message.find("loopStart") != std::string::npos)
            warned = true;
    CHECK(warned);
    CHECK(d.isExportable());     // it is a warning, not a blocker
}

// ───────────────────────── the emitter ─────────────────────────
TEST(Emitter_generates_the_expected_shape_of_class)
{
    const Document d = Document::starter("VisualLoop", "CoolVisualLoop");
    const EmittedCode c = emitCpp(d);
    CHECK(c.ok());
    CHECK(c.headerName == "CoolVisualLoop.h");
    CHECK(c.sourceName == "CoolVisualLoop.cpp");

    // It inherits the authored base and depends on artboard alone.
    CHECK(c.header.find("class CoolVisualLoop : public artboard::VisualLoop") != std::string::npos);
    CHECK(c.header.find("#include <artboard/artboard.h>") != std::string::npos);
    CHECK(c.header.find("genesis") != std::string::npos);        // provenance comment
    CHECK(c.header.find("#include \"Runtime.h\"") == std::string::npos);
    CHECK(c.source.find("genesis::") == std::string::npos);      // no Genesis runtime, at all

    // Params become a real API.
    CHECK(c.header.find("void setAccent(const artboard::Color &v);") != std::string::npos);
    CHECK(c.header.find("void setThickness(double v);") != std::string::npos);

    // The authored signals became hook overrides.
    CHECK(c.header.find("void onLoopStart() override;") != std::string::npos);
    CHECK(c.header.find("void onLoopEnd() override;") != std::string::npos);

    // The binding is a readable line, not machine soup.
    CHECK(c.source.find("std::min(w, h) * 0.7") != std::string::npos);
    // "then" is the completion callback of the previous step. Generated names carry the
    // OWNING object, because several objects can react to one signal.
    CHECK(c.source.find("playRingLoopStartStep1();") != std::string::npos);
    // An infinite track never chains.
    CHECK(c.source.find("every track in this step repeats forever") != std::string::npos);
}
TEST(Emitter_is_deterministic)
{
    const Document d = Document::starter("VisualLoop", "CoolVisualLoop");
    const EmittedCode a = emitCpp(d);
    const EmittedCode b = emitCpp(d);
    CHECK(a.header == b.header);
    CHECK(a.source == b.source);
}
TEST(Emitter_refuses_a_document_with_errors)
{
    Document d = Document::starter("VisualLoop", "C");
    d.shapes[0].setField("w", "1 +");
    const EmittedCode c = emitCpp(d);
    CHECK(!c.ok());
    CHECK(!c.error.empty());
}
TEST(Emitter_covers_every_base_shape_kind_and_param_type)
{
    for (const auto &b : bases())
    {
        Document d = Document::starter(b.name, "Gen" + b.name);
        // one of every shape kind
        Shape rect;
        rect.id = "box";
        rect.kind = ShapeKind::Rect;
        rect.setField("fill", "theme.card");
        rect.setField("cornerRadius", "6");
        rect.setAnimated("cornerRadius", true);
        const std::string boxId = d.addShape(rect);   // may be de-duplicated per base
        Shape path;
        path.id = "tick";
        path.kind = ShapeKind::Path;
        path.parent = boxId;
        path.setField("stroke", "theme.foreground");
        path.path.push_back({'M', {"0", "0"}});
        path.path.push_back({'L', {"self.w", "self.h"}});
        path.path.push_back({'Q', {"0", "0", "1", "1"}});
        path.path.push_back({'C', {"0", "0", "1", "1", "2", "2"}});
        path.path.push_back({'Z', {}});
        const std::string tickId = d.addShape(path);
        Shape label;
        label.id = "cap";
        label.kind = ShapeKind::Label;
        label.text = "{caption}";
        label.setField("fill", "theme.muted");
        label.setAnimated("fontSize", true);
        d.addShape(label);
        Param text;
        text.name = "caption";
        text.type = ParamType::Text;
        text.defaultExpr = "Loading";
        d.params.push_back(text);

        // a reaction on every one of this base's signals
        for (const auto &sd : b.signals)
        {
            Reaction r;
            r.signal = sd.name;
            r.cancel = sd.name == "cycle" ? Cancel::IgnoreIfRunning
                                          : (sd.name == "resize" ? Cancel::Queue : Cancel::Restart);
            Step st;
            st.tracks.push_back({boxId + ".cornerRadius", "0", "8", "120", "10", "EaseOutBack", 0, true});
            r.steps.push_back(st);
            bool already = false;
            for (const auto &pair : d.allReactions())
                if (pair.second->signal == sd.name) already = true;
            if (!already) d.findShape(boxId)->reactions.push_back(r);
        }

        for (const auto &diag : d.validate())
            CHECK(!diag.isError());
        const EmittedCode c = emitCpp(d);
        CHECK(c.ok());
        CHECK(c.header.find("public " + b.cppClass) != std::string::npos);
        CHECK(c.source.find("artboard::PathSegment") != std::string::npos);
        CHECK(c.source.find("artboard::LabelSegment") != std::string::npos);
        CHECK(c.source.find(".cubicTo(") != std::string::npos);
        CHECK(c.source.find("mCaption") != std::string::npos);
        CHECK(c.source.find("inputTransparent = true") != std::string::npos);
        CHECK(c.source.find("drawsBuiltInVisuals = false") != std::string::npos);
        CHECK(!tickId.empty());
    }
}
TEST(Emitter_layout_runs_every_frame_only_when_a_binding_reads_base_state)
{
    // The VisualLoop starter reads no base state: its layout only runs on resize.
    const EmittedCode a = emitCpp(Document::starter("VisualLoop", "Loop"));
    CHECK(a.ok());
    CHECK(a.source.find("if (resized)\n            layout(sizeMs);") != std::string::npos);

    // The ProgressIndicator starter's fill IS base.display, so it must track every frame.
    const EmittedCode b = emitCpp(Document::starter("ProgressIndicator", "Bar"));
    CHECK(b.ok());
    CHECK(b.source.find("layout(resized ? sizeMs : 0.0);") != std::string::npos);
    CHECK(b.source.find("displayValue()") != std::string::npos);
}

// ───────────────────────── the preview runtime ─────────────────────────
TEST(Runtime_builds_a_real_tree_of_the_authored_base)
{
    Runtime rt;
    std::string err;
    CHECK(rt.build(Document::starter("VisualLoop", "C"), &err));
    CHECK(err.empty());
    CHECK(rt.root() != nullptr);
    CHECK(rt.root()->childCount() == 1);
    CHECK(dynamic_cast<artboard::VisualLoop *>(rt.root()) != nullptr);
    CHECK(rt.segmentFor("ring") != nullptr);
    CHECK(rt.segmentFor("nope") == nullptr);
    CHECK(rt.shapeIdFor(rt.segmentFor("ring")) == "ring");
}
TEST(Runtime_refuses_a_document_with_errors)
{
    Document d = Document::starter("VisualLoop", "C");
    d.shapes[0].setField("w", "mystery");
    Runtime rt;
    std::string err;
    CHECK(!rt.build(d, &err));
    CHECK(!err.empty());
}
TEST(Runtime_evaluates_bindings_against_the_live_size)
{
    Runtime rt;
    std::string err;
    CHECK(rt.build(Document::starter("VisualLoop", "C"), &err));
    rt.setSize(200, 100);
    rt.advance(0.0);
    // ring.w = minSide * 0.7 = 70; ring.x = (w - self.w)/2 = 65
    CHECK_NEAR(rt.segmentFor("ring")->width.value(), 70.0, 1e-9);
    CHECK_NEAR(rt.segmentFor("ring")->x.value(), 65.0, 1e-9);

    // A LATER resize eases rather than snapping (R1) — and still lands on the new value.
    rt.setSize(400, 400);
    rt.advance(20.0);    // the ease is armed on this frame and starts from the current value
    rt.advance(80.0);
    const double midway = rt.segmentFor("ring")->width.value();
    CHECK(midway > 70.0);
    CHECK(midway < 280.0);                                          // interpolating, not jumping
    for (int i = 2; i <= 30; ++i)
        rt.advance(i * 20.0);
    CHECK_NEAR(rt.segmentFor("ring")->width.value(), 280.0, 1e-6);   // it reflowed, eased
}
TEST(Runtime_runs_the_authored_reaction_chain)
{
    Runtime rt;
    std::string err;
    CHECK(rt.build(Document::starter("VisualLoop", "CoolVisualLoop"), &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    artboard::Segment *ring = rt.segmentFor("ring");
    CHECK_NEAR(ring->opacity.value(), 0.0, 1e-9);

    rt.loopStart();
    CHECK(rt.loopRunning());
    rt.advance(0.0);
    CHECK(rt.isRunning("loopStart"));
    rt.advance(150.0);
    CHECK(ring->opacity.value() > 0.0);
    CHECK(ring->opacity.value() < 1.0);            // interpolating, not jumping
    rt.advance(300.0);
    CHECK_NEAR(ring->opacity.value(), 1.0, 1e-9);
    CHECK_NEAR(ring->rotation.value(), 0.0, 1e-9); // the spin starts on completion
    rt.advance(900.0);
    CHECK_NEAR(ring->rotation.value(), 3.14159265358979324, 1e-6);   // half a 1200ms turn

    rt.loopStop();
    rt.advance(1050.0);
    CHECK(ring->opacity.value() < 1.0);
    rt.advance(1200.0);
    CHECK_NEAR(ring->opacity.value(), 0.0, 1e-9);
}
TEST(Runtime_renders_the_authored_shapes_through_the_HAL)
{
    Runtime rt;
    std::string err;
    CHECK(rt.build(Document::starter("VisualLoop", "C"), &err));
    rt.setSize(120, 120);
    rt.advance(0.0);

    artboard::RecordingTarget t;
    rt.root()->render(t);
    CHECK(t.count(K::StrokePath) == 0);     // opacity 0: the ring is not drawn at all

    rt.loopStart();
    rt.advance(0.0);
    rt.advance(300.0);
    t.clear();
    rt.root()->render(t);
    CHECK(t.count(K::StrokePath) == 1);     // faded in: it draws
    CHECK(t.count(K::CubicTo) == 4);        // a circle is four cubics
}
TEST(Runtime_honours_cancellation_policies)
{
    auto make = [](Cancel policy) {
        Document d = Document::starter("VisualLoop", "C");
        for (auto &sh : d.shapes) sh.reactions.clear();
        Reaction r;
        r.signal = "cycle";
        r.cancel = policy;
        Step st;
        st.tracks.push_back({"ring.opacity", "0", "1", "200", "0", "Linear", 0, false});
        r.steps.push_back(st);
        d.findShape("ring")->reactions.push_back(r);
        return d;
    };
    {   // restart: a second fire supersedes the first and starts over
        Runtime rt;
        std::string err;
        CHECK(rt.build(make(Cancel::Restart), &err));
        rt.advance(0.0);
        rt.fire("cycle");
        rt.advance(100.0);
        CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.5, 1e-9);
        rt.fire("cycle");
        rt.advance(100.0);
        CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.0, 1e-9);   // restarted from 0
    }
    {   // ignoreIfRunning: the second fire is dropped
        Runtime rt;
        std::string err;
        CHECK(rt.build(make(Cancel::IgnoreIfRunning), &err));
        rt.advance(0.0);
        rt.fire("cycle");
        rt.advance(100.0);
        rt.fire("cycle");
        rt.advance(100.0);
        CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.5, 1e-9);   // unchanged
        rt.advance(200.0);
        CHECK(!rt.isRunning("cycle"));
    }
    {   // queue: the second fire runs after the first finishes
        Runtime rt;
        std::string err;
        CHECK(rt.build(make(Cancel::Queue), &err));
        rt.advance(0.0);
        rt.fire("cycle");
        rt.advance(100.0);
        rt.fire("cycle");
        rt.advance(200.0);
        CHECK(rt.isRunning("cycle"));      // the queued run took over
        rt.advance(300.0);
        CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 0.5, 1e-9);
    }
}
TEST(Runtime_a_binding_follows_the_field_it_reads_while_that_field_animates)
{
    /*  `self.w` means "my width", so while a reaction animates the width a binding reading it
     *  must FOLLOW, frame by frame. Two things had to be true for that: an owned field's value
     *  is its live animated value rather than its own binding's result, and layout has to run
     *  on any frame where it reads something that changes per frame (G-6a).
     */
    Document d = Document::starter("VisualLoop", "B");
    Shape *s = d.findShape("ring");
    s->kind = ShapeKind::Rect;
    s->setField("w", "minSide * 0.2");
    s->setField("h", "minSide * 0.2");
    s->setField("x", "w / 4 * 3 - self.w / 2");   // centred on three quarters across
    s->setField("y", "0");
    s->setField("fill", "accent");
    s->setField("opacity", "1");
    s->setAnimated("w", true);
    s->reactions.clear();
    Reaction r;
    r.signal = "loopStart";
    Step st;
    st.tracks.push_back({"w", "minSide * 0.2", "minSide * 0.8", "400", "0", "Linear", 0, false});
    r.steps.push_back(st);
    s->reactions.push_back(r);
    for (const auto &diag : d.validate())
        CHECK(!diag.isError());

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    rt.setSize(200, 200);
    rt.advance(0.0);
    rt.loopStart();
    rt.advance(0.0);

    artboard::Segment *seg = rt.segmentFor("ring");
    CHECK_NEAR(seg->width.value(), 40.0, 1e-6);
    CHECK_NEAR(seg->x.value(), 150.0 - 40.0 / 2, 1e-6);
    for (double t : {100.0, 200.0, 300.0, 400.0})
    {
        rt.advance(t);
        // No lag and no freeze: x is exactly what the CURRENT width implies, every frame.
        CHECK_NEAR(seg->x.value(), 150.0 - seg->width.value() / 2, 1e-6);
    }
    CHECK_NEAR(seg->width.value(), 160.0, 1e-6);
    CHECK_NEAR(seg->x.value(), 70.0, 1e-6);
}
TEST(Emitter_layout_runs_per_frame_when_a_binding_reads_an_animated_field)
{
    // Reading an animated field is a per-frame dependency, exactly like reading base.*.
    Document plain = Document::starter("VisualLoop", "P");
    plain.findShape("ring")->setField("x", "w / 2");   // reads nothing that moves
    const EmittedCode a = emitCpp(plain);
    CHECK(a.ok());
    CHECK(a.source.find("if (resized)\n            layout(sizeMs);") != std::string::npos);

    Document live = Document::starter("VisualLoop", "L");
    live.findShape("ring")->setField("x", "w / 4 * 3 - self.w / 2");
    live.findShape("ring")->setAnimated("w", true);
    const EmittedCode b = emitCpp(live);
    CHECK(b.ok());
    CHECK(b.source.find("layout(resized ? sizeMs : 0.0);") != std::string::npos);
    // And the owned field's local reads its LIVE value, not its binding.
    CHECK(b.source.find("mOwnRingW ?") != std::string::npos);

    // The base is ticked BEFORE the bindings are evaluated, or every dependent lags a frame.
    const size_t tick = b.source.find("artboard::VisualLoop::advance(nowMs);");
    const size_t bind = b.source.find("layout(resized ? sizeMs : 0.0);");
    CHECK(tick != std::string::npos);
    CHECK(bind != std::string::npos);
    CHECK(tick < bind);
}
TEST(Runtime_binding_stops_fighting_a_reaction_that_owns_the_field)
{
    Runtime rt;
    std::string err;
    CHECK(rt.build(Document::starter("VisualLoop", "C"), &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    rt.loopStart();
    rt.advance(0.0);
    rt.advance(300.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 1.0, 1e-9);
    // ring.opacity is BOUND to 0 and ANIMATED to 1. A resize must not snap it back.
    rt.setSize(240, 240);
    rt.advance(320.0);
    CHECK_NEAR(rt.segmentFor("ring")->opacity.value(), 1.0, 1e-9);
}
TEST(Runtime_live_params_retarget_the_bindings)
{
    Runtime rt;
    std::string err;
    CHECK(rt.build(Document::starter("VisualLoop", "C"), &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    double thickness = 0;
    CHECK(rt.paramNumber("thickness", thickness));
    CHECK_NEAR(thickness, 4.0, 1e-9);
    artboard::Color accent;
    CHECK(rt.paramColor("accent", accent));
    CHECK(!rt.paramNumber("accent", thickness));

    rt.setParamNumber("thickness", 12.0);
    for (int i = 1; i <= 20; ++i)
        rt.advance(i * 20.0);
    rt.loopStart();
    rt.advance(500.0);
    rt.advance(900.0);
    artboard::RecordingTarget t;
    rt.root()->render(t);
    bool sawThick = false;
    for (const auto &op : t.ops())
        if (op.kind == K::SetStroke && std::fabs(op.width - 12.0) < 1e-6) sawThick = true;
    CHECK(sawThick);
}
TEST(Runtime_drives_every_base_kind)
{
    {   // ProgressIndicator — its starter's fill tracks base.display
        Document d = Document::starter("ProgressIndicator", "P");
        Runtime rt;
        std::string err;
        CHECK(rt.build(d, &err));
        CHECK(rt.segmentFor("fill") != nullptr);
        rt.setSize(200, 10);
        rt.advance(0.0);
        rt.setProgress(1.0);
        for (int i = 1; i <= 60; ++i)
            rt.advance(i * 20.0);
        CHECK(rt.segmentFor("fill")->width.value() > 150.0);   // it followed base.display
        rt.setIndeterminate(true);
        rt.advance(1400.0);
    }
    {   // Button — press squashes the body and washes it, release springs back
        Document d = Document::starter("Button", "B");
        Runtime rt;
        std::string err;
        CHECK(rt.build(d, &err));
        rt.setSize(132, 36);
        rt.advance(0.0);
        rt.setPressed(true);
        for (int i = 1; i <= 8; ++i)
            rt.advance(i * 20.0);
        CHECK(rt.segmentFor("body")->scaleX.value() < 1.0);
        CHECK(rt.segmentFor("wash")->opacity.value() > 0.5);
        rt.setPressed(false);
        for (int i = 9; i <= 40; ++i)
            rt.advance(i * 20.0);
        CHECK_NEAR(rt.segmentFor("body")->scaleX.value(), 1.0, 1e-3);
        CHECK(rt.segmentFor("wash")->opacity.value() < 0.05);
        rt.setHovered(true);
        rt.advance(900.0);
        rt.setHovered(false);
        rt.advance(1000.0);
    }
    {   // Slider — the thumb pops on drag-start and settles on drag-end
        Document d = Document::starter("Slider", "S");
        Runtime rt;
        std::string err;
        CHECK(rt.build(d, &err));
        rt.setSize(220, 24);
        rt.advance(0.0);
        rt.setSliderValue(0.75);
        rt.advance(60.0);
        CHECK(rt.segmentFor("thumb")->scaleX.value() > 1.0);
        for (int i = 1; i <= 30; ++i)
            rt.advance(60.0 + i * 20.0);
        CHECK(rt.segmentFor("fill")->width.value() > 100.0);   // followed base.norm
    }
    {   // Checkbox — the tick fades in on check
        Document d = Document::starter("Checkbox", "K");
        Runtime rt;
        std::string err;
        CHECK(rt.build(d, &err));
        rt.setSize(22, 22);
        rt.advance(0.0);
        CHECK_NEAR(rt.segmentFor("tick")->opacity.value(), 0.0, 1e-9);
        rt.setChecked(true);
        for (int i = 1; i <= 20; ++i)
            rt.advance(i * 20.0);
        CHECK(rt.segmentFor("tick")->opacity.value() > 0.9);
        rt.setChecked(false);
        for (int i = 21; i <= 40; ++i)
            rt.advance(i * 20.0);
        CHECK(rt.segmentFor("tick")->opacity.value() < 0.1);
    }
}
TEST(Runtime_notes_unpreviewable_raw_escapes)
{
    Document d = Document::starter("VisualLoop", "C");
    d.shapes[0].setField("strokeWidth", "raw{ 3.0 }");
    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    CHECK(!rt.notes().empty());
}
TEST(Runtime_renders_paths_and_labels)
{
    Document d = Document::starter("VisualLoop", "C");
    Shape p;
    p.id = "tick";
    p.kind = ShapeKind::Path;
    p.setField("stroke", "theme.foreground");
    p.setField("w", "20");
    p.setField("h", "20");
    p.path.push_back({'M', {"0", "0"}});
    p.path.push_back({'L', {"self.w", "self.h"}});
    p.path.push_back({'Z', {}});
    d.addShape(p);
    Shape l;
    l.id = "cap";
    l.kind = ShapeKind::Label;
    l.text = "Loading";
    l.setField("fill", "theme.muted");
    d.addShape(l);

    Runtime rt;
    std::string err;
    CHECK(rt.build(d, &err));
    rt.setSize(120, 120);
    rt.advance(0.0);
    artboard::RecordingTarget t;
    rt.root()->render(t);
    CHECK(t.count(K::LineTo) == 1);
    CHECK(t.count(K::DrawText) == 1);
    bool sawLoading = false;
    for (const auto &op : t.ops())
        if (op.kind == K::DrawText && op.text == "Loading") sawLoading = true;
    CHECK(sawLoading);
}

// ───────────────────────── the verifier's comparison ─────────────────────────
TEST(Verifier_op_serialization_is_field_wise_and_stable)
{
    artboard::RecordingTarget t;
    t.pushLayer(0.5);
    t.setStroke(artboard::Color{1, 0, 0, 1}, 2.0);
    t.beginPath();
    t.moveTo(1, 2);
    t.drawText("hi", 3, 4, 12);
    t.popLayer();
    const std::string a = opsToText(t.ops());
    CHECK(a.find("pushLayer") != std::string::npos);
    CHECK(a.find("moveTo") != std::string::npos);
    CHECK(a.find("hi") != std::string::npos);
    CHECK(opsToText(t.ops()) == a);      // deterministic
}
TEST(Verifier_default_plan_exercises_the_base)
{
    const VerifyPlan p = VerifyPlan::defaultFor(Document::starter("VisualLoop", "C"));
    CHECK(!p.sampleMs.empty());
    bool hasStart = false, hasResize = false;
    for (const auto &e : p.events)
    {
        if (e.action == "start") hasStart = true;
        if (e.action == "resize") hasResize = true;
    }
    CHECK(hasStart);
    CHECK(hasResize);      // a component that only works at its design size fails R4
    const VerifyPlan b = VerifyPlan::defaultFor(Document::starter("Button", "B"));
    bool hasPress = false;
    for (const auto &e : b.events)
        if (e.action == "press") hasPress = true;
    CHECK(hasPress);
}
TEST(Verifier_reports_unavailable_rather_than_passing)
{
    VerifyConfig cfg;
    cfg.artboardInclude.clear();
    std::string why;
    CHECK(!cfg.usable(&why));
    CHECK(!why.empty());
    const VerifyResult r = verify(Document::starter("VisualLoop", "C"), VerifyPlan(), cfg);
    CHECK(!r.available);
    CHECK(!r.matched);                                        // never green when it did not run
    CHECK(r.summary().find("unavailable") != std::string::npos);
}

int main() { return mini::runAll(); }
