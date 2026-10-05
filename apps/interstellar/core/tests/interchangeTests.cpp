/*
 *  interstellar_interchange_tests — L1 for the interchange formats (R-XCH-1..3): timecode with drop
 *  frame, and a cut written to EDL, FCPXML and OTIO and read back as the same cut. Text only.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "Interchange.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace arstro::interstellar::xch;

namespace
{
    bool has(const std::string &h, const std::string &n) { return h.find(n) != std::string::npos; }
    bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

    void test_timecode()
    {
        const TcRate r24 = TcRate::of(24.0), r2997 = TcRate::of(30000.0 / 1001.0), r23 = TcRate::of(24000.0 / 1001.0);
        assert(!r24.drop && r2997.drop && !r23.drop && r23.nominal == 24 && r2997.nominal == 30);
        assert(tcFromFrames(86400, r24) == "01:00:00:00");
        long long f = 0;
        assert(framesFromTc("01:00:00:00", r24, f) && f == 86400);
        // drop frame: the first minute boundary skips ;00 and ;01, the tenth does not
        assert(tcFromFrames(1800, r2997) == "00:01:00;02" && tcFromFrames(17982, r2997) == "00:10:00;00");
        assert(framesFromTc("01:00:00;00", r2997, f) && f == 107892);
        for (long long k = 0; k < 30 * 60 * 21; k += 7)
        {
            long long back = -1;
            assert(framesFromTc(tcFromFrames(k, r2997), r2997, back) && back == k);
        }
        assert(!framesFromTc("1:2", r24, f) && !framesFromTc("01:61:00:00", r24, f));
        std::printf("[PASS] timecode: NDF, drop-frame boundaries and a 21-minute round trip at 29.97\n");
    }

    XTimeline sample()
    {
        XTimeline t;
        t.name = "social30";
        t.fps = 24.0;
        t.width = 3840;
        t.height = 2160;
        XMedia a;
        a.path = "/footage/A001_C003 wide.mov";
        a.name = "A001_C003 wide";
        a.reel = "A001C003";
        a.tcStart = 36000.0;   // 10:00:00:00
        a.duration = 20.0;
        XMedia b;
        b.path = "/footage/B002.mov";
        b.name = "B002";
        b.duration = 10.0;
        XMedia bed;
        bed.path = "/audio/bed.wav";
        bed.name = "bed";
        bed.video = false;
        bed.audio = true;
        bed.duration = 30.0;
        t.media = {a, b, bed};
        auto clip = [](int media, const char *name, int track, bool audio, double at, double in, double out, double speed = 1.0) {
            XClip c;
            c.media = media; c.name = name; c.track = track; c.audio = audio; c.at = at; c.in = in; c.out = out; c.speed = speed;
            return c;
        };
        t.clips.push_back(clip(0, "A001_C003 wide", 1, false, 0.0, 1.0, 3.0));          // 0
        t.clips.push_back(clip(1, "B002", 1, false, 2.0, 0.0, 2.0));                    // 1, dissolved into
        t.clips.push_back(clip(1, "B002", 1, false, 5.0, 4.0, 6.0, 2.0));               // 2, 2× — 1 s on the record
        t.clips.push_back(clip(0, "A001_C003 wide", 2, false, 1.0, 5.0, 6.0));          // 3, V2 over V1
        t.clips.push_back(clip(2, "bed", 1, true, 0.0, 0.0, 6.0));                      // 4, A1
        t.clips[4].gainDb = -6.0;
        t.transitions.push_back({0, 1, 0.5});
        return t;
    }

    /** The same cut: every clip found again by track and record position, with its span and speed. */
    void sameCut(const XTimeline &a, const XTimeline &b, bool video1Only, bool tcAbsolute, double tol = 1e-3)
    {
        for (size_t i = 0; i < a.clips.size(); ++i)
        {
            const XClip &x = a.clips[i];
            if (video1Only && (x.audio || x.track != 1)) continue;
            bool found = false;
            for (const XClip &y : b.clips)
            {
                if (y.audio != x.audio || y.track != x.track || !near(y.at, x.at, tol)) continue;
                const double base = tcAbsolute ? a.media[(size_t)x.media].tcStart : 0.0;
                if (!(near(y.in - base, x.in, tol) && near(y.out - base, x.out, tol) && near(y.speed, x.speed, 1e-3)))
                    std::printf("  clip %zu: in %.4f/%.4f out %.4f/%.4f speed %.4f/%.4f\n", i, y.in - base, x.in, y.out - base, x.out, y.speed, x.speed);
                assert(near(y.in - base, x.in, tol) && near(y.out - base, x.out, tol) && near(y.speed, x.speed, 1e-3));
                found = true;
            }
            if (!found) std::printf("  missing clip %zu (track %d at %.3f)\n", i, x.track, x.at);
            assert(found);
        }
    }

    void test_edl()
    {
        const XTimeline t = sample();
        const std::string edl = writeEdl(t, 1);
        assert(has(edl, "TITLE: social30") && has(edl, "FCM: NON-DROP FRAME"));
        assert(has(edl, "001  A001C003 V     C        10:00:01:00 10:00:03:00 01:00:00:00 01:00:02:00"));
        // the dissolve: the outgoing held at the cut, then the incoming over 12 frames
        assert(has(edl, "002  A001C003 V     C        10:00:03:00 10:00:03:00 01:00:02:00 01:00:02:00"));
        assert(has(edl, "002  B002     V     D    012 00:00:00:00 00:00:02:00 01:00:02:00 01:00:04:00"));
        assert(has(edl, "M2   B002     048.0") && has(edl, "* FROM CLIP NAME: A001_C003 wide") && has(edl, "* FROM CLIP: file:///footage/A001_C003%20wide.mov"));
        assert(has(edl, "* TO CLIP NAME: B002") && has(edl, "* TO CLIP: file:///footage/B002.mov"));   // the dissolve's incoming
        XTimeline back;
        std::string err;
        assert(readEdl(edl, 24.0, back, err));
        assert(back.name == "social30" && back.clips.size() == 3 && back.transitions.size() == 1 && near(back.transitions[0].dur, 0.5));
        assert(back.media[0].path == "/footage/A001_C003 wide.mov" && back.media[0].reel == "A001C003" && back.media[0].tcStart < 0);
        assert(back.media.size() == 2 && back.media[(size_t)back.clips[1].media].path == "/footage/B002.mov");   // the incoming is B, by its TO comments
        sameCut(t, back, true, true);   // EDL source times are absolute timecode until the file is read
        // a 29.97 EDL is drop-frame and reads back to the same frames
        XTimeline n = t;
        n.fps = 30000.0 / 1001.0;
        const std::string df = writeEdl(n, 1);
        assert(has(df, "FCM: DROP FRAME") && has(df, "01:00:00;00"));
        assert(readEdl(df, n.fps, back, err) && back.clips.size() == 3);
        assert(!readEdl("TITLE: x\n001  AX V C 01:00 bad\n", 24.0, back, err) && has(err, "line 2"));
        std::printf("[PASS] EDL: CMX 3600 events, reels, a dissolve, M2 speed, clip/file comments; read back the same cut (NDF and DF)\n");
    }

    void test_fcpxml()
    {
        const XTimeline t = sample();
        const std::string x = writeFcpxml(t);
        assert(has(x, "<fcpxml version=\"1.9\">") && has(x, "frameDuration=\"1/24s\"") && has(x, "tcStart=\"86400/24s\""));
        assert(has(x, "src=\"file:///footage/A001_C003%20wide.mov\"") && has(x, "<transition name=\"Cross Dissolve\""));
        assert(has(x, "lane=\"1\"") && has(x, "lane=\"-1\"") && has(x, "<timeMap>"));
        XTimeline back;
        std::string err;
        assert(readFcpxml(x, back, err));
        assert(back.fps == 24.0 && back.width == 3840 && near(back.recordStart, 3600.0) && back.name == "social30");
        assert(back.media.size() == 3 && back.media[0].path == "/footage/A001_C003 wide.mov" && near(back.media[0].tcStart, 36000.0));
        assert(back.transitions.size() == 1 && near(back.transitions[0].dur, 0.5));
        sameCut(t, back, false, false);
        bool gain = false;
        for (const auto &c : back.clips) gain = gain || (c.audio && near(c.gainDb, -6.0));
        assert(gain && has(x, "<adjust-volume amount=\"-6.0dB\"/>"));
        // a <clip> wrapping its <video> (as OTIO writes FCPXML) is one clip, not two
        const std::string wrapped = "<fcpxml version=\"1.8\"><resources><format id=\"r1\" frameDuration=\"1/24s\"/>"
                                    "<asset id=\"r2\" name=\"a\" src=\"file:///a.mov\" start=\"0s\" duration=\"10s\" hasVideo=\"1\"/></resources>"
                                    "<project name=\"p\"><sequence format=\"r1\"><spine><clip name=\"a\" offset=\"0s\" duration=\"2s\" start=\"1s\">"
                                    "<video offset=\"0s\" ref=\"r2\" duration=\"10s\"/></clip></spine></sequence></project></fcpxml>";
        assert(readFcpxml(wrapped, back, err) && back.clips.size() == 1 && near(back.clips[0].in, 1.0) && near(back.clips[0].out, 3.0));
        // 23.976: NTSC frame durations
        XTimeline n = t;
        n.fps = 24000.0 / 1001.0;
        assert(has(writeFcpxml(n), "frameDuration=\"1001/24000s\""));
        assert(readFcpxml(writeFcpxml(n), back, err) && near(back.fps, 24000.0 / 1001.0, 1e-9));
        sameCut(n, back, false, false, 0.5 / n.fps);   // these seconds are not whole frames at 23.976: within half a frame
        assert(!readFcpxml("<xml/>", back, err) && has(err, "<fcpxml>"));
        std::printf("[PASS] FCPXML 1.9: assets with media-rep, spine + gaps, connected lanes (audio below), dissolve, timeMap speed; read back the same cut\n");
    }

    void test_otio()
    {
        const XTimeline t = sample();
        const std::string j = writeOtio(t);
        assert(has(j, "\"OTIO_SCHEMA\": \"Timeline.1\"") && has(j, "\"Clip.1\"") && has(j, "\"Gap.1\"") && has(j, "\"SMPTE_Dissolve\"") && has(j, "\"LinearTimeWarp.1\""));
        assert(has(j, "\"kind\": \"Audio\"") && has(j, "file:///footage/A001_C003%20wide.mov"));
        XTimeline back;
        std::string err;
        assert(readOtio(j, back, err));
        assert(back.fps == 24.0 && near(back.recordStart, 3600.0) && back.transitions.size() == 1 && back.width == 3840);
        sameCut(t, back, false, false);
        bool gain = false;
        for (const auto &c : back.clips) gain = gain || (c.audio && near(c.gainDb, -6.0));
        assert(gain);
        assert(!readOtio("{\"OTIO_SCHEMA\": \"Clip.1\"}", back, err) && has(err, "not an OpenTimelineIO timeline"));
        assert(!readOtio("{nope", back, err) && has(err, "not JSON"));
        std::printf("[PASS] OTIO: Stack of Tracks, gaps, a dissolve, LinearTimeWarp, audio, the reel and gain in metadata; read back the same cut\n");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_timecode();
    test_edl();
    test_fcpxml();
    test_otio();
    std::printf("interstellar_interchange: all tests passed\n");
    return 0;
}
