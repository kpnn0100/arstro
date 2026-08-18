/*
 *  D-24 fixture: where do the eight seconds of one RAF decode actually go?
 *
 *  The same call sequence NativeImageDecoder::decodeRaw uses, timed per phase, plus the two
 *  cheaper decode modes for comparison. The answer on a 26 MB Fujifilm X-Trans file is that
 *  `dcraw_process()` is 7.7 of 8.5 seconds — and cosmo spends it producing 4170x6246 pixels to
 *  render a 1600 px preview.
 *
 *      g++ -O2 -std=c++17 -I core/ImageProcessing/lib/LibRaw -o /tmp/rawphase \
 *          raw_phase_timing.cpp core/ImageProcessing/lib/LibRaw/lib/libraw.a -lz -lpthread
 *      /tmp/rawphase <file>.RAF            # full quality, as shipped
 *      /tmp/rawphase <file>.RAF --half     # params.half_size=1   -> half dimensions
 *      /tmp/rawphase <file>.RAF --fast     # params.user_qual=0   -> FULL dimensions, bilinear
 *
 *  `--fast` is the interesting one: full resolution, ~7x faster, so nothing downstream that
 *  depends on image dimensions has to change.
 */
#include <libraw/libraw.h>
#include <chrono>
#include <cstdio>
#include <string>
static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{ return std::chrono::duration<double, std::milli>(b - a).count(); }

int main(int argc, char **argv)
{
    const std::string mode = argc > 2 ? argv[2] : "";
    const bool half = mode == "--half";
    LibRaw raw;
    if (half) raw.imgdata.params.half_size = 1;
    if (mode == "--fast") raw.imgdata.params.user_qual = 0;   // bilinear, full resolution
    auto t0 = std::chrono::steady_clock::now();
    if (raw.open_file(argv[1]) != LIBRAW_SUCCESS) return 1;
    auto t1 = std::chrono::steady_clock::now();
    if (raw.unpack() != LIBRAW_SUCCESS) return 1;
    auto t2 = std::chrono::steady_clock::now();
    if (raw.dcraw_process() != LIBRAW_SUCCESS) return 1;
    auto t3 = std::chrono::steady_clock::now();
    int code = 0;
    libraw_processed_image_t *img = raw.dcraw_make_mem_image(&code);
    auto t4 = std::chrono::steady_clock::now();
    if (!img) return 1;
    printf("%-9s open %6.0f ms | unpack %6.0f ms | process %7.0f ms | make_mem %5.0f ms | total %7.0f ms  (%dx%d)\n",
           mode.empty() ? "full" : mode.c_str() + 2, ms(t0,t1), ms(t1,t2), ms(t2,t3), ms(t3,t4), ms(t0,t4), img->width, img->height);
    LibRaw::dcraw_clear_mem(img);
    return 0;
}
