#!/bin/sh
# R-RENDER-5: a still exported from Interstellar and the same frame exported from Cosmo are
# byte-identical — the cheapest proof that the rack really is Cosmo (R-RACK-2).
#
# One photo, graded in Interstellar (written THROUGH to the .cmp), cut onto a timeline and
# exported with `export-still`; then Cosmo exports the same .cmp. The decoded RGBA of the two
# files must hash the same. PNG file bytes may differ by encoder settings; the pixels may not.
#
#   still_equals_cosmo.sh <interstellar-cc> <cosmo-cc> <ffmpeg> <workdir>
set -eu
ISCC="$1"; COSMO="$2"; FFMPEG="$3"; DIR="$4"
rm -rf "$DIR"; mkdir -p "$DIR/cosmo_out"; cd "$DIR"
export HOME="$DIR" INTERSTELLAR_RECENTS="$DIR/recents"
"$FFMPEG" -loglevel error -f lavfi -i "mandelbrot=size=320x180" -frames:v 1 photo.png
"$ISCC" project new r5.isp --res 320x180 : rack add photo.png \
  : set photo.basic.exposure=0.4 photo.basic.contrast=20 photo.basic.temp=5200 photo.basic.vibrance=15 \
        "photo.curve.curve=0,0;0.25,0.19;0.75,0.83;1,1" "photo.grade.grade1=210,18,-4" \
  : track add --kind video : clip add --track v0 --src photo --in 0 --out 1 --at 0 \
  : project save : export-still --timeline main --out isp.png --at 0.5 > /dev/null
"$COSMO" export r5.cmp --outdir cosmo_out --format png > /dev/null
a=$("$FFMPEG" -loglevel error -i isp.png -pix_fmt rgba -f framemd5 - | tail -1 | awk '{print $NF}')
b=$("$FFMPEG" -loglevel error -i cosmo_out/photo.png -pix_fmt rgba -f framemd5 - | tail -1 | awk '{print $NF}')
echo "interstellar $a"
echo "cosmo        $b"
[ -n "$a" ] && [ "$a" = "$b" ]
