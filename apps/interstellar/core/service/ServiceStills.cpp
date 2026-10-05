/*
 *  interstellar_core — the stills gallery and the split-screen wipe (R-CLR-4, R-CLR-5).
 *
 *  `still grab` keeps what the monitor shows — a source's graded frame — and the grade it was made
 *  with: the picture as `<stem>.stills/<id>.png`, the grade as `<id>.grade` (EditParamsIO text, the
 *  node's own grade as the open version folds it, as `grade copy` takes it). The grade is a SNAPSHOT,
 *  never an authority (law 1): `still apply` writes it THROUGH Cosmo onto other sources exactly as
 *  `grade paste` does. Stills are references, so undo leaves them alone (like proxies).
 *
 *  `view wipe` splits the MONITOR between the picture and a reference — a still, or another version
 *  of the timeline at the playhead — vertically (left | right) or horizontally (top / bottom), the
 *  split at a share of the frame. Presentation only: renders and export-still never wipe.
 */
#include "ServiceInternal.h"
#include "Composite.h"
#include "Project.h"
#include "Versions.h"
#include "engine/EditParamsIO.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    std::string InterstellarService::stillsDir() const
    {
        const fs::path p(mModel.projectPath);
        return (p.parent_path() / (p.stem().string() + ".stills")).string();
    }

    bool InterstellarService::stillPicture(const NodeId &id, Raster &out)
    {
        // this session's grab, kept; else the file (a still from an earlier session)
        const auto it = mStillPictures.find(id);
        if (it != mStillPictures.end()) { out = it->second; return true; }
        const Still *s = mProject->still(id);
        if (!s || !mHost.frameSource) return false;
        std::unique_ptr<IFrameSource> src = mHost.frameSource();
        IFrameSource::Info info;
        if (!src || !src->open(resolvePath(s->file), info) || !src->frameAt(0, out) || out.empty()) return false;
        mStillPictures[id] = out;
        return true;
    }

    bool InterstellarService::stillCommand(const Command &c)
    {
        Project &P = *mProject;
        const std::string verb = specFor(c.kind)->verb;
        if (c.kind == CK::ViewWipe)
        {
            const std::string a = c.arg(0);
            if (a == "off") { mWipeRef.clear(); bumpFrame(); return true; }
            std::string ref;
            if (a.empty())
            {
                if (mWipeRef.empty()) return fail("view wipe: name a still or a timeline (or off)");
                ref = mWipeRef;   // only the split moves
            }
            else if (P.still(P.idForRef(a))) ref = "still:" + P.idForRef(a);
            else if (P.timeline(P.idForRef(a))) ref = "timeline:" + P.idForRef(a);
            else return fail("view wipe: no still or timeline named " + a);
            if (c.has("split"))
            {
                const std::string sp = c.flag("split");
                if (sp != "vertical" && sp != "horizontal") return fail("view wipe: --split is vertical or horizontal, got " + sp);
                mWipeVertical = sp == "vertical";
            }
            if (c.has("at"))
            {
                const std::string v = c.flag("at");
                char *end = nullptr;
                const double at = std::strtod(v.c_str(), &end);
                if (!end || *end || !(at >= 0.0 && at <= 1.0)) return fail("view wipe: --at is a share of the frame, 0 … 1, got " + v);
                mWipeAt = at;
            }
            mWipeRef = ref;
            bumpFrame();
            return true;
        }
        if (c.kind == CK::StillDelete)
        {
            const NodeId id = P.idForRef(c.arg(0));
            const Still *s = P.still(id);
            if (!s) return fail("still delete: no still named " + c.arg(0));
            std::error_code ec;
            fs::remove(resolvePath(s->file), ec);
            fs::remove(resolvePath(s->grade), ec);
            if (mWipeRef == "still:" + id) mWipeRef.clear();
            mStillPictures.erase(id);
            P.stills.erase(std::remove_if(P.stills.begin(), P.stills.end(), [&](const Still &x) { return x.id == id; }), P.stills.end());
            markDirty();
            bumpFrame();
            return true;
        }
        if (c.kind == CK::StillApply)
        {
            const Still *s = P.still(P.idForRef(c.arg(0)));
            if (!s) return fail("still apply: no still named " + c.arg(0));
            const Timeline *t = P.timeline(currentTimeline());
            if (t && t->pinned()) return fail("still apply: timeline " + t->name + " is pinned at " + t->pinCommit() + " (R-VER-3)");
            if (t && !t->base.empty())
                return fail("still apply: " + t->name + " is a version — it stores overrides, not grades; apply the still on its base");
            std::ifstream in(resolvePath(s->grade));
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            EditParams params;
            if (text.empty() || !deserializeParams(text, params)) return fail("still apply: " + s->name + "'s grade cannot be read from " + s->grade);
            std::vector<RackObj *> targets;
            for (size_t i = 1; i < c.args.size(); ++i)
            {
                RackObj *ro = P.rackObj(P.idForRef(c.args[i]));
                if (!ro) return fail("still apply: no rack node named " + c.args[i]);
                targets.push_back(ro);
            }
            if (targets.empty() && mRack.selectedNode() >= 0)
                if (RackObj *ro = P.rackObj(rackObjOfCosmo(mRack.selectedNode()))) targets.push_back(ro);
            if (targets.empty()) return fail("still apply: name the nodes to apply it to, or select one");
            std::string err;
            const auto fields = paramFields(params);
            for (RackObj *ro : targets)
            {
                const int node = cosmoNodeOf(ro->id);
                if (node < 0) return fail("still apply: " + ro->name + " is offline");
                if (!mRack.setParams(node, fields, err)) return fail("still apply: " + ro->name + ": " + err);
            }
            markDirty();
            bumpFrame();
            emit(Event(EK::ParamsChanged).with("address", "still apply").with("value", s->name).with("target", "rack"));
            return true;
        }

        // still grab [<source>] [--name <n>]
        if (mModel.projectPath.empty()) return fail("still grab: save the project first — stills live beside it");
        if (!mHost.writeImage) return fail("still grab: this build cannot write a picture");
        RackObj *ro = nullptr;
        double at = 0.0;
        Raster picture;
        if (!c.args.empty())
        {
            ro = P.rackObj(P.idForRef(c.arg(0)));
            if (!ro || ro->kind == "group" || ro->media.empty()) return fail("still grab: no source named " + c.arg(0));
            if (!captureFrame(ro->name, picture)) return false;   // Grade's monitor: its reference frame, graded
            at = ro->frame;
        }
        else
        {
            // the timeline's picture at the playhead, with the grade of the clip on top there
            ResolvedTimeline R;
            std::string err;
            if (!resolved(currentTimeline(), R, err)) return fail("still grab: " + err);
            int bestOrder = -1;
            std::map<NodeId, int> order;
            for (const auto &tr : R.tracks) if (!tr.audio()) order[tr.id] = tr.order;
            const double t = mModel.playhead;
            for (const auto &cl : R.clips)
                if (t >= cl.at - 1e-9 && t < cl.end() - 1e-9 && order.count(cl.track) && order[cl.track] > bestOrder)
                    if (RackObj *r = P.rackObj(cl.src))
                    {
                        bestOrder = order[cl.track];
                        ro = r;
                        at = cl.in + (t - cl.at) * cl.speed;
                    }
            if (!ro) return fail("still grab: no clip at the playhead — name a source to grab its frame");
            if (!captureFrame("", picture)) return false;
        }
        // its own grade as the open version folds it (overrides included), as grade copy takes it
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string source, err;
        if (!colourTreeFor(currentTimeline(), tree, idx, source, err)) return fail("still grab: " + err);
        if (!idx.count(ro->id)) return fail("still grab: " + ro->name + " is offline");
        EditParams own = tree[(size_t)idx[ro->id]].own;
        applyDeltas(own, gradeDeltas(P, currentTimeline(), ro->id));
        std::error_code ec;
        fs::create_directories(stillsDir(), ec);
        if (ec) return fail("still grab: cannot create " + stillsDir());
        Still s;
        s.id = P.freshId("st_");
        const std::string want = c.has("name") ? c.flag("name") : std::string();
        if (!want.empty() && P.nameIsTaken(want)) return fail("still grab: that name is already used: " + want);
        if (!want.empty() && !Project::nameIsLegal(want, err)) return fail("still grab: " + err);
        s.name = want.empty() ? P.freshName("still") : want;
        s.node = ro->id;
        s.at = std::max(0.0, at);
        const std::string png = (fs::path(stillsDir()) / (s.id + ".png")).string();
        const std::string grade = (fs::path(stillsDir()) / (s.id + ".grade")).string();
        if (!mHost.writeImage(png, picture, err)) return fail("still grab: " + err);
        std::ofstream g(grade);
        if (!g || !(g << serializeParams(own))) return fail("still grab: cannot write " + grade);
        s.file = relativePath(png);
        s.grade = relativePath(grade);
        mStillPictures[s.id] = picture;
        P.stills.push_back(s);
        markDirty();
        mOutput = s.id + " " + s.name + "\n";
        emit(Event(EK::Info).with("text", "still " + s.name + " grabbed from " + ro->name));
        return true;
    }

    void InterstellarService::applyWipe(Raster &out, const std::function<bool(const NodeId &, Raster &)> &versionFrame)
    {
        // R-CLR-5: the monitor split between its picture (A) and the reference (B)
        if (mWipeRef.empty() || out.empty()) return;
        Raster ref;
        if (mWipeRef.compare(0, 6, "still:") == 0)
        {
            if (!stillPicture(mWipeRef.substr(6), ref)) return;
        }
        else
        {
            const NodeId tl = mWipeRef.substr(9);
            if (!mProject->timeline(tl) || !versionFrame || !versionFrame(tl, ref)) return;
        }
        // B fitted into A's frame, as the composite fits a source (contain)
        Raster fitted;
        render::Layer L;
        L.src = &ref;
        L.fit = render::Fit::Contain;
        render::compose({L}, out.width, out.height, fitted);
        if (fitted.deep() != out.deep())
        {
            if (out.deep()) toDeep(fitted, fitted);
            else toShallow(fitted, fitted);
        }
        const int split = (int)std::lround(mWipeAt * (mWipeVertical ? out.width : out.height));
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x)
            {
                if ((mWipeVertical ? x : y) < split) continue;   // A keeps its side
                const size_t i = ((size_t)y * out.width + x) * 4;
                if (out.deep()) std::copy_n(&fitted.rgba16[i], 4, &out.rgba16[i]);
                else std::copy_n(&fitted.rgba[i], 4, &out.rgba[i]);
            }
    }
}
}
