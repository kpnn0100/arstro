/*
 *  R-VIDEO-1 — the song's picture through the real FFmpeg (docs/discussion.md Round 18; the L3 round trip): a clip
 *  FFmpeg makes — a second of red, then a second of blue, 64×48 at 25 frames a second, a keyframe every fifth — is
 *  probed (and a sound, a still and a text file are not pictures); its frames decoded at times, scaled to fit a box,
 *  stepping forward without seeking as playing does; muxed beside a mix, its own stream copied, where an offset puts it
 *  (and cut at a keyframe when it began before the render, in decode order where the mix ends — B-frames too), its
 *  packets interleaved with the mix's as it goes; and carried through `solaris-cc` into an MP4 that `ffprobe`
 *  reads back — both streams, the picture's start and length where the offset put them. Never the user's files.
 */
#ifdef NDEBUG
#undef NDEBUG // a test asserts in every build
#endif
#include "VideoFiles.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace arstro;

namespace
{
    int passed = 0;
    void pass(const char *what)
    {
        std::printf("[PASS] %s\n", what);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &n) { return s.find(n) != std::string::npos; }

    fs::path dir;
    /** A command's stdout (and its stderr when asked), run in the scratch folder. */
    std::string sh(const std::string &cmd, int *rc = nullptr, bool withErr = false)
    {
        const std::string full = "cd '" + dir.string() + "' && " + cmd + (withErr ? " 2>&1" : " 2>/dev/null");
        FILE *p = popen(full.c_str(), "r");
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        const int st = pclose(p);
        if (rc) *rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        return out;
    }
    /** `key=value` out of ffprobe's compact lines for stream `type` (video | audio). */
    std::string probe(const std::string &file, const std::string &type, const std::string &key)
    {
        const std::string out = sh(std::string(FFPROBE) + " -v error -show_entries stream=codec_type,codec_name,start_time,duration -of compact '" + file + "'");
        std::istringstream in(out);
        std::string line;
        while (std::getline(in, line))
            if (contains(line, "codec_type=" + type))
            {
                const size_t at = line.find(key + "=");
                if (at == std::string::npos) return "";
                const size_t end = line.find('|', at);
                return line.substr(at + key.size() + 1, end == std::string::npos ? std::string::npos : end - at - key.size() - 1);
            }
        return "";
    }
    double num(const std::string &s) { return s.empty() || s == "N/A" ? -1e9 : std::atof(s.c_str()); }
    int frames(const std::string &file)
    {
        return std::atoi(sh(std::string(FFPROBE) + " -v error -count_frames -select_streams v -show_entries stream=nb_read_frames -of csv=p=0 '" + file + "'").c_str());
    }
    /** The first frame's middle pixel, decoded by ffmpeg itself: 'r', 'b' or '?'. */
    char firstColour(const std::string &file)
    {
        const std::string raw = sh(std::string(FFMPEG) + " -v error -i '" + file + "' -frames:v 1 -s 4x4 -f rawvideo -pix_fmt rgb24 -");
        if (raw.size() < 48) return '?';
        const unsigned char *p = (const unsigned char *)raw.data() + (2 * 4 + 2) * 3;
        return p[0] > 180 && p[2] < 80 ? 'r' : p[2] > 180 && p[0] < 80 ? 'b' : '?';
    }
    /** In the file's own order, how far one stream ran ahead of the other (s): a player reads it front to back. */
    double worstLead(const std::string &file)
    {
        std::istringstream in(sh(std::string(FFPROBE) + " -v error -show_entries packet=codec_type,dts_time -of csv=p=0 '" + file + "'"));
        std::string line;
        double v = -1, a = -1, worst = 0;
        while (std::getline(in, line))
        {
            const size_t comma = line.find(',');
            if (comma == std::string::npos || line.compare(comma + 1, 3, "N/A") == 0) continue;
            (line.compare(0, comma, "video") == 0 ? v : a) = std::atof(line.c_str() + comma + 1);
            if (v >= 0 && a >= 0) worst = std::max(worst, std::fabs(v - a));
        }
        return worst;
    }
    char colourOf(const solaris_host::VideoFrame &f)
    {
        const uint8_t *p = f.rgba.data() + ((size_t)(f.height / 2) * (size_t)f.width + (size_t)(f.width / 2)) * 4;
        return p[0] > 180 && p[2] < 80 ? 'r' : p[2] > 180 && p[0] < 80 ? 'b' : '?';
    }
    std::vector<std::vector<float>> tone(double seconds, int rate)
    {
        std::vector<std::vector<float>> m(2, std::vector<float>((size_t)std::llround(seconds * rate)));
        for (size_t i = 0; i < m[0].size(); ++i) m[0][i] = m[1][i] = (float)(0.25 * std::sin(2 * M_PI * 440.0 * (double)i / rate));
        return m;
    }
}

