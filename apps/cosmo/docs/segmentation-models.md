# Segmentation models for cosmo's Detect mask

cosmo ships **no model weights and no inference runtime**. A Detect mask is decided by the built-in
statistical classifier (colour, position, local structure — see `analysis/Segmenter.h`) unless you
install a model, in which case the model answers for the subjects it declares and the built-in still
answers for the rest.

This file is the survey behind that decision, the install procedure, and the licence facts. It is
deliberately blunt about what is and is not available, because the honest answer is not the one
anybody wants.

---

## 1. The finding: there is no single open-source model that covers the five subjects

The subjects a photo editor wants are sky, skin, hair, foliage, water and people. Searching for ONNX
models with a **permissive** licence (2026-09-02, Hugging Face's own `library=onnx` +
`pipeline_tag=image-segmentation` index) returns essentially one family:

| Model | Licence | What it segments |
|---|---|---|
| `onnx-community/mediapipe_selfie_segmentation` | Apache-2.0 | person / background |
| `onnx-community/mediapipe_selfie_segmentation_landscape` | Apache-2.0 | person / background |
| `BritishWerewolf/U-2-Net`, `U-2-Netp` | Apache-2.0 | salient object (the subject) |
| `Xenova/modnet`, `onnx-community/modnet-webnn` | Apache-2.0 | portrait matting |
| `onnx-community/BiRefNet-ONNX`, `BiRefNet_lite-ONNX`, `BEN2-ONNX` | MIT | salient object, high quality |
| `onnx-community/ormbg-ONNX` | Apache-2.0 | background removal |
| `onnx-community/ISNet-ONNX` | **AGPL-3.0** | salient object — copyleft, read it first |

**Every permissively-licensed one is a subject-versus-background matting model.** Not one of them has
a class for sky, foliage, water or hair.

The models that *do* have those classes are trained on **ADE20K** (150 classes: sky 2, tree 4, grass
9, person 12, plant 17, water 21, sea 26 …) or on **CelebAMask-HQ** (face parsing: skin, hair, brows,
lips). Both datasets are released for **non-commercial research and education only** — ADE20K's terms
require permission from MIT CSAIL for commercial use — and the best-known ADE20K checkpoints
(SegFormer) additionally carry NVIDIA's **source-code licence, which is non-commercial**. On Hugging
Face every SegFormer ONNX conversion is tagged `license:other` for exactly this reason.

So:

- **Person / subject** — genuinely available under Apache-2.0 or MIT. This is why
  `SemanticSubject::Person` exists: it is the subject a real model can actually be had for.
- **Sky, foliage, water, skin, hair** — available as *weights*, but the ones worth using inherit a
  non-commercial dataset or licence. cosmo will happily use them; **whether you may is your call, and
  the manifest records the licence so the answer is written down next to the model.**
- The built-in classifier remains the default for those four-plus-one, and it is not a placeholder:
  sky and foliage in particular are strong, because a hue band plus smoothness plus a position prior
  is genuinely most of what "sky" means in a photograph.

---

## 2. Installing a model

Everything lives in one directory, `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/models/`, and nothing
else has to be configured. `tools/fetch-segmentation-model.sh` does all of this:

```
models/
  onnxruntime.dll  /  libonnxruntime.so          the runtime, fetched from Microsoft's releases
  selfie.onnx                                    the weights
  selfie.cosmoseg                                the manifest — how to drive them
```

cosmo looks for the alphabetically first `*.cosmoseg`, loads the runtime with
`LoadLibrary`/`dlopen`, and reports what it found:

```
[info] segment: using MediaPipe Selfie Segmentation (licence Apache-2.0) from …/selfie.cosmoseg
[warn] segment: …/broken.cosmoseg could not be used: line 4: unknown key inputWidht
[info] segment: no model installed in …/models (built-in classifier)
```

