/*
 *  interstellar_core — AppModelCodec: the model as deterministic text (R-SVC-9).
 *
 *  The `stable` form is what proves two front ends agree: identical commands must produce
 *  byte-identical stable text, whatever each front end did in between and however many times it
 *  pumped. Fields excluded from it are properties of WHEN and WHERE the dump was taken rather
 *  than of the state — `revision`, `frameSeq`, `frameMs`. **Excluding a field to make a test
 *  pass is falsifying the test**, so the list changes only with a line of justification.
 */
#pragma once
#include "AppModel.h"
#include <string>

namespace arstro
{
namespace interstellar
{
    struct DumpOptions
    {
        bool stable = false;   // omit what is a property of when/where, not of the state
        bool json = false;
        bool resolved = false; // include every resolved address — large, so opt-in
    };

    std::string formatModel(const AppModel &m, const DumpOptions &o = {});
}
}