static void test_the_probe()
{
    solaris::VideoInfo vi;
    std::string err;
    const bool ok = solaris_host::probeVideo((dir / "picture.mp4").string(), vi, err);
    std::printf("    picture.mp4: %.3f s, %dx%d, %.2f fps\n", vi.duration, vi.width, vi.height, vi.fps);
    assert(ok && std::fabs(vi.duration - 2.0) < 0.05 && vi.width == 64 && vi.height == 48 && std::fabs(vi.fps - 25.0) < 1e-6);
    for (const auto &bad : {std::make_pair("sound.wav", "it has no picture"), std::make_pair("still.png", "a still image"),
                            std::make_pair("note.txt", "not a file FFmpeg reads")})
    {
        err.clear();
        const bool read = solaris_host::probeVideo((dir / bad.first).string(), vi, err);
        std::printf("    %s: %s\n", bad.first, err.c_str());
        assert(!read && contains(err, bad.second));
    }
    pass("the probe (R-VIDEO-1): a picture's length, size and frame rate; a sound, a still image and a text file are no video");
}

static void test_frames_at_times()
{
    solaris_host::VideoReader v;
    std::string err;
    assert(!v.open((dir / "note.txt").string(), err) && !err.empty());
    assert(v.open((dir / "picture.mp4").string(), err));
    solaris_host::VideoFrame f;
    // fitted to a box, its aspect kept: 64×48 in 32×32 is 32×24; in 100×30, 40×30
    assert(v.frame(0.5, 32, 32, f, err) && f.width == 32 && f.height == 24 && f.rgba.size() == 32u * 24u * 4u);
    assert(colourOf(f) == 'r' && std::fabs(f.time - 0.48) < 1e-6); // frame 12 shows from 0.48 s
    const int first = v.seeks();
    assert(v.frame(0.999, 100, 30, f, err) && f.width == 40 && f.height == 30 && colourOf(f) == 'r' && std::fabs(f.time - 0.96) < 1e-6);
    assert(v.frame(1.0, 32, 32, f, err) && colourOf(f) == 'b' && std::fabs(f.time - 1.0) < 1e-6); // the cut, on its frame
    assert(v.seeks() == first);                                                                  // forward: no seek
    // back to the start, then played frame by frame: each frame its own, and never a seek
    assert(v.frame(0.0, 32, 32, f, err) && colourOf(f) == 'r' && f.time == 0.0 && v.seeks() == first + 1);
    const int played = v.seeks();
    for (int i = 0; i < 50; ++i)
    {
        assert(v.frame(i * 0.04 + 0.01, 32, 32, f, err));
        if (std::fabs(f.time - i * 0.04) > 1e-6 || colourOf(f) != (i < 25 ? 'r' : 'b')) std::printf("    frame %d: time %.3f colour %c\n", i, f.time, colourOf(f));
        assert(std::fabs(f.time - i * 0.04) < 1e-6 && colourOf(f) == (i < 25 ? 'r' : 'b'));
    }
    assert(v.seeks() == played);
    // after its end the last frame stays; back again seeks
    assert(v.frame(5.0, 32, 32, f, err) && colourOf(f) == 'b' && std::fabs(f.time - 1.96) < 1e-6);
    assert(v.frame(0.3, 32, 32, f, err) && colourOf(f) == 'r' && std::fabs(f.time - 0.28) < 1e-6 && v.seeks() > played);
    pass("frames at times (R-VIDEO-1): the frame showing at a time, fitted to a box; forward frame by frame with no seek, as playing; back, or past the end, the right one");
}

