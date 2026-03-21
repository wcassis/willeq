#!/bin/bash
# Analyze animated texture behavior from a willeq log at TRACE level.
# Extracts: texture discovery, buffer matching, frame swaps, and potential mismatches.
# Usage: bash scripts/analyze_anim_textures.sh r02.log

LOG="${1:-r02.log}"
OUT="anim_tex_analysis.txt"

echo "=== Animated Texture Analysis ===" > "$OUT"
echo "Log: $LOG ($(wc -l < "$LOG") lines, $(du -h "$LOG" | cut -f1))" >> "$OUT"
echo "" >> "$OUT"

# 1. Texture discovery (initialize)
echo "=== 1. TEXTURE DISCOVERY (initialize) ===" >> "$OUT"
grep -n "initialize\[.*discovered\|initialize.*registered\|initialize.*total=" "$LOG" >> "$OUT"
echo "" >> "$OUT"

# 2. Water detection
echo "=== 2. WATER DETECTION ===" >> "$OUT"
grep -n "water animation\|detectWater" "$LOG" >> "$OUT"
echo "" >> "$OUT"

# 3. addMesh phase1 matches (existing anim matched to buffer)
echo "=== 3. addMesh PHASE1 BUFFER MATCHES ===" >> "$OUT"
echo "Count: $(grep -c "addMesh phase1.*matched existing" "$LOG")" >> "$OUT"
grep -n "addMesh phase1.*matched existing" "$LOG" | head -50 >> "$OUT"
echo "..." >> "$OUT"
echo "" >> "$OUT"

# 4. addMesh phase2 new discoveries
echo "=== 4. addMesh PHASE2 NEW DISCOVERIES ===" >> "$OUT"
grep -n "addMesh phase2.*NEW\|addMesh.*registered NEW" "$LOG" >> "$OUT"
echo "" >> "$OUT"

# 5. addMesh phase2 known matches
echo "=== 5. addMesh PHASE2 KNOWN MATCHES ===" >> "$OUT"
echo "Count: $(grep -c "addMesh phase2.*matched known" "$LOG")" >> "$OUT"
grep -n "addMesh phase2.*matched known" "$LOG" | head -50 >> "$OUT"
echo "..." >> "$OUT"
echo "" >> "$OUT"

# 6. addMesh result summaries
echo "=== 6. addMesh RESULT SUMMARIES ===" >> "$OUT"
echo "Total regions with matches: $(grep -c "addMesh result:" "$LOG")" >> "$OUT"
echo "Unique match patterns:" >> "$OUT"
grep "addMesh result:" "$LOG" | sed 's/.*addMesh result: //' | sort | uniq -c | sort -rn >> "$OUT"
echo "" >> "$OUT"

# 7. addSceneNode matches (should only be for objects, not regions)
echo "=== 7. addSceneNode MATCHES ===" >> "$OUT"
echo "Count: $(grep -c "addSceneNode.*materials matched" "$LOG")" >> "$OUT"
grep -n "addSceneNode.*materials matched" "$LOG" | head -20 >> "$OUT"
echo "" >> "$OUT"

# 8. Texture names on matched buffers — what's being swapped
echo "=== 8. BUFFER TEXTURE NAMES MATCHED ===" >> "$OUT"
grep "addMesh phase1.*tex=\|addMesh phase2.*tex=\|addSceneNode.*tex=" "$LOG" | \
    sed "s/.*tex='//" | sed "s/'.*//" | sort | uniq -c | sort -rn >> "$OUT"
echo "" >> "$OUT"

# 9. All known animated texture names
echo "=== 9. REGISTERED ANIMATED TEXTURES ===" >> "$OUT"
grep "registered.*anim\|initialize.*registered" "$LOG" >> "$OUT"
echo "" >> "$OUT"

# 10. Object animated texture matches (Step 11 fixup)
echo "=== 10. OBJECT FIXUP ANIMATED MATCHES ===" >> "$OUT"
grep -n "fixup.*animated\|fixup pass.*processed\|addMesh.*NEW.*object\|fire1" "$LOG" | head -20 >> "$OUT"
echo "" >> "$OUT"

# 11. Check for any warnings about frame loads
echo "=== 11. WARNINGS (frame load failures) ===" >> "$OUT"
grep -n "Failed to load frame\|frame texture not found\|animated but only" "$LOG" >> "$OUT"
echo "" >> "$OUT"

echo "Analysis written to $OUT"
wc -l "$OUT"
