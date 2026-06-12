#include "ScopeApp.h"

namespace arstro
{
namespace examples
{
    using namespace artboard;

    ScopeApp::ScopeApp(double width, double height) : mW(width), mH(height), mBoard(Size{width, height})
    {
        AudioConfig::instance().setChannelCount(2);
        mRing.assign((size_t)width, 0.0f); // one sample column per pixel of width

        mBoard.setBackground(Color::hex(0x11141a));

        mTitle = std::make_shared<Text>("arstro · scope", Point{16, 28}, 18, Color::hex(0x5cc8ff));
        mWave = std::make_shared<Polyline>();
        mWave->paint = Paint::stroked(Color::hex(0x5cc8ff), 2.0);
        mPlayhead = std::make_shared<Line>(Point{0, 0}, Point{0, height}, Color::hex(0xff8a5c), 1.0);

        mBoard.add(mTitle);
        mBoard.add(mWave);
        mBoard.add(mPlayhead);

        mSweep.set(0.0);
    }

    void ScopeApp::renderAudio(float *interleaved, int frames)
    {
        std::vector<double> blk;
        mSynth.renderBlockDouble(blk, frames); // interleaved stereo doubles
        const int ch = AudioConfig::instance().channelCount();
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < ch; ++c)
                interleaved[i * ch + c] = (float)blk[i * ch + c];
            // capture channel 0 into the ring (drop oldest)
            mRing.erase(mRing.begin());
            mRing.push_back((float)blk[i * ch]);
        }
    }

    void ScopeApp::render(IRenderTarget &target, double nowMs)
    {
        // Loop the playhead sweep every ~2 s using the animation system.
        if (!mSweep.isAnimating())
            mSweep.animateTo(1.0, 2000.0, Easing::Linear, nowMs);
        double s = mSweep.update(nowMs);
        if (!mSweep.isAnimating() && s >= 1.0)
            mSweep.set(0.0); // restart next frame

        // Map the ring to a waveform polyline across the board (centre line = mH/2).
        const double mid = mH * 0.5, amp = mH * 0.4;
        mWave->points.clear();
        mWave->points.reserve(mRing.size());
        for (size_t i = 0; i < mRing.size(); ++i)
        {
            double x = (mRing.size() <= 1) ? 0.0 : (double)i / (mRing.size() - 1) * mW;
            mWave->points.push_back(Point{x, mid - mRing[i] * amp});
        }

        double px = s * mW;
        mPlayhead->a = Point{px, 0};
        mPlayhead->b = Point{px, mH};

        mBoard.render(target, nowMs);
    }
}
}