static void test_the_mux()
{
    std::string err;
    const auto mix = tone(3.0, 48000);
    // the picture half a second in: its own stream, copied; the mix as AAC
    assert(solaris_host::muxVideo((dir / "picture.mp4").string(), 0.5, mix, 48000, (dir / "late.mp4").string(), err));
    std::printf("    late.mp4: video %s from %s for %s; audio %s for %s\n", probe("late.mp4", "video", "codec_name").c_str(),
                probe("late.mp4", "video", "start_time").c_str(), probe("late.mp4", "video", "duration").c_str(),
                probe("late.mp4", "audio", "codec_name").c_str(), probe("late.mp4", "audio", "duration").c_str());
    assert(probe("late.mp4", "video", "codec_name") == "mpeg4" && probe("late.mp4", "audio", "codec_name") == "aac");
    assert(std::fabs(num(probe("late.mp4", "video", "start_time")) - 0.5) < 0.001 && std::fabs(num(probe("late.mp4", "video", "duration")) - 2.0) < 0.001);
    assert(std::fabs(num(probe("late.mp4", "audio", "duration")) - 3.0) < 0.05 && frames("late.mp4") == 50);
    // begun 0.9 s before the render: frame 23 would be first, but it leans on frame 20 — cut at the next keyframe, frame
    // 25 (the blue's first), shown 0.1 s into the file, where it belongs
    assert(solaris_host::muxVideo((dir / "picture.mp4").string(), -0.9, mix, 48000, (dir / "early.mkv").string(), err));
    assert(probe("early.mkv", "video", "codec_name") == "mpeg4" && probe("early.mkv", "audio", "codec_name") == "flac");
    std::printf("    early.mkv: %d frames from %s, the first %c\n", frames("early.mkv"), probe("early.mkv", "video", "start_time").c_str(), firstColour("early.mkv"));
    assert(frames("early.mkv") == 25 && firstColour("early.mkv") == 'b' && std::fabs(num(probe("early.mkv", "video", "start_time")) - 0.1) < 0.001);
    // cut where the mix ends: a 1.5 s mix keeps the picture's first 1.5 s
    assert(solaris_host::muxVideo((dir / "picture.mp4").string(), 0.0, tone(1.5, 48000), 48000, (dir / "short.mov").string(), err));
    assert(frames("short.mov") == 38 && probe("short.mov", "audio", "codec_name") == "aac");
    // interleaved as it goes: a long picture's packets and the mix's side by side in the file, not seconds apart
    assert(solaris_host::muxVideo((dir / "long.mp4").string(), 0.0, tone(14.0, 48000), 48000, (dir / "long-out.mp4").string(), err));
    std::printf("    long-out.mp4: one stream at most %.2f s ahead of the other\n", worstLead("long-out.mp4"));
    assert(worstLead("long-out.mp4") < 0.25);
    // B-frames (shown out of decode order): cut 0.9 s in and ended at 1 s of mix — every frame kept decodes, the first
    // one a keyframe at most a GOP after the cut, in time (the picture at file time t is its own t + 0.9: red before 1 s)
    assert(solaris_host::muxVideo((dir / "bframes.mp4").string(), -0.9, tone(1.0, 48000), 48000, (dir / "bframes-out.mp4").string(), err));
    int rc = 0;
    const std::string broken = sh(std::string(FFMPEG) + " -v error -i bframes-out.mp4 -f null -", &rc, true);
    std::printf("    bframes-out.mp4: %d frames from %s, the first %c%s\n", frames("bframes-out.mp4"), probe("bframes-out.mp4", "video", "start_time").c_str(),
                firstColour("bframes-out.mp4"), broken.empty() ? ", all decode" : (", " + broken).c_str());
    const double from = num(probe("bframes-out.mp4", "video", "start_time"));
    assert(rc == 0 && broken.empty() && from >= 0 && from < 0.2);
    assert(firstColour("bframes-out.mp4") == (from + 0.9 < 1.0 - 1e-6 ? 'r' : 'b'));
    // every frame from the keyframe to the mix's end shows (frames 0…47 are before 1.9 s of picture) — at most a
    // reorder's worth past it — and the first one decoded is that keyframe, at the file's start (no lead-in kept)
    const int k0 = (int)std::lround((from + 0.9) * 25), needed = 48 - k0, got = frames("bframes-out.mp4");
    const std::string first = sh(std::string(FFPROBE) + " -v error -select_streams v -show_entries frame=key_frame,pkt_pts_time -of csv=p=0 bframes-out.mp4 | head -1");
    std::printf("    bframes-out.mp4: keyframe %d first, %d frames (%d before the end), the first decoded: %s", k0, got, needed, first.c_str());
    assert(got >= needed && got <= needed + 2 && first.rfind("1,", 0) == 0 && std::fabs(std::atof(first.c_str() + 2) - from) < 0.001);
    // and nothing copied that cannot show: every packet of the picture in the file is a frame that decodes
    const int packets = std::atoi(sh(std::string(FFPROBE) + " -v error -count_packets -select_streams v -show_entries stream=nb_read_packets -of csv=p=0 bframes-out.mp4").c_str());
    assert(packets == got);
    // refusals
    assert(!solaris_host::muxVideo((dir / "picture.mp4").string(), 3.0, mix, 48000, (dir / "x.mp4").string(), err) && contains(err, "starts after the render ends"));
    assert(!solaris_host::muxVideo((dir / "picture.mp4").string(), -2.5, mix, 48000, (dir / "x.mp4").string(), err) && contains(err, "ends before the render starts"));
    assert(!solaris_host::muxVideo((dir / "sound.wav").string(), 0, mix, 48000, (dir / "x.mp4").string(), err) && contains(err, "it has no picture"));
    pass("the mux (R-VIDEO-1): the picture's own stream copied where the offset puts it — cut at a keyframe when it began before, and where the mix ends; the mix beside it, AAC in MP4/MOV, FLAC in MKV");
}

