/*
 *  interstellar_core — ParamRegistry: the parameter address space, and the router that decides
 *  which service owns a write (R-PARAM).
 *
 *  One dotted address space serves six consumers — `set`, the CLI, the API document, the
 *  expression language, the automation lane list and the UI panel builder — so there is one
 *  vocabulary and no translation layer for a typo to live in.
 *
 *      gr1.basic.exposure     a rack group's exposure       (Cosmo's EditParams.exposure)
 *      gr1.opacity            the grade weight              (Interstellar's, no filter segment)
 *      clp_a.geom.scale       a clip's scale
 *      v0.opacity             a track's opacity
 *      ac_push.value          an automation clip's output   (read-only)
 *      project.playhead       a project value               (read-only)
 *
 *  **The table is BUILT, not hand-written** (R-PARAM-3). Cosmo's leaf names come from the same
 *  `EditParamsIO` vocabulary its own codec uses, so a preset, a `.cmp`, a `cosmo-cc set` line
 *  and an expression all spell `exposure` identically (R-PARAM-4); Interstellar's come from the
 *  structs in Project.h. Cosmo's `--help` is the experiment already run for the alternative:
 *  its command names are generated and always right, while the argument hints beside them are
 *  hand-maintained and missing for 8 of 30.
 */
#pragma once
#include "Project.h"
#include "RackAccess.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class ParamRegistry
    {
    public:
        enum class Type { Float, Int, Bool, Enum, Ref };
        enum class Owner { Interstellar, Cosmo };
        enum class ObjKind { Rack, Clip, Track, AutoClip, Project };

        struct Entry
        {
            ObjKind obj = ObjKind::Rack;
            std::string filter;      // "basic" | "detail" | "geom" | "" for an object-level param
            std::string param;       // the leaf name
            Type type = Type::Float;
            std::string unit;        // "ev" "%" "px" "deg" "K" "s" "frames" ""
            double min = 0, max = 0, def = 0;
            bool automatable = true;
            bool bindable = true;
            bool readOnly = false;
            Owner owner = Owner::Interstellar;
            /** The key this maps to in the owner's own vocabulary — Cosmo's `EditParamsIO` name
             *  for a rack parameter, the struct field for an Interstellar one. */
            std::string key;

            /** `basic.exposure`, or `opacity` where there is no filter segment. */
            std::string suffix() const { return filter.empty() ? param : filter + "." + param; }
        };

        struct Resolved
        {
            const Entry *entry = nullptr;
            ObjKind obj = ObjKind::Rack;
            NodeId objectId;         // the .isp node id, or the Cosmo node id for a rack object
            std::string objectName;  // the bind name, as written
        };

        /** Every entry, for the API document and for a UI that builds its panels from the
         *  registry rather than from a hand-written list. Stable order. */
        static const std::vector<Entry> &all();

        /** Resolve a dotted address against a project and a rack. On failure fills `err` WITH
         *  THE NEAREST CANDIDATES — `gr1.basic.exposer` is a typo a human makes weekly and an
         *  address space several hundred names deep cannot be eyeballed (R-PARAM-5, and cosmo's
         *  D-59 is what happens when a wrong key reports success). */
        static bool resolve(const Project &p, const RackAccess *rack, const std::string &address,
                            Resolved &out, std::string &err);

        /** Every address that exists right now, spelled out with the real object names — what
         *  completion offers and what `api` prints. */
        static std::vector<std::string> addresses(const Project &p, const RackAccess *rack);

        static const char *typeName(Type t);
        static const char *ownerName(Owner o);
        static const char *objKindName(ObjKind k);
    };
}
}