The name also reaches the UI — the Detect section header reads **"Detect (MediaPipe Selfie
Segmentation)"** instead of **"Detect (colour & texture)"** — because a mask from a network and a
mask from a hue band are not the same claim and a photographer should not have to guess which they
are looking at.

Nothing here is a build dependency. cosmo links no runtime; a machine with no model installed
compiles and runs identically.

---

## 3. The manifest

Line-based `key=value`, `#` comments, no escaping — the same shape as every other text format cosmo
owns. **An unknown key is an error**, not an ignored line: a manifest is written by hand, and
silently defaulting a typo away turns "I misspelled `inputWidth`" into "the model does nothing and I
cannot see why".

```ini
# cosmo segmentation model
name=MediaPipe Selfie Segmentation
model=selfie.onnx           # relative to this file
inputWidth=256
inputHeight=256             # or inputSize=256 for both
layout=nchw                 # nchw | nhwc
range=0..1                  # 0..1 | -1..1 | imagenet
output=alpha                # alpha (one channel of "is it") | classes (C class channels)
activation=none             # none | sigmoid | softmax
subject=person              # alpha models: the ONE subject this answers for
license=Apache-2.0
source=https://huggingface.co/onnx-community/mediapipe_selfie_segmentation
```

A multi-class model maps output channels to subjects instead of naming one:

```ini
output=classes
activation=softmax
class.2=sky        # ADE20K indices, for a model trained on it
class.4=foliage
class.9=foliage
class.12=person
class.21=water
class.26=water
```

Every subject the manifest does **not** claim is **declined**, and the built-in answers it. That is
the whole point of the seam: a person-matting model must never claim to have found the sky, or the
built-in stops being consulted for the subjects it is actually good at.

Preprocessing follows the manifest exactly: the framed image is area-averaged down to the model's
input size **in linear light** (the only place averaging pixels is physically meaningful), encoded to
sRGB, then normalised per `range` — because every published preprocessing recipe is stated in
gamma-encoded terms, the same reasoning as R-AISEG-3.

---

## 4. What was verified, and how

The path was run end to end against the real thing, not argued about:

```
segmenter: MediaPipe Selfie Segmentation   (licence Apache-2.0)
  model 256x256, nchw, output=alpha
  photo 512x512
  sky      -> declined
  skin     -> declined
  foliage  -> declined
  water    -> declined
  hair     -> declined
  person   -> ANSWERED
  coverage: 262144 values, mean 0.4217, range [0.000, 1.000]  (42.2% of the frame)
  outline: 6 loop(s), 866 points
```

ONNX Runtime 1.22.0 (Microsoft's prebuilt Windows x64 release, MIT), loaded dynamically from a
MinGW-built binary that links nothing, driving a 462 KB Apache-2.0 model over a 512×512 portrait: the
subject is masked, everything else declined, and the boundary traces into six loops.

---

## 5. Notes for whoever extends this

- **Only the C API header is vendored** (`third_party/onnxruntime_c_api.h`, MIT, 275 KB). The struct's
  **field order is the ABI**, so a hand-written subset would compile and then call the wrong function
  pointer — silently. `GetApi(ORT_API_VERSION)` returns null against an older runtime, which is the
  negotiation working, and cosmo reports it and falls back.
- **MinGW needs `#define _stdcall __stdcall`** before the header in strict `-std=c++NN` mode. The
  build uses `-std=c++17`, not `gnu++17`, and without it the header is a wall of
  `expected ')' before '*'`.
- **One intra-op thread.** The render worker is already one of a budgeted set (R-CPU / R-SVC-10), and
  a model helping itself to every core behind the budget's back is exactly the duplication D-11 was
  about.
- ONNX Runtime publishes prebuilt binaries for **win-x64, linux-x64, linux-aarch64 and osx-arm64**, so
  the SBC target (RK3588 / A733, `linux-aarch64`) is covered by the same script.
- If you add a class-mapped model, add its class indices to the manifest, not to the code. The code
  knows about subjects; only the manifest knows about a particular model's numbering.
