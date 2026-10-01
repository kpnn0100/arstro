#!/bin/sh
# Real containers for interstellar_host_tests: mp4, mov, mkv (H.264 / HEVC 10-bit / VP9), 29.97,
# and streams whose first timestamp is NOT zero (what froze MKV playback).
#   host_media.sh <interstellar_host_tests> <ffmpeg> <workdir>
set -eu
TEST="$1"; FFMPEG="$2"; DIR="$3"
rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"
src="testsrc2=size=320x180:rate=24:duration=4"
"$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libx264 -pix_fmt yuv420p plain.mp4
"$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libx264 -pix_fmt yuv420p plain.mov
"$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libx264 -pix_fmt yuv420p plain.mkv
"$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libx264 -pix_fmt yuv420p -output_ts_offset 3.5 offset.mkv
"$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libx264 -pix_fmt yuv420p -output_ts_offset 1.25 offset.mp4
"$FFMPEG" -loglevel error -f lavfi -i "testsrc2=size=320x180:rate=30000/1001:duration=4" -c:v libx264 -pix_fmt yuv420p ntsc.mkv
set -- plain.mp4 plain.mov plain.mkv offset.mkv offset.mp4 ntsc.mkv
if "$FFMPEG" -hide_banner -encoders 2>/dev/null | grep -q libx265; then
  "$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libx265 -pix_fmt yuv420p10le -x265-params log-level=error hevc10.mkv
  set -- "$@" hevc10.mkv
fi
if "$FFMPEG" -hide_banner -encoders 2>/dev/null | grep -q libvpx-vp9; then
  "$FFMPEG" -loglevel error -f lavfi -i "$src" -c:v libvpx-vp9 -deadline realtime vp9.mkv
  set -- "$@" vp9.mkv
fi
"$TEST" "$@"
