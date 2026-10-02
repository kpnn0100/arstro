#!/bin/sh
# R-RENDER-6 through the real encoders: every codec and profile Deliver offers is rendered by the
# CLI (the grammar the GUI dispatches) and read back with ffprobe — codec, profile, pixel format,
# the BT.709 tags (D-9), size and rate. Then the colour itself: an H.264 render, decoded by FFmpeg
# honouring its tags, must give the same pixel as the PNG still of the same frame (D-9: an untagged
# BT.601 conversion shifted a saturated colour by far more than the tolerance).
#   render_codecs.sh <interstellar-cc> <ffmpeg> <ffprobe> <workdir>
set -eu
CC="$1"; FFMPEG="$2"; FFPROBE="$3"; DIR="$4"
rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"
export HOME="$DIR" INTERSTELLAR_RECENTS="$DIR/recents"
# a saturated flat field: where a matrix mistake shows most
"$FFMPEG" -loglevel error -f lavfi -i "color=c=0xD83A1E:size=320x180:rate=24:duration=2" -pix_fmt yuv444p -c:v libx264 -qp 0 a.mp4
"$CC" project new mv.isp --res 320x180 : rack add a.mp4 : track add --kind video \
  : clip add --track v0 --src a --in 0 --out 2 --at 0 --name shotA : project save > /dev/null
fail=0
probe() {  # file field → value
  "$FFPROBE" -v error -select_streams v:0 -show_entries "stream=$2" -of default=nw=1:nk=1 "$1" | head -1
}
check() {  # name file field expected
  got=$(probe "$2" "$3")
  if [ "$got" = "$4" ]; then echo "  [ok] $1: $3=$got"; else echo "  [FAIL] $1: $3=$got, wanted $4"; fail=1; fi
}
render() { "$CC" project open mv.isp : render --timeline main "$@" : wait render.done > /dev/null; }

render --format h264 --quality 23 --speed fast --out h264.mp4
check h264 h264.mp4 codec_name h264
check h264 h264.mp4 pix_fmt yuv420p
check h264 h264.mp4 color_space bt709
check h264 h264.mp4 color_transfer bt709
check h264 h264.mp4 color_primaries bt709
check h264 h264.mp4 nb_frames 48
render --format h265 --bits 10 --res 160x90 --fps 12 --out h265.mp4
check h265 h265.mp4 codec_name hevc
check h265 h265.mp4 pix_fmt yuv420p10le
check h265 h265.mp4 codec_tag_string hvc1
check h265 h265.mp4 width 160
check h265 h265.mp4 height 90
check h265 h265.mp4 r_frame_rate 12/1
for p in proxy lt standard hq 4444; do
  render --format prores --profile $p --out prores_$p.mov
  check "prores $p" prores_$p.mov codec_name prores
done
check "prores proxy" prores_proxy.mov profile Proxy
check "prores hq" prores_hq.mov profile HQ
check "prores 4444" prores_4444.mov profile 4444
check "prores standard" prores_standard.mov pix_fmt yuv422p10le
for p in lb hqx 444; do
  render --format dnxhr --profile $p --out dnxhr_$p.mov
  check "dnxhr $p" dnxhr_$p.mov codec_name dnxhd
done
check "dnxhr lb" dnxhr_lb.mov pix_fmt yuv422p
check "dnxhr hqx" dnxhr_hqx.mov pix_fmt yuv422p10le
check "dnxhr 444" dnxhr_444.mov pix_fmt yuv444p10le
check "dnxhr hqx" dnxhr_hqx.mov profile "DNXHR HQX"
render --format png-seq --range 0:0.5 --out frames
n=$(ls frames | wc -l)
if [ "$n" = 12 ]; then echo "  [ok] png-seq: 12 frames"; else echo "  [FAIL] png-seq: $n frames, wanted 12"; fail=1; fi

# the colour: the H.264 render's centre pixel, decoded honouring its tags, vs the PNG still
"$CC" project open mv.isp : export-still --timeline main --at 1 --out still.png > /dev/null
px() { "$FFMPEG" -loglevel error -i "$1" -frames:v 1 -vf "crop=2:2:160:90,scale=out_range=pc,format=rgb24" -f rawvideo - | od -An -tu1 -N3 | tr -s ' ' | sed 's/^ //'; }
a=$(px still.png); b=$(px h264.mp4)
echo "  still $a  h264 $b"
set -- $a; r1=$1; g1=$2; b1=$3
set -- $b; r2=$1; g2=$2; b2=$3
d() { x=$(( $1 - $2 )); [ $x -lt 0 ] && x=$(( -x )); echo $x; }
if [ $(d $r1 $r2) -le 4 ] && [ $(d $g1 $g2) -le 4 ] && [ $(d $b1 $b2) -le 4 ]; then
  echo "  [ok] the H.264 render decodes to the still's colour (within 4)"
else
  echo "  [FAIL] the H.264 render's colour drifted from the still"; fail=1
fi
[ $fail = 0 ] && echo "render_codecs: ok"
exit $fail
