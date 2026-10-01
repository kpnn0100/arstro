#!/bin/sh
# The real app over the real service, headless: make media, build a project with the CLI (the
# same grammar the GUI dispatches), then render every tab through the app's hooks.
#   live.sh <interstellar-cc> <interstellar_live_shots> <ffmpeg> <workdir>
set -eu
ISCC="$1"; LIVE="$2"; FFMPEG="$3"; DIR="$4"
rm -rf "$DIR"; mkdir -p "$DIR/footage"; cd "$DIR"
export HOME="$DIR" INTERSTELLAR_RECENTS="$DIR/recents"
"$FFMPEG" -loglevel error -f lavfi -i testsrc2=size=640x360:rate=24:duration=4 -pix_fmt yuv420p footage/a.mp4
"$FFMPEG" -loglevel error -f lavfi -i smptebars=size=640x360:rate=24:duration=3 -pix_fmt yuv420p footage/b.mp4
"$ISCC" project new mv.isp --res 640x360 : rack add footage/a.mp4 footage/b.mp4 \
  : set a.basic.exposure=0.4 a.basic.temp=5600 : track add --kind video \
  : clip add --track v0 --src a --in 0 --out 2 --at 0 --name shotA \
  : clip add --track v0 --src b --in 0 --out 2 --at 2 --name shotB \
  : transition add --between shotA,shotB --dur 0.5 \
  : timeline new social30 --base main : project save > /dev/null
"$LIVE" --project "$DIR/mv.isp" --outdir "$DIR/shots" --select a
