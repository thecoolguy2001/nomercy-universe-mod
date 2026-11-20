#!/bin/sh
# Run a ROM headlessly with a frame-timed script.
#   run.sh <rom> <script> <outdir> [extra mupen64plus args...]
set -e
T=/root/n64tools
ROM=$1; SCRIPT=$2; OUT=$3; shift 3
mkdir -p "$OUT" "$T/cfg"
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export NMDBG_SCRIPT="$SCRIPT" NMDBG_OUT="$OUT" NMDBG_VIDEO_SO="$T/nmdbg/video-nmdbg.so"
export NMDBG_CORE_SO="$T/mupen64plus-core/projects/unix/libmupen64plus.so.2.0.0"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
exec "$T/mupen64plus-ui-console/projects/unix/mupen64plus" \
    --corelib "$T/mupen64plus-core/projects/unix/libmupen64plus.so.2.0.0" \
    --datadir "$T/mupen64plus-core/data" --configdir "$T/cfg" \
    --plugindir "$T/nmdbg" \
    --gfx "$T/nmdbg/video-nmdbg.so" --input "$T/nmdbg/input-nmdbg.so" \
    --rsp "$T/mupen64plus-rsp-cxd4/projects/unix/mupen64plus-rsp-cxd4.so" \
    --audio dummy --noosd --nosaveoptions --emumode 1 \
    --set "rsp-cxd4[DisplayListToGraphicsPlugin]=False" \
    --set "rsp-cxd4[AudioListToAudioPlugin]=False" \
    --set "Video-AngrylionPlus[Parallel]=True" \
    --set "Core[SaveSRAMPath]=$T/saves" --set "Core[SaveStatePath]=$T/states" \
    "$@" "$ROM"
