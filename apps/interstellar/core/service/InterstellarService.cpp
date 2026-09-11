#include "InterstellarService.h"
#include "AppModelCodec.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        /** A path's file name without its extension — what a person calls the thing. */
        std::string baseName(const std::string &path)
        {
            std::string b = path;
            const auto slash = b.find_last_of("/\\");
            if (slash != std::string::npos) b = b.substr(slash + 1);
            const auto dot = b.find_last_of('.');
            if (dot != std::string::npos && dot > 0) b = b.substr(0, dot);
            return b;
        }
    }

    InterstellarService::InterstellarService(Hooks hooks) : mHooks(std::move(hooks))
    {
        refreshModel();
    }

    void InterstellarService::emit(Event::Kind k, const std::string &text, int a, int b, double ms)
    {
        Event e;
        e.kind = k;
        e.text = text;
        e.a = a;
        e.b = b;
        e.ms = ms;
        for (auto &s : mSinks) s(e);
    }

    bool InterstellarService::reject(const std::string &why)
    {
        // A rejection reaches BOTH the event stream and lastError, so a failure is inspectable
        // after the fact by a front end that was not listening (R-SVC-6).
        mModel.lastError = why;
        emit(Event::Kind::CommandRejected, why);
        return false;
    }

    bool InterstellarService::dispatchText(const std::string &line, std::string &err)
    {
        const Command c = parseCommand(line, err);
        if (!c.valid()) return err.empty() ? true : reject(err);
        return dispatch(c);
    }

    void InterstellarService::pump(double)
    {
        // Never blocks, and starts no thread on its own initiative: the caller drives the clock
        // (cosmo's R-SVC-6). With no async work yet, this only keeps the model current.
        refreshModel();
    }

    bool InterstellarService::setAddress(const std::string &address, const std::string &value,
                                         std::string &err)
    {
        ParamRegistry::Resolved r;
        if (!ParamRegistry::resolve(mProject, mHooks.rack, address, r, err)) return false;
        const auto &e = *r.entry;
        if (e.readOnly) { err = "'" + address + "' is read-only"; return false; }

        // One authority per parameter: a bound parameter is not writable, or the write would be
        // silently overwritten on the next frame (R-EVAL-1).
        if (mBindings.forTarget(address))
        {
            err = "'" + address + "' is driven by a binding — delete it first, or edit the expression";
            return false;
        }

        const double v = std::atof(value.c_str());
        if (!std::isfinite(v))
        {
            // A COMMAND carrying a bad number is refused, where a FILE would be repaired: nothing
            // legitimate sends one, and a refusal is debuggable (cosmo's D-36 split).
            err = "not a finite number: '" + value + "'";
            return false;
        }

        switch (r.obj)
        {
            case ParamRegistry::ObjKind::Rack:
            {
                if (e.owner == ParamRegistry::Owner::Interstellar)
                {
                    RackObj *ro = nullptr;
                    for (auto &x : mProject.rackObjs)
                        if (x.node == r.objectId) ro = &x;
                    if (!ro) { err = "no such rack object: " + address; return false; }
                    if (e.param == "opacity") { ro->opacity = v; return true; }
                    err = "unhandled rack parameter: " + address;
                    return false;
                }
                if (!mHooks.rack) { err = "no rack is embedded — `rack import <file.cmp>` first"; return false; }
                // The write goes THROUGH to the hosted Cosmo service, which is what makes an
                // Interstellar colour edit an edit to the Cosmo project (R-COSMO-3).
                return mHooks.rack->setParam(r.objectId, e.key, v, err);
            }
            case ParamRegistry::ObjKind::Track:
            {
                Track *t = mProject.track(r.objectId);
                if (!t) { err = "no such track"; return false; }
                if (e.param == "opacity") { t->opacity = v; return true; }
                if (e.param == "gain") { t->gain = v; return true; }
                if (e.param == "mute") { t->mute = v != 0; return true; }
                if (e.param == "blend") { t->blend = (Blend)(int)v; return true; }
                err = "unhandled track parameter: " + address;
                return false;
            }
            case ParamRegistry::ObjKind::Clip:
            {
                Clip *c = mProject.clip(r.objectId);
                if (!c) { err = "no such clip"; return false; }
                if (e.filter == "geom")
                {
                    Geom &g = c->geom;
                    if (e.param == "x") { g.x = v; return true; }
                    if (e.param == "y") { g.y = v; return true; }
                    if (e.param == "scale") { g.scale = v; return true; }
                    if (e.param == "rotation") { g.rotation = v; return true; }
                    if (e.param == "anchor.x") { g.anchorX = v; return true; }
                    if (e.param == "anchor.y") { g.anchorY = v; return true; }
                    if (e.param == "crop.x") { g.cropX = v; return true; }
                    if (e.param == "crop.y") { g.cropY = v; return true; }
                    if (e.param == "crop.w") { g.cropW = v; return true; }
                    if (e.param == "crop.h") { g.cropH = v; return true; }
                }
                if (e.param == "opacity") { c->opacity = v; return true; }
                if (e.param == "speed") { if (v == 0) { err = "speed=0"; return false; } c->speed = v; return true; }
                if (e.param == "at") { c->at = v; return true; }
                if (e.param == "in") { c->in = v; return true; }
                if (e.param == "out") { c->out = v; return true; }
                if (e.param == "blend") { c->blend = (Blend)(int)v; return true; }
                if (e.param == "fit") { c->fit = (Fit)(int)v; return true; }
                err = "unhandled clip parameter: " + address;
                return false;
            }
            case ParamRegistry::ObjKind::AutoClip:
            {
                AutoClip *a = mProject.autoClip(r.objectId);
                if (!a) { err = "no such automation clip"; return false; }
                if (e.param == "dur") { a->dur = v; return true; }
                err = "unhandled automation parameter: " + address;
                return false;
            }
            case ParamRegistry::ObjKind::Project:
                err = "'" + address + "' is read-only";
                return false;
        }
        err = "unhandled address: " + address;
        return false;
    }

    bool InterstellarService::applySet(const Command &c)
    {
        // Every field is applied or the whole command is refused, and the refusal names the
        // field. Partially applying a `set` is how a script ends up in a state no one wrote.
        for (const auto &kv : c.fields)
        {
            std::string err;
            ParamRegistry::Resolved r;
            if (!ParamRegistry::resolve(mProject, mHooks.rack, kv.first, r, err))
                return reject("set " + kv.first + "=" + kv.second + " — " + err);
        }
        for (const auto &kv : c.fields)
        {
            std::string err;
            if (!setAddress(kv.first, kv.second, err))
                return reject("set " + kv.first + "=" + kv.second + " — " + err);
            mProject.name.empty() ? void() : void();
            emit(Event::Kind::ParamChanged, kv.first, 0, 0, std::atof(kv.second.c_str()));
        }
        // Any parameter change can change what a frame looks like, and the cache key's parameter
        // hash only protects against a STALE READ — it does not free the bytes. Clearing is the
        // honest coarse answer until a per-layer range invalidation earns its keep.
        mCache.clear();
        mModel.dirty = true;
        refreshModel();
        return true;
    }

    bool InterstellarService::dispatch(const Command &c)
    {
        std::string err;
        switch (c.kind)
        {
            case Command::Kind::None: return true;

            case Command::Kind::ProjectNew:
            {
                mProject = Project();
                mProject.id = "prj_1";
                // The FILE NAME, not the path. A project name is a label a person reads in the
                // top bar, and a full path there is both wrong and wide enough to crowd out the
                // workspace switcher — found by looking at a shot.
                mProject.name = baseName(c.path);
                if (!c.field("fps").empty()) mProject.fps = std::atof(c.field("fps").c_str());
                const std::string res = c.field("res");
                if (!res.empty())
                {
                    const auto x = res.find('x');
                    if (x == std::string::npos) return reject("--res wants WxH, e.g. 3840x2160");
                    mProject.width = std::atoi(res.substr(0, x).c_str());
                    mProject.height = std::atoi(res.substr(x + 1).c_str());
                }
                mBindings.rebuild(mProject, err);
                mPath = c.path;
                emit(Event::Kind::ProjectOpened, c.path, 0, 0);
                refreshModel();
                return true;
            }
            case Command::Kind::ProjectOpen:
            {
                emit(Event::Kind::ProjectOpening, c.path);
                Project p;
                int repaired = 0;
                if (!p.load(c.path, err, &repaired)) return reject(err);
                mProject = p;
                mPath = c.path;
                if (mProject.name.empty() || mProject.name.find('/') != std::string::npos)
                    mProject.name = baseName(c.path);
                if (repaired)
                    emit(Event::Kind::Info,
                         "repaired " + std::to_string(repaired) + " non-finite value(s)");
                if (!mBindings.rebuild(mProject, err)) return reject(err);
                emit(Event::Kind::ProjectOpened, c.path, (int)mProject.tracks.size(),
                     (int)mProject.clips.size());
                refreshModel();
                return true;
            }
            case Command::Kind::ProjectSave:
            {
                const std::string path = c.path.empty() ? mPath : c.path;
                if (path.empty()) return reject("no path to save to");
                if (!mProject.save(path, err)) return reject(err);
                mPath = path;
                mModel.dirty = false;
                emit(Event::Kind::ProjectSaved, path);
                refreshModel();
                return true;
            }
            case Command::Kind::ProjectClose:
                mProject = Project();
                mPath.clear();
                mBindings.rebuild(mProject, err);
                emit(Event::Kind::ProjectClosed);
                refreshModel();
                return true;

            case Command::Kind::RackImport:
            {
                Embed e;
                e.id = mProject.freshId("emb_");
                e.name = mProject.freshName("rack");
                e.role = "rack";
                e.path = c.path;
                e.target = "cosmo:" + c.path;
                e.branch = c.field("branch", "main");
                e.writeBranch = c.field("write-branch", e.branch);
                if (mProject.rackEmbed())
                    return reject("this project already embeds a rack — one colour authority (R-COSMO-1)");
                mProject.embeds.push_back(e);
                // Give every rack node a bind name now, so an address exists before anyone
                // types one (R-PARAM-2).
                if (mHooks.rack)
                    for (const auto &n : mHooks.rack->nodes())
                        mProject.ensureRackObj(n.id, n.cosmoName);
                emit(Event::Kind::RackChanged, "imported " + c.path);
                refreshModel();
                return true;
            }
            case Command::Kind::RackNew:
            {
                Embed e;
                e.id = mProject.freshId("emb_");
                e.name = mProject.freshName("rack");
                e.role = "rack";
                e.target = "cosmo:new";
                if (mProject.rackEmbed()) return reject("this project already embeds a rack");
                mProject.embeds.push_back(e);
                emit(Event::Kind::RackChanged, "created an empty rack");
                refreshModel();
                return true;
            }
            case Command::Kind::RackPin:
            {
                Embed *e = mProject.rackEmbed();
                if (!e) return reject("no rack is embedded");
                const auto at = e->branch.find('@');
                e->branch = (at == std::string::npos ? e->branch : e->branch.substr(0, at)) + "@" + c.name;
                e->writeBranch.clear();   // a pin is read-only, so a write branch is meaningless
                emit(Event::Kind::RackPinned, e->branch);
                refreshModel();
                return true;
            }
            case Command::Kind::RackUnpin:
            {
                Embed *e = mProject.rackEmbed();
                if (!e) return reject("no rack is embedded");
                const auto at = e->branch.find('@');
                if (at != std::string::npos) e->branch = e->branch.substr(0, at);
                e->writeBranch = e->branch;
                emit(Event::Kind::RackChanged, "unpinned");
                refreshModel();
                return true;
            }
            case Command::Kind::RackAdd:
            {
                // R-RACK-7: a source's identity here is its `#rackobj` — an id, a bind name, a
                // media path and a reference-frame time. Its COLOUR is the hosted Cosmo
                // project's, and until that exists the colour is identity. This is what lets a
                // cut be made and rendered before P3 lands, without colour ever living here.
                if (c.paths.empty()) return reject("rack add needs one or more media paths");
                if (mHooks.rack && mHooks.rack->isPinned())
                    return reject("the rack is pinned at " + mHooks.rack->pinCommit() +
                                  " — it is read-only (R-COSMO-5)");
                int added = 0;
                for (const auto &path : c.paths)
                {
                    // A stable node id per source. With a hosted rack this is the Cosmo node id;
                    // without one it is Interstellar's own, and the seam does not care which.
                    const NodeId node = mProject.freshId("cn_");
                    // The bind name is derived from the FILE NAME, because that is what a user
                    // recognises — and it must be a legal identifier, which a filename is not
                    // (R-PARAM-2).
                    std::string base = path;
                    const auto slash = base.find_last_of("/\\");
                    if (slash != std::string::npos) base = base.substr(slash + 1);
                    const auto dot = base.find_last_of('.');
                    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
                    RackObj &ro = mProject.ensureRackObj(node, base);
                    ro.media = path;
                    // Ask the source what it is, now rather than at the first render, so an
                    // unopenable path reads as offline immediately instead of as an empty frame.
                    const IFrameSource::Info info = sourceInfo(path);
                    if (info.width <= 0)
                        emit(Event::Kind::Error, "cannot open " + path + " — it will read as offline");
                    else
                    {
                        emit(Event::Kind::RackChanged,
                             "added " + ro.name + " " + std::to_string(info.width) + "x" +
                                 std::to_string(info.height) + " " + canonicalNumber(info.fps) +
                                 "fps frames=" + std::to_string(info.frames));
                        ++added;
                    }
                }
                // Deliberately NOT refused when nothing could be read. Registering a source and
                // being able to DECODE one are different things: a project is a text document
                // that REFERENCES media (R-FMT-3), so a front end with no codec — or a file that
                // has moved — still gets a source it can relink, and the clip reads as offline
                // rather than as a stall (R-RACK-5). The first version of this refused, and it
                // broke the UI tests, which have no decoder and no need of one.
                if (added == 0)
                    emit(Event::Kind::Info,
                         "no media could be read; the source(s) are registered and read as offline");
                refreshModel();
                return true;
            }
            case Command::Kind::RackSelect:
            case Command::Kind::RackGroupNew:
            case Command::Kind::RackDuplicate:
            case Command::Kind::RackRename:
                // These need the hosted CosmoService's own command path (P3). Refused rather than
                // quietly ignored (R-SVC-6).
                if (!mHooks.rack) return reject("no rack is embedded — `rack import <file.cmp>` first");
                if (mHooks.rack->isPinned())
                    return reject("the rack is pinned at " + mHooks.rack->pinCommit() +
                                  " — it is read-only (R-COSMO-5)");
                return reject("not implemented yet: this rack operation needs the hosted "
                              "CosmoService's own command path (P3)");

            case Command::Kind::Set: return applySet(c);

            case Command::Kind::Get:
            {
                double v = 0;
                Evaluator ev(mProject, mHooks.rack, mAutomation, mBindings);
                if (!ev.staticValue(c.name, v))
                {
                    ParamRegistry::Resolved r;
                    ParamRegistry::resolve(mProject, mHooks.rack, c.name, r, err);
                    return reject(err.empty() ? ("cannot read " + c.name) : err);
                }
                emit(Event::Kind::Info, c.name + " = " + canonicalNumber(v));
                return true;
            }
            case Command::Kind::Eval:
            {
                std::string out;
                if (!evalAddress(c.name, c.ms, c.flag, out)) return reject(mModel.lastError);
                emit(Event::Kind::Info, out);
                return true;
            }

            case Command::Kind::TrackAdd:
            {
                const bool audio = c.field("kind", "video") == "audio";
                const int order = c.field("order").empty() ? -1 : std::atoi(c.field("order").c_str());
                if (!mTimeline.addTrack(audio, c.field("name"), order, err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "track added");
                refreshModel();
                return true;
            }
            case Command::Kind::ClipAdd:
            {
                if (c.field("track").empty()) return reject("clip add needs --track");
                if (c.field("src").empty()) return reject("clip add needs --src rack:<source>");
                // `--src rack:a` may name a source by its BIND NAME, which is what a user knows —
                // node ids are assigned by the writer and are not guessable (the first `rack add`
                // produced cn_1 and cn_3, because naming a source consumes an id too). It is
                // stored canonically as `rack:<node>` so the project text is unambiguous, and an
                // unknown source is REFUSED here rather than reading as offline three commands
                // later (R-SVC-6).
                std::string src = c.field("src");
                if (src.rfind("rack:", 0) == 0)
                {
                    const std::string ref = src.substr(5);
                    const RackObj *ro = mProject.rackObjForNode(ref);
                    if (!ro) ro = mProject.rackObj(ref);
                    if (!ro)
                    {
                        std::string known;
                        for (const auto &r : mProject.rackObjs)
                            known += (known.empty() ? "" : ", ") + r.name;
                        return reject("no such source: '" + ref + "'" +
                                      (known.empty() ? " — the rack is empty; `rack add <media>` first"
                                                     : " — the rack has: " + known));
                    }
                    src = "rack:" + ro->node;
                }
                const Clip *added = mTimeline.addClip(c.field("track"), src,
                                                      c.fieldNum("in", 0), c.fieldNum("out", 0),
                                                      c.fieldNum("at", 0), c.field("name"), err);
                if (!added) return reject(err);
                emit(Event::Kind::TimelineChanged, "clip added " + added->name);
                refreshModel();
                return true;
            }
            case Command::Kind::ClipTrim:
            {
                const bool head = !c.field("in").empty();
                if (head == !c.field("out").empty())
                    return reject("clip trim wants exactly one of --in or --out");
                if (!mTimeline.trim(c.name, head, c.fieldNum(head ? "in" : "out", 0), err))
                    return reject(err);
                emit(Event::Kind::TimelineChanged, "clip trimmed " + c.name);
                refreshModel();
                return true;
            }
            case Command::Kind::ClipSplit:
                if (!mTimeline.split(c.name, c.fieldNum("at", 0), err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "clip split " + c.name);
                refreshModel();
                return true;
            case Command::Kind::ClipMove:
                if (!mTimeline.move(c.name, c.fieldNum("at", 0), c.field("track"), err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "clip moved " + c.name);
                refreshModel();
                return true;
            case Command::Kind::ClipDelete:
                if (!mTimeline.remove(c.name, c.field("ripple") == "1", err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "clip deleted " + c.name);
                refreshModel();
                return true;
            case Command::Kind::ClipRoll:
                if (!mTimeline.roll(c.name, c.name2, c.fieldNum("by", 0), err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "edit rolled");
                refreshModel();
                return true;
            case Command::Kind::ClipSlip:
                if (!mTimeline.slip(c.name, c.fieldNum("by", 0), err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "clip slipped " + c.name);
                refreshModel();
                return true;
            case Command::Kind::TransitionAdd:
            {
                const std::string between = c.field("between");
                const auto comma = between.find(',');
                if (comma == std::string::npos) return reject("--between wants <clipA>,<clipB>");
                if (!mTimeline.addTransition(between.substr(0, comma), between.substr(comma + 1),
                                             c.field("kind", "dissolve"), c.fieldNum("dur", 0.5), err))
                    return reject(err);
                emit(Event::Kind::TimelineChanged, "transition added");
                refreshModel();
                return true;
            }
            case Command::Kind::MarkerAdd:
            {
                Marker m;
                m.id = mProject.freshId("mk_");
                m.name = mProject.freshName(c.field("name", "mark"));
                m.at = c.fieldNum("at", 0);
                m.note = c.field("note");
                mProject.markers.push_back(m);
                emit(Event::Kind::TimelineChanged, "marker added");
                refreshModel();
                return true;
            }
            case Command::Kind::Rename:
            {
                NodeId id;
                if (!mProject.idForName(c.name, id)) return reject("no such object: " + c.name);
                if (!mProject.rename(id, c.name2, err)) return reject(err);
                if (!mBindings.rebuild(mProject, err)) return reject(err);
                emit(Event::Kind::TimelineChanged, "renamed " + c.name + " to " + c.name2);
                refreshModel();
                return true;
            }

            case Command::Kind::AutoNew:
            {
                AutoClip a;
                a.id = mProject.freshId("ac_");
                std::string why;
                if (!Project::nameIsLegal(c.name, why)) return reject(why);
                if (mProject.nameIsTaken(c.name)) return reject("bind name already used: " + c.name);
                a.name = c.name;
                a.dur = c.fieldNum("dur", 1.0);
                if (!c.field("interp").empty() && !parseInterp(c.field("interp"), a.interp))
                    return reject("--interp wants linear|bezier|hold|step");
                Ease ease = Ease::Linear;
                if (!c.field("ease").empty() && !parseEase(c.field("ease"), ease))
                    return reject("--ease wants linear|easeIn|easeOut|easeInOut");
                // `--points 0=0,1=1` in the shape's own local time and unit space.
                const std::string pts = c.field("points");
                std::istringstream ps(pts);
                std::string tok;
                while (std::getline(ps, tok, ','))
                {
                    const auto eq = tok.find('=');
                    if (eq == std::string::npos) return reject("--points wants t=v,t=v");
                    AutoPoint p;
                    p.t = std::atof(tok.substr(0, eq).c_str()) * (a.dur > 0 ? a.dur : 1.0);
                    p.value = std::atof(tok.substr(eq + 1).c_str());
                    p.ease = ease;
                    a.points.push_back(p);
                }
                if (a.points.size() < 2) return reject("a shape needs at least two breakpoints");
                std::sort(a.points.begin(), a.points.end(),
                          [](const AutoPoint &x, const AutoPoint &y) { return x.t < y.t; });
                mProject.autoClips.push_back(a);
                emit(Event::Kind::AutomationChanged, "shape " + a.name + " created");
                refreshModel();
                return true;
            }
            case Command::Kind::AutoPointSet:
            {
                AutoClip *a = mProject.autoClip(c.name);
                if (!a) return reject("no such automation clip: " + c.name);
                const double at = c.fieldNum("at", 0);
                AutoPoint p;
                p.t = at;
                p.value = c.fieldNum("value", 0);
                if (!c.field("ease").empty() && !parseEase(c.field("ease"), p.ease))
                    return reject("--ease wants linear|easeIn|easeOut|easeInOut");
                auto it = std::find_if(a->points.begin(), a->points.end(),
                                       [&](const AutoPoint &x) { return std::fabs(x.t - at) < 1e-9; });
                if (it == a->points.end()) a->points.push_back(p);
                else *it = p;
                std::sort(a->points.begin(), a->points.end(),
                          [](const AutoPoint &x, const AutoPoint &y) { return x.t < y.t; });
                emit(Event::Kind::AutomationChanged, "breakpoint on " + a->name);
                refreshModel();
                return true;
            }
            case Command::Kind::AutoLinkAdd:
            {
                const AutoClip *shape = mProject.autoClip(c.name);
                if (!shape) return reject("no such automation clip: " + c.name);
                ParamRegistry::Resolved r;
                if (!ParamRegistry::resolve(mProject, mHooks.rack, c.name2, r, err)) return reject(err);
                if (!r.entry->automatable)
                    return reject("'" + c.name2 + "' cannot be automated: it is a " +
                                  std::string(ParamRegistry::typeName(r.entry->type)) +
                                  ", whose change is structural rather than continuous (R-AUTO-6)");
                if (mBindings.forTarget(c.name2))
                    return reject("'" + c.name2 + "' is driven by a binding — a parameter has ONE "
                                  "producer (R-BIND-5)");
                AutoLink l;
                l.clip = shape->id;
                l.target = c.name2;
                l.at = c.fieldNum("at", 0);
                l.dur = c.fieldNum("dur", 0);
                l.haveFromTo = !c.field("from").empty() || !c.field("to").empty() ||
                               c.field("scale").empty();
                l.from = c.fieldNum("from", 0);
                l.to = c.fieldNum("to", 1);
                l.scale = c.fieldNum("scale", 1);
                l.offset = c.fieldNum("offset", 0);
                if (!c.field("mode").empty() && !parseAutoMode(c.field("mode"), l.mode))
                    return reject("--mode wants absolute|add|multiply");
                l.scope = c.field("scope");
                if (!l.scope.empty())
                {
                    const Clip *sc = mProject.clip(l.scope);
                    if (!sc) return reject("no such clip to scope to: " + l.scope);
                    l.scope = sc->id;
                }
                l.fadeIn = std::atoi(c.field("fade-in", "0").c_str());
                l.fadeOut = std::atoi(c.field("fade-out", "0").c_str());
                if (!mAutomation.addLink(l, err)) return reject(err);
                emit(Event::Kind::AutomationChanged, shape->name + " -> " + c.name2);
                refreshModel();
                return true;
            }
            case Command::Kind::AutoUnlink:
            {
                const AutoLink *l = mProject.autoLink(c.name);
                if (!l) return reject("no such autolink: " + c.name);
                const NodeId id = l->id;
                mProject.autoLinks.erase(
                    std::remove_if(mProject.autoLinks.begin(), mProject.autoLinks.end(),
                                   [&](const AutoLink &x) { return x.id == id; }),
                    mProject.autoLinks.end());
                emit(Event::Kind::AutomationChanged, "unlinked " + c.name);
                refreshModel();
                return true;
            }
            case Command::Kind::AutoLanes:
            {
                for (const auto &lane : mAutomation.lanes(c.name))
                    emit(Event::Kind::Info,
                         "lane " + lane.address + " links=" + std::to_string(lane.links.size()));
                return true;
            }

            case Command::Kind::BindSet:
            {
                ParamRegistry::Resolved r;
                if (!ParamRegistry::resolve(mProject, mHooks.rack, c.name, r, err)) return reject(err);
                if (!r.entry->bindable) return reject("'" + c.name + "' cannot be bound (read-only)");
                auto hasLink = [&](const std::string &addr) {
                    for (const auto &l : mProject.autoLinks)
                        if (l.target == addr) return true;
                    return false;
                };
                if (!mBindings.set(mProject, c.name, c.text, hasLink, err)) return reject(err);
                auto it = std::find_if(mProject.bindings.begin(), mProject.bindings.end(),
                                       [&](const Binding &b) { return b.target == c.name; });
                if (it == mProject.bindings.end())
                {
                    Binding b;
                    b.id = mProject.freshId("bn_");
                    b.target = c.name;
                    b.expr = c.text;
                    mProject.bindings.push_back(b);
                }
                else it->expr = c.text;
                emit(Event::Kind::BindingChanged, c.name + " = " + c.text);
                refreshModel();
                return true;
            }
            case Command::Kind::BindDelete:
            {
                if (!mBindings.remove(c.name)) return reject("no binding on " + c.name);
                mProject.bindings.erase(
                    std::remove_if(mProject.bindings.begin(), mProject.bindings.end(),
                                   [&](const Binding &b) { return b.target == c.name; }),
                    mProject.bindings.end());
                emit(Event::Kind::BindingChanged, "deleted " + c.name);
                refreshModel();
                return true;
            }
            case Command::Kind::BindList:
                for (const auto &b : mBindings.bindings())
                {
                    std::string deps;
                    for (const auto &d : b.deps) deps += (deps.empty() ? "" : ", ") + d;
                    emit(Event::Kind::Info, b.target + " = " + b.expr + "   [" + deps + "]");
                }
                return true;

            case Command::Kind::Playhead:
            {
                double t = mModel.playhead;
                if (c.name == "next-cut") t = mTimeline.nextCut(t);
                else if (c.name == "prev-cut") t = mTimeline.prevCut(t);
                else if (!c.name.empty() && (c.name[0] == '+' || c.name[0] == '-'))
                    t += std::atof(c.name.c_str());
                else t = c.ms;
                mModel.playhead = std::max(0.0, t);
                mModel.playheadFrame = mProject.frameAt(mModel.playhead);
                emit(Event::Kind::PlayheadChanged, {}, (int)mModel.playheadFrame, 0, mModel.playhead);
                refreshModel();
                return true;
            }
            case Command::Kind::Play: mModel.playing = true; return true;
            case Command::Kind::Pause: mModel.playing = false; return true;

            case Command::Kind::Lint:
            {
                const auto findings = lint();
                for (const auto &f : findings)
                    emit(Event::Kind::LintFinding, f.address + " " + f.detail, 0, 0, f.at);
                emit(Event::Kind::Info, "lint: " + std::to_string(findings.size()) + " finding(s)");
                refreshModel();
                return true;
            }

            case Command::Kind::Render:
            {
                const std::string out = c.field("out");
                if (out.empty()) return reject("render needs --out");
                if (!mHooks.makeFrameWriter) return reject("this front end supplied no frame writer");
                auto writer = mHooks.makeFrameWriter(out);
                if (!writer) return reject("cannot create a writer for " + out);
                double from = 0, to = mProject.duration();
                const std::string range = c.field("range");
                if (!range.empty())
                {
                    const auto colon = range.find(':');
                    if (colon == std::string::npos) return reject("--range wants a:b");
                    from = std::atof(range.substr(0, colon).c_str());
                    to = std::atof(range.substr(colon + 1).c_str());
                }
                if (c.field("lint") == "1" || mProject.settings.lintOnRender)
                    for (const auto &f : lint())
                        emit(Event::Kind::LintFinding, f.address + " " + f.detail, 0, 0, f.at);
                const long long first = mProject.frameAt(from), last = mProject.frameAt(to);
                if (!writer->begin(out, mProject.width, mProject.height, mProject.fps,
                                   std::max(0LL, last - first)))
                    return reject("cannot open " + out);
                int done = 0;
                mModel.render.active = true;
                mModel.render.total = (int)std::max(0LL, last - first);
                mModel.render.outPath = out;
                for (long long f = first; f < last; ++f)
                {
                    Raster frame;
                    renderFrame(mProject.secondsAt(f), frame, -1);   // full resolution for a master
                    if (!writer->write(frame)) { mModel.render.failures++; break; }
                    if (++done % 24 == 0) emit(Event::Kind::RenderProgress, {}, done, mModel.render.total);
                    mModel.render.done = done;
                }
                writer->end();
                mModel.render.active = false;
                emit(Event::Kind::RenderFinished, out, done, mModel.render.failures);
                refreshModel();
                return true;
            }
            case Command::Kind::ExportStill:
            {
                const std::string out = c.field("out");
                if (out.empty()) return reject("export-still needs --out");
                if (!mHooks.makeFrameWriter) return reject("this front end supplied no frame writer");
                auto writer = mHooks.makeFrameWriter(out);
                if (!writer) return reject("cannot create a writer for " + out);
                Raster frame;
                renderFrame(c.field("at").empty() ? mModel.playhead : c.fieldNum("at", 0), frame, -1);
                if (!writer->begin(out, mProject.width, mProject.height, mProject.fps, 1) ||
                    !writer->write(frame) || !writer->end())
                    return reject("cannot write " + out);
                emit(Event::Kind::Info, "wrote " + out);
                return true;
            }

            case Command::Kind::SettingsSet:
                for (const auto &kv : c.fields)
                {
                    if (kv.first == "proxyEdge") mProject.settings.proxyEdge = std::atoi(kv.second.c_str());
                    else if (kv.first == "cpuPercent") mProject.settings.cpuPercent = std::atoi(kv.second.c_str());
                    else if (kv.first == "cacheBytes") mProject.settings.cacheBytes = std::atoll(kv.second.c_str());
                    else if (kv.first == "lintOnRender") mProject.settings.lintOnRender = kv.second == "1" || kv.second == "true";
                    else return reject("unknown setting: " + kv.first);
                }
                refreshModel();
                return true;

            // Front-end verbs: the service holds the state, the front end does the printing and
            // the waiting. `dispatch` never blocks (R-SVC-6), so `wait` is a no-op here.
            case Command::Kind::StatePrint:
            case Command::Kind::Api:
            case Command::Kind::Wait:
                return true;
            case Command::Kind::Quit:
                mQuit = true;
                return true;
        }
        return reject("unhandled command");
    }

    InterstellarService::OpenSource *InterstellarService::sourceFor(const std::string &media)
    {
        if (media.empty() || !mHooks.makeFrameSource) return nullptr;
        auto it = mSources.find(media);
        if (it != mSources.end()) return it->second.failed ? nullptr : &it->second;

        OpenSource os;
        os.source = mHooks.makeFrameSource();
        if (!os.source || !os.source->open(media, os.info))
        {
            // Remembered as FAILED rather than retried: a missing file would otherwise be
            // reopened once per frame of a scrub.
            os.failed = true;
            os.source.reset();
            mSources[media] = std::move(os);
            return nullptr;
        }
        auto &slot = mSources[media];
        slot = std::move(os);
        return &slot;
    }

    IFrameSource::Info InterstellarService::sourceInfo(const std::string &media)
    {
        if (const OpenSource *os = sourceFor(media)) return os->info;
        return {};
    }

    std::vector<LintFinding> InterstellarService::lint() const
    {
        Evaluator ev(mProject, mHooks.rack, mAutomation, mBindings);
        auto findings = mAutomation.lintBoundaries(
            [&](const std::string &addr, double &out) { return ev.staticValue(addr, out); });
        // A binding whose expression cannot resolve is a finding too, reported once.
        for (const auto &b : mBindings.bindings())
            for (const auto &d : b.deps)
            {
                double v = 0;
                ParamRegistry::Resolved r;
                std::string err;
                const bool known = ParamRegistry::resolve(mProject, mHooks.rack, d, r, err) ||
                                   mProject.autoClip(d.substr(0, d.find('.'))) != nullptr;
                if (!known) findings.push_back({b.target, "reads '" + d + "', which does not resolve", {}, 0, 2});
                (void)v;
            }
        for (const auto &c : mProject.clips)
            if (c.src.rfind("rack:", 0) != 0)
                findings.push_back({c.name, "src '" + c.src + "' is not a rack node", c.id, c.at, 1});
        return findings;
    }

    bool InterstellarService::evalAddress(const std::string &address, double t, bool explain,
                                          std::string &out) const
    {
        Evaluator ev(mProject, mHooks.rack, mAutomation, mBindings);
        double v = 0;
        std::vector<std::string> trace;
        if (!ev.value(address, t, v, explain ? &trace : nullptr))
        {
            ParamRegistry::Resolved r;
            std::string err;
            ParamRegistry::resolve(mProject, mHooks.rack, address, r, err);
            const_cast<AppModel &>(mModel).lastError = err.empty() ? ("cannot evaluate " + address) : err;
            return false;
        }
        std::ostringstream s;
        s << address << " = " << canonicalNumber(v) << "  (at t=" << canonicalTime(t) << ")";
        for (const auto &line : trace) s << "\n    " << line;
        out = s.str();
        return true;
    }

    bool InterstellarService::renderFrame(double t, Raster &out, int proxyEdge)
    {
        Evaluator ev(mProject, mHooks.rack, mAutomation, mBindings);
        const ResolvedValues rv = ev.resolve(t);
        const auto active = ev.activeAt(t);

        out.allocate(mProject.width, mProject.height, 0);
        if (active.empty()) return false;

        // `proxyEdge <= 0` means full resolution — what an export wants. Otherwise every layer is
        // graded at that long edge, which is what makes a scrub affordable (R-PLAY-2).
        const int edge = proxyEdge > 0 ? proxyEdge : (proxyEdge == 0 ? mProject.settings.proxyEdge : -1);

        std::vector<Raster> sources(active.size());
        std::vector<Composite::Layer> layers;
        layers.reserve(active.size());

        for (size_t i = 0; i < active.size(); ++i)
        {
            const auto &a = active[i];
            const std::string media = mProject.mediaForSrc(a.clip->src);
            const std::string node =
                a.clip->src.rfind("rack:", 0) == 0 ? a.clip->src.substr(5) : a.clip->src;

            // ── steps 4-5: the source's effective params, then the grade ────────────────────
            // The params come from the RACK — whole, never rebuilt field by field (R-COSMO-1a).
            // `false` means identity, and that is the fast path rather than a failure.
            arstro::EditParams params;
            const bool hasParams = mHooks.rack && mHooks.rack->effectiveParams(node, params);
            const uint64_t phash = ev.paramHash(*a.clip, rv);

            FrameCache::Key key;
            key.source = media;
            key.sourceFrame = a.sourceFrame;
            key.paramHash = phash;
            key.level = edge;

            if (!media.empty() && mCache.get(key, sources[i]))
            {
                // A hit is a frame this pipeline already produced for these exact inputs.
            }
            else if (OpenSource *os = sourceFor(media))
            {
                Raster decoded;
                if (os->source->frameAt(a.sourceFrame, decoded) && !decoded.empty())
                {
                    mGrade.render(decoded, params, hasParams, edge, sources[i]);
                    mCache.put(key, sources[i]);
                }
            }

            if (sources[i].empty())
            {
                // Offline reads as MISSING, never as a stall: a marked placeholder so the project
                // stays openable and the clip says what is wrong (R-RACK-5).
                sources[i].allocate(16, 16, 0);
                for (size_t p = 0; p < sources[i].rgba.size(); p += 4)
                {
                    sources[i].rgba[p + 0] = 60;
                    sources[i].rgba[p + 1] = 20;
                    sources[i].rgba[p + 2] = 20;
                    sources[i].rgba[p + 3] = 255;
                }
            }

            // ── step 6: geometry, from the RESOLVED table, so an automated or bound crop,
            //    scale or position is what actually gets drawn ────────────────────────────────
            Composite::Layer l;
            l.source = &sources[i];
            l.geom = a.clip->geom;
            auto pick = [&](const char *suffix, double fallback) {
                const std::string addr = a.clip->name + "." + suffix;
                return rv.has(addr) ? rv.get(addr) : fallback;
            };
            l.geom.x = pick("geom.x", l.geom.x);
            l.geom.y = pick("geom.y", l.geom.y);
            l.geom.scale = pick("geom.scale", l.geom.scale);
            l.geom.rotation = pick("geom.rotation", l.geom.rotation);
            l.geom.cropX = pick("geom.crop.x", l.geom.cropX);
            l.geom.cropY = pick("geom.crop.y", l.geom.cropY);
            l.geom.cropW = pick("geom.crop.w", l.geom.cropW);
            l.geom.cropH = pick("geom.crop.h", l.geom.cropH);
            l.fit = a.clip->fit;
            l.blend = a.clip->blend;
            l.opacity = pick("opacity", a.clip->opacity) *
                        (rv.has(a.track->name + ".opacity") ? rv.get(a.track->name + ".opacity")
                                                            : a.track->opacity);

            // ── step 7: the transition weight, computed ONCE in `activeAt` so the renderer and
            //    every other consumer agree about which clips are live and how much each
            //    contributes (R-CUT-4a). A dissolve is a LINEAR alpha ramp.
            l.opacity *= a.transitionWeight;
            layers.push_back(l);
        }

        Composite::compose(layers, mProject.width, mProject.height, out);
        mFrameSeq++;
        mModel.frameSeq = mFrameSeq;
        mModel.frameWidth = out.width;
        mModel.frameHeight = out.height;
        mModel.frameLayers = (int)layers.size();
        return true;
    }

    void InterstellarService::refreshModel()
    {
        AppModel m;
        m.revision = mModel.revision + 1;
        m.projectPath = mPath;
        m.projectName = mProject.name;
        m.dirty = mModel.dirty;
        m.workspace = mModel.workspace;
        m.fps = mProject.fps;
        m.width = mProject.width;
        m.height = mProject.height;
        m.duration = mProject.duration();
        m.playhead = mModel.playhead;
        m.playheadFrame = mProject.frameAt(mModel.playhead);
        m.playing = mModel.playing;
        m.settings = mProject.settings;
        m.lastError = mModel.lastError;
        m.render = mModel.render;
        m.frameSeq = mFrameSeq;
        m.frameWidth = mModel.frameWidth;
        m.frameHeight = mModel.frameHeight;
        m.frameLayers = mModel.frameLayers;
        // "memory is bounded" and "the cache works" are claims this project has learned not to
        // make without a number a front end can read back (R-NFR-5, R-CPU-4's lesson).
        m.cacheBytes = mCache.residentBytes();
        m.cacheEntries = (int)mCache.entries();
        m.cacheHits = mCache.hits();
        m.cacheMisses = mCache.misses();

        if (const Embed *e = mProject.rackEmbed())
        {
            m.rackPath = e->path;
            m.rackPinned = e->pinned();
            m.rackPinCommit = e->pinCommit();
        }
        // The rack list is built from the PROJECT's own `#rackobj` nodes, so sources are visible
        // before a Cosmo project is hosted (R-RACK-7) — without this a view could not list what
        // the user just added. A hosted rack then supplies the tree shape (depth, grouping,
        // bypass) for the nodes it knows.
        for (const auto &ro : mProject.rackObjs)
        {
            RackNodeModel r;
            r.node = ro.node;
            r.bindName = ro.name;
            r.cosmoName = ro.name;
            r.media = ro.media;
            r.gradeWeight = ro.opacity;
            for (const auto &c : mProject.clips)
                if (c.src == "rack:" + ro.node) r.referenced = true;
            auto it = mSources.find(ro.media);
            r.failed = ro.media.empty() || (it != mSources.end() && it->second.failed);
            if (mHooks.rack)
                for (const auto &n : mHooks.rack->nodes())
                    if (n.id == ro.node)
                    {
                        r.cosmoName = n.cosmoName;
                        r.depth = n.depth;
                        r.group = n.group;
                        r.bypass = n.bypass;
                        r.pending = n.pending;
                    }
            m.rack.push_back(r);
        }
        if (mHooks.rack) m.rackPinned = m.rackPinned || mHooks.rack->isPinned();

        for (const auto &t : mProject.tracks)
            m.tracks.push_back({t.id, t.name, t.audio, t.order, t.mute, t.opacity});
        std::sort(m.tracks.begin(), m.tracks.end(),
                  [](const TrackModel &a, const TrackModel &b) { return a.order < b.order; });

        for (const auto &c : mProject.clips)
        {
            ClipModel cm;
            cm.id = c.id;
            cm.name = c.name;
            cm.track = c.track;
            cm.src = c.src;
            cm.at = c.at; cm.in = c.in; cm.out = c.out;
            cm.speed = c.speed; cm.opacity = c.opacity;
            cm.duration = c.duration();
            if (c.src.rfind("rack:", 0) == 0)
            {
                const std::string node = c.src.substr(5);
                if (const RackObj *ro = mProject.rackObjForNode(node))
                {
                    cm.srcName = ro->name;
                    cm.media = ro->media;
                }
                // OFFLINE is now a real answer from the decoder rather than a guess about whether
                // a rack is attached: a clip whose media cannot be opened reads as missing, and
                // one whose media is fine does not (R-RACK-5).
                auto it = mSources.find(cm.media);
                cm.srcOffline = cm.media.empty() || (it != mSources.end() && it->second.failed);
            }
            m.clips.push_back(cm);
        }
        std::sort(m.clips.begin(), m.clips.end(),
                  [](const ClipModel &a, const ClipModel &b) { return a.at < b.at; });

        for (const auto &lane : mAutomation.lanes({}))
        {
            LaneModel lm;
            lm.address = lane.address;
            for (const auto *l : lane.links) lm.links.push_back(l->id);
            m.lanes.push_back(lm);
        }
        for (const auto &b : mBindings.bindings())
            m.bindings.push_back({b.target, b.expr, b.deps, b.broken, b.brokenWhy});

        for (const auto &f : lint()) m.lint.push_back(f.address + " " + f.detail);

        // Every resolved address at the playhead, so a number the renderer used can be read
        // back (R-EVAL-3).
        {
            Evaluator ev(mProject, mHooks.rack, mAutomation, mBindings);
            const ResolvedValues rv = ev.resolve(mModel.playhead);
            for (const auto &kv : rv.values) m.resolved.emplace_back(kv.first, kv.second);
        }
        mModel = std::move(m);
    }

    std::string InterstellarService::apiDocument(bool json) const
    {
        // GENERATED from the same tables the parser and the codec use, so it cannot describe a
        // command the app does not have and cannot omit one it does (R-SVC-10).
        std::ostringstream s;
        if (json)
        {
            auto q = [](const std::string &v) {
                std::string o = "\"";
                for (char c : v) { if (c == '"' || c == '\\') o += '\\'; o += c; }
                return o + "\"";
            };
            s << "{\n  \"app\": \"interstellar\",\n  \"commands\": [\n";
            const auto &specs = commandSpecs();
            for (size_t i = 0; i < specs.size(); ++i)
                s << "    {\"name\": " << q(specs[i].name) << ", \"args\": " << q(specs[i].args)
                  << ", \"what\": " << q(specs[i].what) << "}" << (i + 1 < specs.size() ? "," : "")
                  << '\n';
            s << "  ],\n  \"events\": [\n";
            for (int k = 0; k <= (int)Event::Kind::CommandRejected; ++k)
                s << "    " << q(eventName((Event::Kind)k))
                  << (k < (int)Event::Kind::CommandRejected ? "," : "") << '\n';
            s << "  ],\n  \"parameters\": [\n";
            const auto &all = ParamRegistry::all();
            for (size_t i = 0; i < all.size(); ++i)
            {
                const auto &e = all[i];
                s << "    {\"object\": " << q(ParamRegistry::objKindName(e.obj)) << ", \"address\": "
                  << q("<" + std::string(ParamRegistry::objKindName(e.obj)) + ">." + e.suffix())
                  << ", \"type\": " << q(ParamRegistry::typeName(e.type)) << ", \"unit\": "
                  << q(e.unit) << ", \"min\": " << canonicalNumber(e.min) << ", \"max\": "
                  << canonicalNumber(e.max) << ", \"default\": " << canonicalNumber(e.def)
                  << ", \"automatable\": " << (e.automatable ? "true" : "false")
                  << ", \"bindable\": " << (e.bindable ? "true" : "false")
                  << ", \"readOnly\": " << (e.readOnly ? "true" : "false")
                  << ", \"owner\": " << q(ParamRegistry::ownerName(e.owner)) << "}"
                  << (i + 1 < all.size() ? "," : "") << '\n';
            }
            s << "  ]\n}\n";
            return s.str();
        }

        s << "# interstellar — API\n\n## Commands\n\n";
        for (const auto &sp : commandSpecs())
            s << "    " << sp.name << ' ' << sp.args << "\n        " << sp.what << '\n';
        s << "\n## Events\n\n";
        for (int k = 0; k <= (int)Event::Kind::CommandRejected; ++k)
            s << "    " << eventName((Event::Kind)k) << '\n';
        s << "\n## Parameter address space\n\n";
        s << "    object     address                      type   unit   min      max      default  auto bind owner\n";
        for (const auto &e : ParamRegistry::all())
        {
            char line[256];
            std::snprintf(line, sizeof line, "    %-10s %-28s %-6s %-6s %-8s %-8s %-8s %-4s %-4s %s\n",
                          ParamRegistry::objKindName(e.obj), e.suffix().c_str(),
                          ParamRegistry::typeName(e.type), e.unit.c_str(),
                          canonicalNumber(e.min).c_str(), canonicalNumber(e.max).c_str(),
                          canonicalNumber(e.def).c_str(), e.automatable ? "yes" : "no",
                          e.bindable ? "yes" : "no", ParamRegistry::ownerName(e.owner));
            s << line;
        }
        return s.str();
    }
}
}
