#!/usr/bin/env bash
# B1191: build MEASUREMENT-ONLY packs from my tree's bytes (ThinkPad side).
# 1) ETC1 pack  -> must equal my tree's pin 1cdd31e7... (proof of correct rebuild)
# 2) RGBA8888 pack (measurement A/B, v2, never installed, never shipped)
set -x
WT=/home/user/src/willeq-wt/kb-b1191-green-channel
BIN=/home/user/willeq-builds/kb-b1191-green-channel/build/bin
UIFILES=/mnt/projects/EverQuestP1999/uifiles/default
EV=/home/user/kb-packs/kb-b1191-green-channel
rm -rf "$EV"; mkdir -p "$EV/stage/atlas" "$EV/etcl" "$EV/rgba"
"$BIN/ui_atlas_builder" --output "$EV/stage/atlas" --uifiles-path "$UIFILES" > "$EV/ui_atlas.log" 2>&1 || { echo UI_ATLAS_FAILED; cat "$EV/ui_atlas.log"; exit 1; }
ln -sfn "$WT/data/textures" "$EV/stage/textures"
"$BIN/eqe1_builder" --bmp-dir "$EV/stage/atlas" --bmp-dir "$EV/stage/textures" \
  --rgba8888-texture ui_atlas --output "$EV/etcl" --name willeq > "$EV/pack_etcl.log" 2>&1 || { echo ETCL_FAILED; cat "$EV/pack_etcl.log"; exit 1; }
"$BIN/eqe1_builder" --bmp-dir "$EV/stage/atlas" --bmp-dir "$EV/stage/textures" \
  --payload-format rgba8888 --output "$EV/rgba" --name willeq > "$EV/pack_rgba.log" 2>&1 || { echo RGBA_FAILED; cat "$EV/pack_rgba.log"; exit 1; }
sha256sum "$EV/etcl/willeq.eqe1" "$EV/rgba/willeq.eqe1"
ls -la "$EV/etcl" "$EV/rgba"
echo PACKS_DONE
