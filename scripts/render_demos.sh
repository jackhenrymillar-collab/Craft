#!/usr/bin/env bash
# Render the milestone demo hits with render_cli (WAV + spectrum PNG/CSV + JSON report each).
#   scripts/render_demos.sh [build-dir] [out-dir]
set -euo pipefail
BUILD_DIR="${1:-build}"
OUT="${2:-renders}"
CONFIG="${CONFIG:-Release}"
R="$BUILD_DIR/tools/render_cli/render_cli_artefacts/$CONFIG/render_cli"
[[ -x "$R" ]] || R="$(find "$BUILD_DIR" -type f -name render_cli -perm -u+x | head -1)"
mkdir -p "$OUT"

# M1: soft/hard mallet, centre/edge strike (fixed points, no spread, so they're comparable).
"$R" --out "$OUT/01_soft_mallet_centre.wav" --hardness 0.05 --at 0.0,0   --hits 0:0.8 --length 3
"$R" --out "$OUT/02_hard_mallet_centre.wav" --hardness 0.95 --at 0.0,0   --hits 0:0.8 --length 3
"$R" --out "$OUT/03_soft_mallet_edge.wav"   --hardness 0.05 --at 0.8,0.4 --hits 0:0.8 --length 3
"$R" --out "$OUT/04_hard_mallet_edge.wav"   --hardness 0.95 --at 0.8,0.4 --hits 0:0.8 --length 3

# Retrigger: a crescendo roll that keeps adding energy to the ringing head.
"$R" --out "$OUT/05_roll_retrigger.wav" --hardness 0.7 --spread 0.3 --position 0.4 --length 3 \
     --hits 0:0.3,0.12:0.35,0.24:0.4,0.36:0.45,0.48:0.5,0.6:0.6,0.72:0.7,0.84:0.8,0.96:1.0

# Treatments, all on the same edge hit as 04 for comparison.
"$R" --out "$OUT/06_age_mylar_damp.wav"   --hardness 0.95 --at 0.8,0.4 --age 1 --climate 1 --length 3
"$R" --out "$OUT/07_age_hide_dry.wav"     --hardness 0.95 --at 0.8,0.4 --material Hide --age 1 --climate 0 --length 3
"$R" --out "$OUT/08_deform_heavy.wav"     --hardness 0.95 --at 0.8,0.4 --deform 1 --deform-size 0.5 --deform-density 0.6 --length 3
"$R" --out "$OUT/09_twist_wrinkled.wav"   --hardness 0.95 --at 0.8,0.4 --twist 0.9 --twist-pivot 0.6 --twist-focus 0.5 --wrinkle 0.8 --length 3
"$R" --out "$OUT/10_puncture_ragged.wav"  --hardness 0.95 --at 0.8,0.4 --holes 0.5 --hole-size 0.6 --hole-scatter 0.8 --ragged 0.9 --length 3
"$R" --out "$OUT/11_everything.wav"       --hardness 0.95 --at 0.8,0.4 --age 0.7 --climate 0.8 --deform 0.6 --twist -0.5 --wrinkle 0.4 --holes 0.3 --ragged 0.6 --length 3
