#!/bin/sh
# R-RENDER-6 through the real encoders: every codec and profile Deliver offers is rendered by the
# CLI (the grammar the GUI dispatches) and read back with ffprobe — codec, profile, pixel format,
# the BT.709 tags (D-9, D-11), size and rate, and each output transform's tags (R-COLOR-4). Then the colour itself: an H.264 render, decoded by FFmpeg
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
# D-11: a ProRes master says its colour in its own frame header, where readers look first
check "prores standard" prores_standard.mov color_primaries bt709
check "prores standard" prores_standard.mov color_transfer bt709
check "dnxhr hqx" dnxhr_hqx.mov color_space bt709

# R-COLOR-4: an output transform tags what it made — HDR as Rec.2100 with its signalling
render --format h265 --output pq --peak 1000 --out pq.mkv
check "pq" pq.mkv pix_fmt yuv420p10le
check "pq" pq.mkv color_primaries bt2020
check "pq" pq.mkv color_transfer smpte2084
check "pq" pq.mkv color_space bt2020nc
md=$("$FFPROBE" -v error -select_streams v:0 -read_intervals "%+#1" -show_frames -show_entries frame=side_data_list -of compact pq.mkv | grep -c "max_luminance=10000000/10000" || true)
if [ "$md" -ge 1 ]; then echo "  [ok] pq: the mastering display (Rec.2020, 1000 cd/m²) is in the stream"; else echo "  [FAIL] pq: no mastering display metadata"; fail=1; fi
render --format prores --output pq --out pq.mov
check "pq prores" pq.mov color_transfer smpte2084
check "pq prores" pq.mov color_primaries bt2020
render --format h265 --output hlg --out hlg.mp4
check "hlg" hlg.mp4 color_transfer arib-std-b67
check "hlg" hlg.mp4 pix_fmt yuv420p10le
render --format h264 --output srgb --out srgb.mp4
check "srgb" srgb.mp4 color_transfer iec61966-2-1
render --format prores --output p3d65 --out p3.mov
check "p3d65" p3.mov color_primaries smpte432
if "$CC" project open mv.isp : render --timeline main --format h264 --output pq --out bad.mp4 > /dev/null 2>&1; then
  echo "  [FAIL] an 8-bit HDR render was accepted"; fail=1
else echo "  [ok] an 8-bit HDR render is refused"; fi
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
# R-PLAY-3: the video unit. Where VA-API answers, a hardware render is a real H.264 with the same
# tags and the same colour; where it does not, the render still finishes, in software, and says so.
spec() { "$CC" project open mv.isp : render --timeline main "$@" : wait render.done : state print --json | tr ',' '\n' | grep '"spec"' | tail -1; }
s=$(spec --encoder hardware --out hw.mp4)
echo "  hardware: $s"
check hardware hw.mp4 codec_name h264
check hardware hw.mp4 color_space bt709
x264() { LC_ALL=C grep -a -c 'x264 - core' "$1" || true; }   # libx264 signs its stream; the video unit does not
if echo "$s" | grep -q "encoded in software"; then
  echo "  [skip] no VA-API video unit on this machine: the colour check ran on the software fallback"
elif [ "$(x264 hw.mp4)" = 0 ]; then echo "  [ok] hardware: encoded by the video unit, not libx264"
else echo "  [FAIL] hardware: the file carries libx264's signature"; fail=1; fi
c=$(px hw.mp4); echo "  still $a  hardware $c"
set -- $c; r3=$1; g3=$2; b3=$3
if [ $(d $r1 $r3) -le 6 ] && [ $(d $g1 $g3) -le 6 ] && [ $(d $b1 $b3) -le 6 ]; then
  echo "  [ok] the hardware render decodes to the still's colour (within 6)"
else
  echo "  [FAIL] the hardware render's colour drifted from the still"; fail=1
fi
s=$(INTERSTELLAR_VAAPI_DEVICE=/dev/dri/none spec --encoder hardware --out fb.mp4)
check fallback fb.mp4 codec_name h264
if echo "$s" | grep -q "encoded in software" && [ "$(x264 fb.mp4)" = 1 ]; then echo "  [ok] no video unit: rendered in software, and said"; else echo "  [FAIL] fallback not said: $s"; fail=1; fi

# R-AUD-9: a timeline with sound muxes the master — AAC beside H.264, 24-bit PCM beside ProRes
"$FFMPEG" -loglevel error -f lavfi -i "sine=frequency=1000:sample_rate=44100:duration=2" -c:a pcm_s16le tone.wav
"$CC" project open mv.isp : audio track add --name music : audio clip add --track music --src tone.wav --at 0 : project save > /dev/null
render --format h264 --out sound.mp4
render --format prores --out sound.mov
astream() { "$FFPROBE" -v error -select_streams a:0 -show_entries "stream=$2" -of default=nw=1:nk=1 "$1" | head -1; }
for pair in "sound.mp4 aac" "sound.mov pcm_s24le"; do
  set -- $pair
  got=$(astream "$1" codec_name); rate=$(astream "$1" sample_rate); ch=$(astream "$1" channels)
  if [ "$got" = "$2" ] && [ "$rate" = 48000 ] && [ "$ch" = 2 ]; then echo "  [ok] $1: $got 48 kHz stereo"; else echo "  [FAIL] $1: audio $got $rate Hz $ch ch, wanted $2 48000 2"; fail=1; fi
done

[ $fail = 0 ] && echo "render_codecs: ok"
exit $fail
