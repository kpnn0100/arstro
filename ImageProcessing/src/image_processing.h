/*
 *  Arstro ImageProcessing Library — aggregate header.
 *
 *  Add `src/` to the include path and `#include "image_processing.h"`. This is
 *  the image-domain analogue of the DSP library's synth_dsp.h. Headers are added
 *  here as the corresponding modules land.
 */
#pragma once

// Foundation
#include "base/Pixel.h"
#include "base/Image.h"
#include "base/ColorSpace.h"
#include "base/SmoothedParameter.h"
#include "base/ImageConfig.h"
#include "base/ImageProcessor.h"
#include "base/ImageSource.h"
#include "base/ImageBlock.h"

// Sources
#include "source/SolidImageSource.h"
#include "source/FileImageSource.h"

// Tone
#include "tone/Exposure.h"
#include "tone/Contrast.h"
#include "tone/ToneRegions.h"
#include "tone/ToneCurve.h"

// Color
#include "color/WhiteBalance.h"
#include "color/Vibrance.h"
#include "color/ColorMixer.h"
#include "color/ColorGrading.h"

// Effects
#include "effect/Dehaze.h"
#include "effect/Grain.h"

// Transform
#include "transform/Crop.h"
#include "transform/Rotate.h"

// Analysis + video + engine
#include "analysis/Histogram.h"
#include "video/VideoProcessor.h"
#include "engine/EditEngine.h"
