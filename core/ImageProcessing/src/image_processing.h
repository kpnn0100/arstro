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
#include "base/Spatial.h"
#include "base/SmoothedParameter.h"
#include "base/ImageConfig.h"
#include "base/Parallel.h"
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
#include "effect/Texture.h"
#include "effect/Clarity.h"

// Detail
#include "detail/Sharpen.h"
#include "detail/NoiseReduction.h"

// Transform
#include "transform/Crop.h"
#include "transform/Rotate.h"
#include "transform/LensCorrection.h"

// Analysis + video + engine
#include "analysis/Histogram.h"
#include "analysis/Segmenter.h"
#include "analysis/Contour.h"
#include "analysis/Detection.h"
#include "video/VideoProcessor.h"
#include "engine/EditParams.h"
#include "engine/EditParamsIO.h"
#include "engine/Apf.h"
#include "engine/EditParamsApf.h"
#include "engine/MaskStack.h"
#include "engine/EditEngine.h"
#include "engine/RenderService.h"
#include "compute/ComputeBackend.h"
