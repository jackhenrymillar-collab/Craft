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

# M2: one demo per object (each with its default material unless named).
"$R" --out "$OUT/12_oak_bar_on_cords.wav"     --part Bar --hardness 0.35 --at 0.0,0 --length 3
"$R" --out "$OUT/13_steel_plate_hung.wav"     --part Plate --hardness 0.5 --at 0.45,0.7 --length 6
"$R" --out "$OUT/14_bronze_bowl_resting.wav"  --part Bowl --hardness 0.15 --at 0.9,0 --length 8
"$R" --out "$OUT/15_bronze_bell_hung.wav"     --part Bell --mount Hung --striker Mallet --hardness 0.95 --at 0.9,0 --length 8
"$R" --out "$OUT/16_aluminium_tube_chime.wav" --part Tube --striker Hammer --hardness 0.7 --at 0.85,0 --length 6
"$R" --out "$OUT/17_steel_spring.wav"         --part Spring --hardness 0.8 --at 0.3,0 --length 3
"$R" --out "$OUT/18_oak_box_palm_finger.wav"  --part Box --striker Hand --hardness 0.1 --at 0.0,0 --hits 0:0.9 --length 2
"$R" --out "$OUT/19_oak_box_fingertips.wav"   --part Box --striker Hand --hardness 0.9 --at 0.8,1.57 --hits 0:0.7,0.25:0.5,0.5:0.8 --length 2

# Strikers: the same hung steel plate hit four ways.
"$R" --out "$OUT/20_plate_mallet.wav" --part Plate --striker Mallet --hardness 0.5 --at 0.45,0.7 --length 4
"$R" --out "$OUT/21_plate_hammer.wav" --part Plate --striker Hammer --hardness 0.8 --at 0.45,0.7 --length 4
"$R" --out "$OUT/22_plate_hand.wav"   --part Plate --striker Hand   --hardness 0.3 --at 0.45,0.7 --length 4
"$R" --out "$OUT/23_plate_drop.wav"   --part Plate --striker Drop   --hardness 0.9 --at 0.45,0.7 --hits 0:1 --length 4

# Drop on other parts, and the air mic.
"$R" --out "$OUT/24_drop_on_glass_bowl.wav" --part Bowl --material Glass --striker Drop --hardness 1 --at 0.3,0 --hits 0:0.5 --length 5
"$R" --out "$OUT/25_bowl_air_mic_0.5m.wav"   --part Bowl --hardness 0.3 --at 0.35,0.5 --air-mix 1 --mic-distance 0.5 --length 6
"$R" --out "$OUT/25b_bowl_contact_mic.wav"  --part Bowl --hardness 0.3 --at 0.35,0.5 --air-mix 0 --length 6
"$R" --out "$OUT/26_steel_bar_glock.wav" --part Bar --material Steel --hardness 0.7 --at 0.0,0 --length 4
