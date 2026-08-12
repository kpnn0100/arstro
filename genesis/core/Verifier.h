/*
 *  Genesis — Verifier: proof that the preview is the code.
 *
 *  The editor interprets; the export compiles. Two implementations of one semantics
 *  drift, and a design tool that lies about the result is worthless. So Genesis does not
 *  ask you to trust it:
 *
 *      1. emit the .h/.cpp
 *      2. compile them, with a generated harness, against artboard_core
 *      3. drive BOTH the compiled class and the interpreter through the same signal script
 *      4. render both into an artboard::RecordingTarget at each sampled frame
 *      5. diff the op streams
 *
 *  Any divergence comes back as the exact op index, field, and frame. This is only
 *  possible because Artboard already has a deterministic, device-free render target.
 *
 *  Verification needs a C++ compiler at authoring time. Where there is none, `run()`
 *  reports `available == false` rather than silently passing — an unavailable check is
 *  never a green one.
 */
#pragma once
#include "Document.h"
#include <artboard/artboard.h>
#include <string>
#include <vector>

namespace genesis
{
    /** One thing done to the component while verifying. */
    struct VerifyEvent
    {
        double atMs = 0.0;
        std::string action;   // start stop progress indeterminate press release check slider hover signal
        double arg = 0.0;
        std::string name;     // for action == "signal"
    };

    struct VerifyPlan
    {
        double width = 0.0, height = 0.0;    // 0 = the document's design size
        std::vector<double> sampleMs;        // frames whose op stream is compared
        std::vector<VerifyEvent> events;

        /** The default plan for a base: exercises its expected signals and samples the
         *  transitions they drive, at the design size and at a resized one. */
        static VerifyPlan defaultFor(const Document &doc);
    };

    struct VerifyConfig
    {
        std::string compiler = "c++";
        std::string artboardInclude;   // .../Artboard/include
        std::string artboardSrc;       // .../Artboard/src
        std::string artboardLibDir;    // dir holding libartboard_core.a
        std::string workDir;           // scratch dir for the generated harness
        /** Fill from the build-time defaults baked in by CMake. */
        static VerifyConfig defaults();
        bool usable(std::string *why = nullptr) const;
    };

    struct VerifyDifference
    {
        double atMs = 0.0;
        int opIndex = 0;
        std::string field;
        std::string preview;
        std::string compiled;
    };

    struct VerifyResult
    {
        bool available = false;   // a compiler + artboard build were found and used
        bool matched = false;     // the streams agreed everywhere
        std::string error;        // why it could not run, or why it failed
        std::string compilerOutput;
        int comparedFrames = 0;
        int comparedOps = 0;
        std::vector<VerifyDifference> differences;
        std::string summary() const;
    };

    /** Run the full check. Never throws; a missing toolchain yields available == false. */
    VerifyResult verify(const Document &doc, const VerifyPlan &plan, const VerifyConfig &cfg);

    /** The op-stream text the two sides compare. Exposed for unit tests. */
    std::string opsToText(const std::vector<artboard::DrawOp> &ops);
}
