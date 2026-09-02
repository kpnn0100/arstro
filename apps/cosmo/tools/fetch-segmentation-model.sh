#!/usr/bin/env bash
# Fetch a segmentation model and the ONNX Runtime for cosmo's Detect mask (R-AISEG-15).
#
# cosmo ships neither, on purpose: the runtime is 12 MB of shared library per platform and the
# models worth having are other people's, under other people's licences. This script puts both
# where cosmo looks, writes the manifest, and prints the licence so the decision is made with the
# facts in front of you rather than after the fact.
#
#   ./fetch-segmentation-model.sh [--list] [--model NAME] [--dest DIR] [--no-runtime]
#
# Everything lands in ${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/models/ unless --dest says
# otherwise. Re-running is safe: an existing file is left alone.
set -euo pipefail

ORT_VERSION="1.22.0"
DEST="${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/models"
MODEL="selfie"
WANT_RUNTIME=1

# ── the catalogue ────────────────────────────────────────────────────────────────────────
# Only models whose weights carry a PERMISSIVE licence are offered by name. Everything with a
# class for sky, foliage, water or hair that is worth using is trained on ADE20K or
# CelebAMask-HQ, both of which are non-commercial-research datasets — see
# docs/segmentation-models.md. Such a model can still be installed by hand; this script will
# not pick one for you, because that is a decision and not a download.
catalogue() {
  cat <<'EOF'
selfie|MediaPipe Selfie Segmentation|Apache-2.0|person|256|nchw|0..1|alpha|none|https://huggingface.co/onnx-community/mediapipe_selfie_segmentation|https://huggingface.co/onnx-community/mediapipe_selfie_segmentation/resolve/main/onnx/model.onnx
selfie-landscape|MediaPipe Selfie Segmentation (landscape)|Apache-2.0|person|256|nchw|0..1|alpha|none|https://huggingface.co/onnx-community/mediapipe_selfie_segmentation_landscape|https://huggingface.co/onnx-community/mediapipe_selfie_segmentation_landscape/resolve/main/onnx/model.onnx
u2netp|U^2-Net (small)|Apache-2.0|person|320|nchw|imagenet|alpha|sigmoid|https://huggingface.co/BritishWerewolf/U-2-Netp|https://huggingface.co/BritishWerewolf/U-2-Netp/resolve/main/onnx/model.onnx
EOF
}

usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
  case "$1" in
    --list) catalogue | awk -F'|' '{printf "  %-18s %-42s %-12s subject=%s\n", $1, $2, $3, $4}'; exit 0;;
    --model) MODEL="$2"; shift 2;;
    --dest) DEST="$2"; shift 2;;
    --no-runtime) WANT_RUNTIME=0; shift;;
    -h|--help) usage;;
    *) echo "unknown argument: $1" >&2; exit 2;;
  esac
done

row="$(catalogue | grep "^${MODEL}|" || true)"
if [ -z "$row" ]; then
  echo "no such model: ${MODEL}" >&2
  echo "try --list, or write a manifest by hand (docs/segmentation-models.md)" >&2
  exit 2
fi
IFS='|' read -r key name licence subject size layout range output activation source url <<<"$row"

mkdir -p "$DEST"
echo "cosmo segmentation model -> $DEST"
echo "  ${name}"
echo "  licence: ${licence}   (${source})"
echo "  cosmo neither vets nor grants this licence. Read it before shipping anything made with it."
echo

# ── the runtime ──────────────────────────────────────────────────────────────────────────
# Microsoft's own prebuilt release, MIT. Only the shared library is kept — cosmo links nothing
# and finds it at runtime, so there is no import library and no header to install.
if [ "$WANT_RUNTIME" -eq 1 ]; then
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ORT_PKG="onnxruntime-win-x64-${ORT_VERSION}.zip"; ORT_LIB="onnxruntime.dll";;
    Darwin)               ORT_PKG="onnxruntime-osx-arm64-${ORT_VERSION}.tgz"; ORT_LIB="libonnxruntime.dylib";;
    *) case "$(uname -m)" in
         aarch64|arm64) ORT_PKG="onnxruntime-linux-aarch64-${ORT_VERSION}.tgz";;
         *)             ORT_PKG="onnxruntime-linux-x64-${ORT_VERSION}.tgz";;
       esac
       ORT_LIB="libonnxruntime.so";;
  esac
  if [ -e "${DEST}/${ORT_LIB}" ]; then
    echo "runtime: ${ORT_LIB} already there"
  else
    echo "runtime: fetching ${ORT_PKG} (~50-70 MB, MIT)"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    curl -fsSL -o "${tmp}/${ORT_PKG}" \
      "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_PKG}"
    case "$ORT_PKG" in
      *.zip) ( cd "$tmp" && unzip -q "$ORT_PKG" );;
      *.tgz) ( cd "$tmp" && tar xzf "$ORT_PKG" );;
    esac
    found="$(find "$tmp" -name "${ORT_LIB}*" -type f | head -1)"
    [ -n "$found" ] || { echo "  could not find ${ORT_LIB} in ${ORT_PKG}" >&2; exit 1; }
    cp "$found" "${DEST}/${ORT_LIB}"
    # The licence travels with the binary. cosmo does not redistribute it; you might.
    lic="$(find "$tmp" -name LICENSE -type f | head -1)"
    [ -n "$lic" ] && cp "$lic" "${DEST}/onnxruntime-LICENSE.txt"
    echo "  installed ${ORT_LIB}"
  fi
fi

# ── the weights ──────────────────────────────────────────────────────────────────────────
if [ -e "${DEST}/${key}.onnx" ]; then
  echo "model: ${key}.onnx already there"
else
  echo "model: fetching ${key}.onnx"
  curl -fsSL -o "${DEST}/${key}.onnx" "$url"
  echo "  $(wc -c <"${DEST}/${key}.onnx") bytes"
fi

# ── the manifest ─────────────────────────────────────────────────────────────────────────
# Rewritten every run: it is generated, and a stale one describing a model that has since been
# replaced is the failure mode this whole file exists to avoid.
cat >"${DEST}/${key}.cosmoseg" <<EOF
# cosmo segmentation model — generated by tools/fetch-segmentation-model.sh
# Loaded by cosmo when it is the alphabetically first *.cosmoseg in this directory.
name=${name}
model=${key}.onnx
inputSize=${size}
layout=${layout}
range=${range}
output=${output}
activation=${activation}
subject=${subject}
license=${licence}
source=${source}
EOF
echo "manifest: ${key}.cosmoseg"
echo
echo "Done. Start cosmo and the Mask panel's Detect header will read \"Detect (${name})\"."
echo "Subjects this model does not answer for stay with the built-in classifier."