static void test_through_solaris_cc()
{
    int rc = 0;
    const std::string cc = std::string("'") + SOLARIS_CC + "'";
    // refused through the CLI: a sound is no picture
    std::string out = sh(cc + " project new refused.slp : video set sound.wav", &rc, true);
    assert(rc != 0 && contains(out, "cannot read sound.wav as a video: it has no picture"));
    out = sh(cc + " project new song.slp --bpm 120 : clip add --instrument synth --at 0 --length 8 : pattern steps pt_1 --pitch 60 \"x...x...x...x...\""
                  " : video set picture.mp4 --offset 1 : render --out song.mp4 --video : project save",
             &rc, true);
    std::printf("    solaris-cc: %s", out.c_str());
    assert(rc == 0 && contains(out, "vd_1\n") && contains(out, "song.mp4\n") && !contains(out, "Qavg"));
    std::ifstream in(dir / "song.slp");
    std::stringstream text;
    text << in.rdbuf();
    assert(contains(text.str(), "#avideo id=vd_1 src=picture.mp4 offset=1.0"));
    std::printf("    song.mp4: video from %s for %s; audio %s for %s\n", probe("song.mp4", "video", "start_time").c_str(),
                probe("song.mp4", "video", "duration").c_str(), probe("song.mp4", "audio", "codec_name").c_str(), probe("song.mp4", "audio", "duration").c_str());
    assert(std::fabs(num(probe("song.mp4", "video", "start_time")) - 1.0) < 0.001 && std::fabs(num(probe("song.mp4", "video", "duration")) - 2.0) < 0.001);
    assert(probe("song.mp4", "audio", "codec_name") == "aac" && num(probe("song.mp4", "audio", "duration")) >= 4.0 && frames("song.mp4") == 50);
    // from beat 4 (2 s): the picture began a second before — the blue half, from the file's start
    out = sh(cc + " project open song.slp : render --out from.mkv --video --from 4", &rc, true);
    assert(rc == 0 && frames("from.mkv") == 25 && firstColour("from.mkv") == 'b');
    pass("through solaris-cc (R-VIDEO-1, L3): `video set` kept in the .slp; `render --video` an MP4 ffprobe reads — the picture from 1 s for its 2 s, the mix as AAC; from beat 4 an MKV of the blue half");
}

int main()
{
    unsetenv("DISPLAY"); // nothing here opens a window
    const char *base = std::getenv("SOLARIS_TEST_DIR");
    dir = fs::path(base && *base ? base : fs::temp_directory_path().string()) / ("solaris-video-test-" + std::to_string((long)getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    setenv("SOLARIS_SETTINGS", (dir / "settings.txt").c_str(), 1); // never the user's own files
    setenv("SOLARIS_RECENTS", (dir / "recents").c_str(), 1);
    setenv("SOLARIS_PRESETS", (dir / "presets").c_str(), 1);
    setenv("SOLARIS_TEMPLATES", (dir / "templates").c_str(), 1);
    setenv("SOLARIS_LIBRARY", (dir / "library").c_str(), 1);
    setenv("SOLARIS_PLUGINS", (dir / "plugins").c_str(), 1);
    setenv("SOLARIS_VST3_PATH", (dir / "vst3").c_str(), 1);
    // the clips, made by FFmpeg: red then blue (a keyframe every fifth frame), a second of tone, a still, a text file
    int rc = 0;
    const std::string ff = std::string("'") + FFMPEG + "' -v error -y ";
    sh(ff + "-f lavfi -i \"color=c=red:s=64x48:r=25:d=1[a];color=c=blue:s=64x48:r=25:d=1[b];[a][b]concat=n=2:v=1:a=0\" -c:v mpeg4 -g 5 -pix_fmt yuv420p picture.mp4", &rc);
    assert(rc == 0);
    sh(ff + "-f lavfi -i \"color=c=red:s=64x48:r=25:d=1[a];color=c=blue:s=64x48:r=25:d=1[b];[a][b]concat=n=2:v=1:a=0\" -c:v mpeg4 -g 5 -bf 2 -pix_fmt yuv420p bframes.mp4", &rc);
    assert(rc == 0);
    sh(ff + "-f lavfi -i testsrc=s=64x48:r=25:d=14 -c:v mpeg4 -g 25 -pix_fmt yuv420p long.mp4", &rc);
    assert(rc == 0);
    sh(ff + "-f lavfi -i sine=frequency=440:duration=1 sound.wav", &rc);
    assert(rc == 0);
    sh(ff + "-f lavfi -i color=c=green:s=16x16 -frames:v 1 still.png", &rc);
    assert(rc == 0);
    std::ofstream(dir / "note.txt") << "not a picture\n";

    test_the_probe();
    test_frames_at_times();
    test_the_mux();
    test_through_solaris_cc();
    fs::remove_all(dir);
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
