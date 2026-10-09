/*
 *  solaris_core — Bindings: what an address IS, and a project's formulas checked and ordered
 *  (R-AUTO-1…3).
 *
 *  `describeAddress` is the one answer to "is this a number a formula can drive, and what are its
 *  range, unit, label and owner?" — the `set` refusal, `auto create`'s range and name, the compile
 *  and `eval` all ask it, so they cannot disagree. Bindable: a strip's `gain` (dB) and `pan`, a
 *  send's `gain`, `project.masterGain`, and every numeric (non-choice) registry parameter of every
 *  device.
 *
 *  `orderBindings` parses every formula, resolves each name to an automation, an address (bound or
 *  not) or nothing, and orders the bindings so a link always reads one evaluated before it. A
 *  binding that cannot be read — a syntax error, an unknown name, a loop — is reported and left out
 *  (inert: the address keeps its own value), so a hand-edited file still opens; the service refuses
 *  such a binding before it is ever stored.
 */
#pragma once
#include "Formula.h"
#include "Project.h"
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct AddressSpec
    {
        enum Kind { StripGain, StripPan, SendGain, MasterGain, DeviceParam } kind = StripGain;
        std::string node, field;  // `dv_2`, `filter.cutoff`
        std::string owner;        // the strip's name (a device's: its rack's strip; the master's: "Master")
        std::string label, unit;  // "Cutoff", "Hz"
        double lo = 0, hi = 1, value = 0; // its range and its own (stored) value
        bool integer = false;
        bool masterRack = false;  // a device on the master's rack
        int paramIndex = -1;      // DeviceParam: the registry index
    };

    /** False + `err` (naming why) when `address` is not a number a formula can drive. */
    bool describeAddress(const Project &p, const std::string &address, AddressSpec &out, std::string &err);

    struct OrderedBinding
    {
        const Binding *binding = nullptr;
        AddressSpec spec;
        ParsedFormula formula;
        std::vector<std::string> reads; // the names it reads: automation ids and addresses
    };

    /** The project's bindings in evaluation order; the unreadable ones go to `problems`
     *  ("<address>: <why>"). */
    std::vector<OrderedBinding> orderBindings(const Project &p, std::vector<std::string> &problems);

    /** Would `formula` on `address` be accepted beside the project's other bindings? (`p` unchanged.) */
    bool checkBinding(const Project &p, const std::string &address, const std::string &formula, std::string &err);
}
}
