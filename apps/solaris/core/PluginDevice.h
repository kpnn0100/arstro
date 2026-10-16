#pragma once
/*
 *  solaris_core — another maker's plugin as a library Device (R-VST-6; docs/discussion.md Round 9).
 *
 *  A CONNECTION is one instance of the plugin (the host's, behind IPluginInstance) with what Solaris knows of it: its
 *  class, bundle and name, its parameters as a DeviceType (`p<id>`, numbers 0 … 1, the plugin's titles and units), and
 *  why it is offline when it is. The live connection outlives the engines that play it — an edit swaps the engine, not
 *  the plugin's process — and a render makes its own.
 *
 *  A PluginDevice is the thin Device one engine plays a connection through. The engine splits a block at every note,
 *  so the notes, the parameter changes and a reset are queued (fixed arrays: the audio thread never allocates) and go
 *  with the next block, at its start. It waits for the answer at most the block's own duration live — an engine
 *  block's at least — and as long as it takes in a render. Offline (never started, crashed, overran), an instrument is
 *  silent and an effect passes its input dry.
 */
#include "PluginHost.h"
#include "device/Device.h"
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct PluginConnection
    {
        std::string uid, bundle, name;
        bool instrument = false;
        int rate = 0, maxBlock = 128;
        std::unique_ptr<IPluginInstance> inst; // null: it could not be started (`failed` says why)
        std::string failed;
        DeviceType type;                       // "vst3": its parameters as p<id>, 0 … 1
        bool told = false;                     // the service said it went offline (once)

        bool offline() const { return !inst || inst->offline(); }
        std::string why() const { return inst ? inst->why() : failed; }
    };

    /** The DeviceType of a plugin: its parameters (or, offline, the `p<id>` names the song stores) as numbers 0 … 1. */
    void setPluginType(PluginConnection &c, const std::vector<std::string> &storedNames = {});
    /** `p<id>` → the id; false when the name is not one. */
    bool pluginParamId(const std::string &name, uint32_t &id);

    /** A Device that plays `c` — live (a budget of its block's duration) or for a render (it waits). */
    std::unique_ptr<Device> makePluginDevice(std::shared_ptr<PluginConnection> c, bool live);
}
}
