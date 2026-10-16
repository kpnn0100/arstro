// solaris_host — AudioRing (R-DEV-9, R-DEV-5): the frames between the player and a sound server's realtime callback.
// Order kept; a short read is silence and ONE xrun — none before the writer has begun or after a flush; a writer
// with no room waits, and nothing is lost or reordered across two threads.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "AudioRing.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using arstro::solaris_host::AudioRing;

int main()
{
    int passed = 0;
    {
        AudioRing r;
        r.reset(2, 8);
        std::vector<float> out(2 * 4, -1.0f);
        assert(r.read(out.data(), 4) == 0 && r.xruns() == 0);     // nothing written yet: silence, not an xrun
        for (float v : out) assert(v == 0.0f);
        const float in[] = {1, 2, 3, 4, 5, 6};                    // three frames, L/R
        assert(r.write(in, 3) && r.filled() == 3);
        assert(r.read(out.data(), 2) == 2 && out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 4);
        assert(r.read(out.data(), 4) == 1 && out[0] == 5 && out[1] == 6 && out[2] == 0 && out[7] == 0); // short: silence after
        assert(r.xruns() == 1);                                    // … and one xrun
        r.write(in, 3);
        r.clear();                                                 // a flush: dropped, and no xrun for the silence after
        assert(r.read(out.data(), 4) == 0 && r.xruns() == 1);
        r.write(in, 1);
        assert(r.read(out.data(), 4) == 1 && r.xruns() == 2);      // written again: counted again
        std::printf("[PASS] AudioRing: order kept; a short read is silence and one xrun, none before the first write or after a flush\n");
        ++passed;
    }
    {
        // a writer with no room waits; across two threads every frame arrives once, in order
        AudioRing r;
        r.reset(1, 64);
        const int total = 64 * 200;
        std::vector<float> got;
        got.reserve(total);
        std::atomic<bool> done{false};
        std::thread reader([&] {
            std::vector<float> buf(32);
            while (!done.load() || r.filled() > 0)
            {
                const int n = r.read(buf.data(), 32);
                got.insert(got.end(), buf.begin(), buf.begin() + n);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
        std::vector<float> block(48);
        int k = 0;
        for (int w = 0; w < total / 48; ++w)
        {
            for (auto &v : block) v = (float)(k++);
            assert(r.write(block.data(), 48));
        }
        done.store(true);
        reader.join();
        assert((int)got.size() == k);
        bool ordered = true;
        for (int i = 0; i < k; ++i) ordered &= got[(size_t)i] == (float)i;
        assert(ordered);
        // a writer waiting on a dead stream gives up
        AudioRing full;
        full.reset(1, 4);
        const float four[] = {1, 2, 3, 4, 5};
        std::atomic<bool> alive{false};
        assert(!full.write(four, 5, &alive));
        std::printf("[PASS] AudioRing: a writer with no room waits; %d frames across two threads, each once and in order; a dead stream lets it go\n", k);
        ++passed;
    }
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
