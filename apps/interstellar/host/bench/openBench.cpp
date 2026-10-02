/*
 *  interstellar_open_bench — how long each service pump takes while a project opens (D-6).
 *  Not a test. A window pumps once per frame on the UI thread; any pump longer than a frame is a
 *  visible stall in the loading screen.
 *
 *      interstellar_open_bench <project.isp>
 */
#include "HostFrameSource.h"
#include "InterstellarService.h"
#include "VideoFrameDecoder.h"
#include "core/ThreadBudget.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace arstro;
using namespace arstro::interstellar;
using Clock = std::chrono::steady_clock;

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    cosmo::ThreadBudget budget(50);
    InterstellarService::Host h;
    h.rackDecoder = [](std::shared_ptr<const FrameSelector> sel) {
        return std::unique_ptr<cosmo::IImageDecoder>(new interstellar_host::VideoFrameDecoder(std::move(sel)));
    };
    h.frameSource = [] { return std::unique_ptr<IFrameSource>(new interstellar_host::HostFrameSource()); };
    InterstellarService svc(budget, h);
    std::vector<std::string> during;
    svc.subscribe([&](const Event &e) { during.push_back(eventName(e.kind)); });
    struct Pump { double ms; std::string what; };
    std::vector<Pump> pumps;
    const auto t0 = Clock::now();
    auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::string err;
    auto t = Clock::now();
    svc.dispatchText(std::string("project open \"") + argv[1] + "\"", err);
    pumps.push_back({ms(t, Clock::now()), "dispatch project open"});
    double now = 0;
    while (svc.busy() && ms(t0, Clock::now()) < 120000)
    {
        during.clear();
        t = Clock::now();
        svc.pump(now += 16.0);
        std::string what;
        for (const auto &d : during) what += d + " ";
        pumps.push_back({ms(t, Clock::now()), what});
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    Raster r;
    t = Clock::now();
    svc.renderFrame(0.0, 860, r);
    pumps.push_back({ms(t, Clock::now()), "first monitor frame"});
    std::printf("open → idle %.0f ms, %zu pumps\n", ms(t0, Clock::now()), pumps.size());
    std::sort(pumps.begin(), pumps.end(), [](const Pump &a, const Pump &b) { return a.ms > b.ms; });
    for (size_t i = 0; i < pumps.size() && i < 6; ++i) std::printf("  %8.1f ms  %s\n", pumps[i].ms, pumps[i].what.c_str());
    return 0;
}
