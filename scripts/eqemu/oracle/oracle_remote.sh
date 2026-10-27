#!/usr/bin/env bash
# Runs on the paired oracle VM; no server DB writes here.
set -uo pipefail
ACTION=${1:-} DIR=${2:-} ORACLE_LANE=${3:-}
# B1073 (#393): oracle_session.sh passes the driving lane, so the lane-5-only
# fixes below (the NVIDIA GLX pin, the WM-less focus wrap) are inert on lanes
# 1-4, where the oracle is Xvfb + llvmpipe and a focus/force-NVIDIA change
# would break GL or hang a windowfocus --sync with no focusable window.
[[ $ACTION == selftest || ( -n $DIR && $DIR == /home/user/eq-oracle/sessions/* ) ]] || exit 65
export DISPLAY=:99 WINEPREFIX=/home/user/eq-oracle/wineprefix WINEARCH=win32 WINEDEBUG=-all LP_NUM_THREADS=1
# B1073 (#393): the NVIDIA GLX stack is the lane-5 standard. :99 registers
# ONLY NVIDIA's glxserver_nvidia (the generic Mesa glx module is disabled in
# /etc/X11/oracle/xorg.conf, docs/thinkpad.md), and the client's 32-bit
# libglvnd libGL.so.1 dispatcher is pinned to the same vendor so Wine's GLX
# goes to NVIDIA, never Mesa, regardless of which i386 vendor lib the
# dispatcher probes first. Lanes 1-4 (Xvfb + llvmpipe) have no NVIDIA vendor
# at all, so the pin is lane-5 only: there it would point the dispatcher at a
# vendor lib that does not exist.
if [[ $ORACLE_LANE == 5 ]]; then export __GLX_VENDOR_LIBRARY_NAME=nvidia; fi
source /mnt/projects/eq-oracle/scripts/ui-lib.sh
CLIENT=/home/user/eq-oracle/client
# B1134 (#454): the OBSERVER. A plan with `# observer=<Character>` runs a SECOND
# official client: the observer (a second PLAYER character, never a spawned
# NPC) on :99 -- the NVIDIA Xorg that the video grabs -- from its own client
# copy and Wine prefix, and the plan's login character (the caster) on a
# session Xvfb (llvmpipe; its frames are never measured). Rows prefixed `obs`
# drive the observer; every other row drives the caster. Setup:
# oracle_observer_setup.sh; recipe: docs/oracle_driver.md "The observer".
OBS_CLIENT=/home/user/eq-oracle/client-observer
OBS_PREFIX=/home/user/eq-oracle/wineprefix-observer
OBS_DISPLAY=:99
CASTER_DISPLAY=${ORACLE_CASTER_DISPLAY:-:98}
CASTER_CLIENT=$CLIENT CASTER_PREFIX=$WINEPREFIX
OBSERVER= ROLE=caster CASTER_CHARACTER=
use_role() {
    ROLE=$1
    if [[ $ROLE == observer ]]; then
        export DISPLAY=$OBS_DISPLAY WINEPREFIX=$OBS_PREFIX LP_NUM_THREADS=1
        CLIENT=$OBS_CLIENT CHARACTER=$OBSERVER
        if [[ $ORACLE_LANE == 5 ]]; then export __GLX_VENDOR_LIBRARY_NAME=nvidia; else unset __GLX_VENDOR_LIBRARY_NAME; fi
    else
        # The caster's Xvfb registers Mesa's GLX only. llvmpipe stays at ONE
        # thread: more JIT workers exhaust the 32-bit eqgame.exe's address
        # space ("LLVM ERROR: Unable to allocate section memory!",
        # /mnt/projects/eq-oracle/scripts/launch-client.sh).
        export DISPLAY=$CASTER_DISPLAY WINEPREFIX=$CASTER_PREFIX LP_NUM_THREADS=1 __GLX_VENDOR_LIBRARY_NAME=mesa
        CLIENT=$CASTER_CLIENT CHARACTER=$CASTER_CHARACTER
    fi
}
alive() { pgrep -x eqgame.exe >/dev/null && xdotool search --name '^EverQuest$' >/dev/null 2>&1; }
# B1073 (#393): the lane-5 rig has NO window manager, so after a re-render the
# client window does not hold input focus and motion clicks (jclick, the
# face_open_click drag) land on the root window and do nothing -- this is why
# Set Face stopped opening after the 2026-10-13 re-provision. Give the client
# the focus before every motion click. A failure is harmless: the click is
# still made (a lost focus costs one retry, an always-throwing helper would
# cost every run), so this never returns non-zero.
client_focus() {
    # B1134: bounded, so a display with no focusable window cannot hang it.
    timeout 5 xdotool windowfocus --sync --name '^EverQuest$' 2>/dev/null || :
}
# The wrap. ui-lib.sh (sourced above) defines jclick/jrclick; on lane 5 we
# redefine them so every motion click -- the facepick-rows open loop, the
# selection clicks, the hold verb, the login helper's jclicks -- gives the
# client window the focus first. Lanes 1-4 keep ui-lib's originals. The
# wrapper adds a few milliseconds per click (windowfocus --sync); on a
# no-WM rig that is the difference between a click that lands and one that
# does nothing.
if [[ $ORACLE_LANE == 5 ]]; then
    jclick() { client_focus; xdotool mousemove "$1" "$2"; sleep 0.6; xdotool mousedown 1; sleep 0.4; xdotool mousemove $(( $1 + 1 )) $(( $2 + 1 )); sleep 0.3; xdotool mouseup 1; }
    jrclick() { client_focus; xdotool mousemove "$1" "$2"; sleep 0.6; xdotool mousedown 3; sleep 0.4; xdotool mousemove $(( $1 + 1 )) $(( $2 + 1 )); sleep 0.3; xdotool mouseup 3; }
fi
shot() { import -window root -crop 1280x800+0+30 "$DIR/$1.png" 2>/dev/null; }
stamp() { date -u +%s.%3N; }
mark() { printf '%s\t%s\t%s\n' "$(stamp)" "$1" "$2" >>"$DIR/timeline.tsv"; }
assert_visible() {
    local expected=$1 name=$2
    if [[ $expected == window:EverQuest ]]; then
        xdotool search --name '^EverQuest$' >/dev/null 2>&1 && return 0
    else
        # B1059: with the broker up (template plans) the glyph atlas reads the
        # known text regions first; only its match decides. Otherwise, and
        # whenever it does not match, the legacy tesseract screen_text does.
        if [[ -n ${TEXT_BROKER:-} ]] && read_text screen 0,0,1,1 "$expected" "$name" column5 &&
            [[ $RT_DECIDER == atlas && $RT_OK == 1 ]]; then
            return 0
        fi
        if screen_text | grep -qiE "$expected"; then
            [[ -n ${TEXT_BROKER:-} ]] && text_log "$name" column5 screen tesseract 1 "screen_text matched: $expected" - tesseract
            return 0
        fi
        [[ -n ${TEXT_BROKER:-} ]] && text_log "$name" column5 screen unknown 0 "no match: $expected" - -
    fi
    shot "$name-assertion-failed" || :
    echo "screen assertion failed: $name expected $expected" >&2
    return 65
}
race_y() { case "$1" in
    Barbarian) echo 291;; DarkElf) echo 315;; Dwarf) echo 339;; Erudite) echo 363;;
    Gnome) echo 411;; HalfElf) echo 435;; Halfling) echo 459;; HighElf) echo 483;;
    Human) echo 507;; Iksar) echo 531;; Ogre) echo 555;; Troll) echo 579;;
    WoodElf) echo 627;; *) return 65;; esac; }
class_y() { case "$1" in
    Bard) echo 291;; Cleric) echo 363;; Druid) echo 387;; Enchanter) echo 411;;
    Magician) echo 435;; Monk) echo 459;; Necromancer) echo 483;; Paladin) echo 507;;
    Ranger) echo 531;; Rogue) echo 555;; ShadowKnight) echo 579;; Shaman) echo 603;;
    Warrior) echo 627;; Wizard) echo 651;; *) return 65;; esac; }
city_y() { case "$1" in
    Barbarian) echo 320;; DarkElf) echo 386;; Dwarf) echo 452;; Erudite) echo 276;;
    Gnome) echo 518;; HalfElf|Human) echo 298;; Halfling) echo 342;;
    HighElf) echo 496;; Iksar) echo 540;; Ogre) echo 430;; Troll) echo 408;;
    WoodElf) echo 474;; *) return 65;; esac; }
gender_x() { case "$1" in Male) echo 105;; Female) echo 169;; *) return 65;; esac; }
name_filled() {
    # The Name field on the deity/city page (file y 262-274, x 30-228). A typed
    # name's glyphs make the box's gray standard deviation 0.172 and an empty
    # field's uniform background 0.094 (measured on real stills:
    # charcreate-fix4's 'Oracleface' vs barbarian-fix2h's empty field), so
    # >= 0.13 means text is present. Without this, a lost xdotool keystroke
    # sails straight to submit and the client answers with a "Name is not
    # valid" dialog (fix2h) instead of a create packet -- the report then
    # finds zero OP_CharacterCreate and the run dies with no evidence of why.
    # B1058: that crop is root-relative (ImageMagick honours the +0+30 page
    # offset), so it reads FILE rows 234-244: the deity list's first row
    # (AGNOSTIC), not the Name field at file rows 38-55 (review/B1058/README.md
    # "name_filled"). Template-mode creates pass the field rect located from
    # the Get Name button instead (name_filled_at); this legacy path is kept
    # byte-for-byte so B1048's accepted plans are unchanged.
    local image="$DIR/name-check.png" std
    import -window root -crop 1280x800+0+30 "$image" 2>/dev/null || return 65
    std=$(convert "$image" -crop "190x10+30+264" +repage -colorspace Gray \
        -format '%[fx:standard_deviation]' info:) || return 65
    awk -v std="$std" 'BEGIN {exit !(std >= 0.13)}'
}
name_filled_at() {
    # B1058: the Name field's text area relative to the located Get Name
    # button centre (root gx,gy): file x gx-156..gx-46, y gy-30-5..+10. Typed
    # field std 0.26-0.31, empty 0.0086 (lane-5 name-check.png, fix2l/fix2i
    # name-check.png, UIGAP-ORACLE/204/11 vs fix2h/02, 204/07, 204/09); the
    # threshold is their midpoint, 0.134. +repage first: FILE coordinates.
    local gx=$1 gy=$2 image="$DIR/name-check.png" std
    import -window root -crop 1280x800+0+30 "$image" 2>/dev/null || return 65
    std=$(convert "$image" +repage -crop "110x10+$((gx-156))+$((gy-35))" +repage \
        -colorspace Gray -format '%[fx:standard_deviation]' info:) || return 65
    awk -v std="$std" 'BEGIN {exit !(std >= 0.134)}'
}
# --- B1059 text reads ---------------------------------------------------------
# read_text asks the broker (driver host) to read a rect of a fresh shot with
# the glyph atlas, tesseract as the fallback (oracle_locate.py read_answer).
# Request "read:<mode>:<x,y,w,h>:<expected as hex>", answer "<seq> TEXT
# <decider> <ok> <score> <path> <text as hex>"; decider atlas|tesseract|unknown.
# An unanswered or ERR read is decider 'unknown': the caller keeps its legacy
# check. Every read is a row of text-reads.tsv (the report's "Text reads").
RT_DECIDER= RT_OK= RT_TEXT=
text_log() {
    # step what mode decider ok text score path
    local t=${6//$'\t'/ }
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" "${t//$'\n'/ | }" "$7" "$8" \
        >>"$DIR/text-reads.tsv"
}
read_text() {
    local mode=$1 rect=$2 expect=$3 step=$4 what=$5 seq=$LOCATE_SEQ hex i aseq kind score path thex
    [[ $mode =~ ^(edit|line|screen)$ && $rect =~ ^[0-9]+,[0-9]+,[0-9]+,[0-9]+$ ]] || return 65
    hex=$(printf '%s' "$expect" | od -An -tx1 | tr -d ' \n')
    (( ${#hex} <= 512 )) || return 65
    LOCATE_SEQ=$((LOCATE_SEQ+1))
    RT_DECIDER=unknown RT_OK=0 RT_TEXT=
    mkdir -p "$DIR/locate" || return 65
    import -window root -crop 1280x800+0+30 "$DIR/locate/shot-$seq.png" 2>/dev/null || return 65
    printf 'read:%s:%s:%s\n' "$mode" "$rect" "$hex" >"$DIR/locate/req-$seq.tmp" || return 65
    mv "$DIR/locate/req-$seq.tmp" "$DIR/locate/req-$seq.tsv" || return 65
    for ((i = 0; i < LOCATE_WAIT_S * 5; i++)); do
        [[ -s $DIR/locate/ans-$seq.tsv ]] && break
        sleep 0.2
    done
    if [[ -s $DIR/locate/ans-$seq.tsv ]]; then
        read -r aseq kind RT_DECIDER RT_OK score path thex <"$DIR/locate/ans-$seq.tsv"
        if [[ $aseq != "$seq" || $kind != TEXT || ! $RT_DECIDER =~ ^(atlas|tesseract|unknown)$ ||
              ! $RT_OK =~ ^[01]$ || ! ${thex:-} =~ ^[0-9a-f]*$ ]]; then
            RT_DECIDER=unknown RT_OK=0 score=- path=- thex=
        fi
        RT_TEXT=$(printf '%b' "$(sed 's/../\\x&/g' <<<"${thex:-}")")
    else
        score=- path=-
    fi
    text_log "$step" "$what" "$mode" "$RT_DECIDER" "$RT_OK" "$RT_TEXT" "${score:--}" "${path:--}"
    return 0
}
name_verify() {
    # Template mode's Name check: the field (file rect 165 px left of and 8 px
    # above the Get Name centre; label_crops.py) read by the atlas must equal
    # the typed name. tesseract only ever confirms; an atlas-less rig or an
    # UNKNOWN read keeps the presence check (name_filled_at) unchanged.
    local gx=$1 gy=$2 typed=$3 step=$4
    read_text edit "$((gx-165)),$((gy-38)),118,16" "$typed" "$step" name-field || return 65
    case $RT_DECIDER in
        atlas|tesseract) [[ $RT_OK == 1 ]] ;;
        *) name_filled_at "$gx" "$gy" ;;
    esac
}
selected_at() {
    # The selected label is yellow; enabled but unselected labels are white.
    # ImageMagick preserves import's root-screen virtual page offset (+0,+30).
    # Both click and crop therefore use the table's root-screen coordinates.
    # Detection: make yellow transparent and read the box's mean alpha. A
    # selected row measures 0.90-0.95 and a white-text row 1.0000 (both
    # measured on real stills), so < 0.98 means the row holds the selection.
    # The old negate-mean read ~0.99 in BOTH states (0.9919 unselected vs
    # 1.0000 selected in fix2g) and could never tell them apart, which is how
    # fix4 (class=16), fix2e (race=7) and fix2g (race=Iksar) slipped through.
    local label=$1 x=$2 y=$3
    local image="$DIR/selected-$label.png" mean
    import -window root -crop 1280x800+0+30 "$image" 2>/dev/null || return 65
    mean=$(convert "$image" -crop "95x18+$((x-45))+$((y-9))" +repage \
        -fuzz 30% -transparent yellow \
        -format '%[fx:mean.alpha]' info:) || return 65
    awk -v mean="$mean" 'BEGIN {exit !(mean < 0.98)}'
}
select_once() {
    # Click the row, verify it is selected (yellow alpha, selected_at), and
    # on a failed verification wait for the list to settle, then re-click
    # once. The race and class lists share one selection cursor, the screen
    # restores the previous session's selection, and the restore animation
    # scrolls each list to center that cursor: the label at a fixed root row
    # changes with the restored scroll state (measured: fix2j's class click
    # at 627 landed on Rogue; fix2k's race list had scrolled so 291 showed
    # the Froglok row). A RAPID re-click mid-re-render moves the shared
    # cursor to a different row (fix4 class=16, fix2e race=7), so the retry
    # waits 8 s for the restore scroll to settle before its one retry. A
    # selected-label OCR read-back was considered and rejected: the small
    # white labels on the dark list background do not survive the rig's
    # screen_text preprocessing (gray, 200%, threshold 60%) even at psm 7,
    # so it cannot gate reliably. What IS reliable, in order: (1) the
    # yellow-alpha verification at the requested row, (2) one settled
    # retry, (3) FAIL -- the run gets re-driven instead of creating the
    # wrong identity; the decoded create packet (oracle_create.py) is the
    # final authority.
    local label=$1 x=$2 y=$3
    jclick "$x" "$y"; sleep 3
    if selected_at "$label" "$x" "$y"; then return 0; fi
    sleep 8
    jclick "$x" "$y"; sleep 3
    if ! selected_at "$label" "$x" "$y"; then
        shot "select-failed-$label" || :
        echo "selection failed: $label at $x,$y" >&2
        return 65
    fi
}
# --- B1058 template clicks ---------------------------------------------------
# The oracle box has no NumPy/OpenCV, so locating runs on the driver host.
# locate_template takes a root shot into $DIR/locate/shot-N.png, publishes
# req-N.tsv (the template name) and waits for ans-N.tsv. The driver's broker
# (oracle_locate.py broker) fetches each request and delivers the previous
# answer in ONE ssh exchange (broker_exchange below). The answer is FRAME
# pixels of the shot as Pillow reads it; import's crop is root +0+30, so the
# root point is (frame x, frame y + 30) (review/B1058/README.md "Coordinates").
LOCATE_SEQ=0 LOC_X= LOC_Y= LOC_SCORE= LOC_PATH= SETTLE_RETRIES=0
LOCATE_WAIT_S=${LOCATE_WAIT_S:-60}
locate_template() {
    # 0 found (LOC_X/LOC_Y = root centre), 1 best score below min_score, 65 error.
    local tname=$1 seq=$LOCATE_SEQ i aseq fx fy found
    [[ $tname =~ ^[A-Za-z0-9_-]+$ ]] || return 65
    LOCATE_SEQ=$((LOCATE_SEQ+1))
    LOC_X= LOC_Y= LOC_SCORE= LOC_PATH=
    mkdir -p "$DIR/locate" || return 65
    import -window root -crop 1280x800+0+30 "$DIR/locate/shot-$seq.png" 2>/dev/null || return 65
    printf '%s\n' "$tname" >"$DIR/locate/req-$seq.tmp" || return 65
    mv "$DIR/locate/req-$seq.tmp" "$DIR/locate/req-$seq.tsv" || return 65
    for ((i = 0; i < LOCATE_WAIT_S * 5; i++)); do
        [[ -s $DIR/locate/ans-$seq.tsv ]] && break
        sleep 0.2
    done
    if [[ ! -s $DIR/locate/ans-$seq.tsv ]]; then
        echo "locate $tname: no broker answer in ${LOCATE_WAIT_S}s" >&2
        return 65
    fi
    read -r aseq fx fy LOC_SCORE LOC_PATH found <"$DIR/locate/ans-$seq.tsv"
    if [[ $aseq != "$seq" || ! $fx =~ ^-?[0-9]+$ || ! $fy =~ ^-?[0-9]+$ || ! $found =~ ^[01]$ ]]; then
        echo "locate $tname: broker answered '$(cat "$DIR/locate/ans-$seq.tsv")'" >&2
        return 65
    fi
    LOC_X=$fx LOC_Y=$((fy + 30))
    [[ $found == 1 ]]
}
template_log() {
    # step template root_x root_y score path verify settle_retries
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$@" >>"$DIR/template-clicks.tsv"
}
click_template() {
    local tname=$1 off=$2 step=$3 dx dy rc
    [[ $off == 0 || -z $off ]] && off=0,0
    [[ $off =~ ^-?[0-9]+,-?[0-9]+$ ]] || return 65
    IFS=, read -r dx dy <<<"$off"
    locate_template "$tname"; rc=$?
    if (( rc != 0 )); then
        template_log "$step" "$tname" "${LOC_X:--}" "${LOC_Y:--}" "${LOC_SCORE:--}" "${LOC_PATH:--}" not-found 0
        shot "$step-template-missing-$tname" || :
        echo "click-template $tname: not located (score ${LOC_SCORE:-none})" >&2
        return 65
    fi
    jclick "$((LOC_X + dx))" "$((LOC_Y + dy))"
    template_log "$step" "$tname" "$((LOC_X + dx))" "$((LOC_Y + dy))" "$LOC_SCORE" "$LOC_PATH" clicked 0
}
wait_template() {
    # Poll (ui-lib RULE 2) until the template scores >= its min_score.
    local tname=$1 ms=$2 step=$3 end rc
    [[ $ms =~ ^[0-9]+$ ]] || return 65
    end=$(( $(date +%s%3N) + ms ))
    while :; do
        locate_template "$tname"; rc=$?
        (( rc == 65 )) && return 65
        if (( rc == 0 )); then
            template_log "$step" "$tname" "$LOC_X" "$LOC_Y" "$LOC_SCORE" "$LOC_PATH" seen 0
            return 0
        fi
        (( $(date +%s%3N) >= end )) && break
        sleep 0.5
    done
    template_log "$step" "$tname" "${LOC_X:--}" "${LOC_Y:--}" "${LOC_SCORE:--}" "${LOC_PATH:--}" timeout 0
    shot "$step-wait-template-timeout-$tname" || :
    echo "wait-template $tname: not seen in ${ms} ms" >&2
    return 65
}
selected_poll() {
    # selected_at polled for up to ~12 tries instead of a fixed 3 s sleep.
    local label=$1 x=$2 y=$3 i
    for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
        selected_at "$label" "$x" "$y" && return 0
        sleep 0.5
    done
    return 1
}
select_template() {
    # select_once with the row LOCATED instead of read from the table. The
    # settle retry is kept and COUNTED (SETTLE_RETRIES, template-clicks.tsv)
    # so a run shows whether templates made it unnecessary.
    local tname=$1 label=$2 step=$3 verify=first retries=0
    if ! locate_template "$tname"; then
        template_log "$step" "$tname" "${LOC_X:--}" "${LOC_Y:--}" "${LOC_SCORE:--}" "${LOC_PATH:--}" not-found 0
        shot "select-missing-$label" || :
        echo "select $label: template $tname not located" >&2
        return 65
    fi
    jclick "$LOC_X" "$LOC_Y"
    if ! selected_poll "$label" "$LOC_X" "$LOC_Y"; then
        template_log "$step" "$tname" "$LOC_X" "$LOC_Y" "$LOC_SCORE" "$LOC_PATH" first-failed 0
        retries=1 verify=retry
        SETTLE_RETRIES=$((SETTLE_RETRIES+1))
        sleep 8
        if ! locate_template "$tname"; then
            template_log "$step" "$tname" "${LOC_X:--}" "${LOC_Y:--}" "${LOC_SCORE:--}" "${LOC_PATH:--}" not-found 1
            return 65
        fi
        jclick "$LOC_X" "$LOC_Y"
        if ! selected_poll "$label" "$LOC_X" "$LOC_Y"; then
            template_log "$step" "$tname" "$LOC_X" "$LOC_Y" "$LOC_SCORE" "$LOC_PATH" failed 1
            shot "select-failed-$label" || :
            echo "selection failed: $label at $LOC_X,$LOC_Y" >&2
            return 65
        fi
    fi
    template_log "$step" "$tname" "$LOC_X" "$LOC_Y" "$LOC_SCORE" "$LOC_PATH" "$verify" "$retries"
    SEL_X=$LOC_X SEL_Y=$LOC_Y
}
broker_exchange() {
    # The remote half of one broker ssh exchange: store the answer to the
    # previous request (if any), then wait up to BROKER_WAIT_S for request
    # <next> and stream it: "REQ<TAB>n<TAB>name" + newline + the shot's PNG
    # bytes. Prints IDLE when nothing was requested in time.
    local next=$1 answer=$2 aseq i
    [[ $next =~ ^[0-9]+$ ]] || return 65
    mkdir -p "$DIR/locate" || return 65
    if [[ -n $answer ]]; then
        [[ $answer =~ ^[0-9]+\ (-?[0-9]+\ -?[0-9]+\ -?[0-9]+\.[0-9]+\ (cv2|numpy)\ [01]|ERR|TEXT\ (atlas|tesseract|unknown)\ [01]\ [0-9]+\.[0-9]{4}\ (cv2|numpy|tesseract|-)\ [0-9a-f]{0,256})$ ]] || return 65
        aseq=${answer%% *}
        printf '%s\n' "$answer" >"$DIR/locate/ans-$aseq.tmp" || return 65
        mv "$DIR/locate/ans-$aseq.tmp" "$DIR/locate/ans-$aseq.tsv" || return 65
    fi
    for ((i = 0; i < ${BROKER_WAIT_S:-20} * 5; i++)); do
        if [[ -f $DIR/locate/req-$next.tsv ]]; then
            printf 'REQ\t%s\t%s\n' "$next" "$(head -1 "$DIR/locate/req-$next.tsv")"
            cat "$DIR/locate/shot-$next.png"
            return 0
        fi
        sleep 0.2
    done
    echo IDLE
}
expected_for() {
    local verb=$1 target=$2 optional=${3:-}
    if [[ -n $optional ]]; then echo "$optional"; return; fi
    case $verb in
        login) echo 'Character Select|Create a New Character' ;;
        # "Click on a character" mis-OCR'd on this rig ("Click an a
        # character", measured on the a2 assertion-failed still, psm 6);
        # the tail of the same line reads cleanly and is unique to the
        # ExploreModeWindow (EXW_InfoLabel1, EQUI_CharacterSelect.xml).
        explore-open) echo 'select that character' ;;
        explore-esc) echo 'Create a New Character' ;;
        hold-d|orbit|wheel) echo 'select that character' ;;
        enter-press|camp-quit) echo window:EverQuest ;;
        zone) echo window:EverQuest ;;
        open) case $target in
            hotbar) echo 'Hot Buttons|Hotbutton' ;;
            inventory) echo Inventory ;;
            character-create) echo 'Character Creation' ;;
            *) return 65 ;;
            esac ;;
        random) echo 'Random|Accept|Cancel' ;;
        accept|cancel) echo 'Character Creation' ;;
        create) echo window:EverQuest ;;
        open-creation|open-face) echo 'Character Creation' ;;
        # The capture-leg verbs leave the creation slot OPEN (page 0, no
        # The capture-leg verbs leave the creation slot OPEN (page 0, no
        # submission). facepick-rows' postcondition is its OWN modal's
        # button strip (Random/Accept/Cancel -- the one region the lane-5
        # OCR reads reliably, and the same gate face_open uses): the creation
        # page's full-screen text is too flaky under psm 6 (heading and
        # bottom row both miss intermittently, 2026-10-14 pilots). race-table
        # and close-creation stay on the character-select/creation screens,
        # never a zone, so a window check would be wrong; they use the weak
        # 'Character' substring (both "Character Select" and "Character
        # Creation" carry it -- the most reliably-read word on the rig). The
        # STRONG state checks live in-driver: facepick-rows requires
        # face_open before probing; close-creation requires the creation
        # heading to have disappeared.
        race-table) echo 'Character' ;;
        close-creation) echo window:EverQuest ;;
        facepick-rows) echo 'Random|Accept|Cancel' ;;
        *) echo "${optional:-window:EverQuest}" ;;
    esac
}
face_open() { face_buttons_text '200x55+0+485' | grep -qiE 'Random|Accept|Cancel'; }
# B888-CC1 (#204): the Set Face OPEN click. matrix3 (the reference stock
# capture, oracle/matrix_capture3.sh, which rendered all 26 pairs' picker
# rows) opens Set Face with a net-ZERO drag: mousedown, move +1,+1, move BACK
# to the start, then mouseup -- its comment is "the net-zero drag keeps the
# popup stationary". The ui-lib jclick used by every earlier capture leg is a
# 1px NET drag (mousedown, move +1,+1, mouseup, no move-back), and on this
# client that opens the modal in the half-open "FACE + buttons only" state
# with the picker LIST never rendered (the full26/pilot failure: all pairs
# stilled showing only the FACE row). The net-zero drag is the one matrix3
# proved renders the full list, so the open uses exactly its sequence.
# B1073 (#393): the open shape. On lanes 1-4 this is matrix3's net-ZERO drag
# (the sequence that reference capture used). On lane 5 it is the
# focus-wrapped jclick instead: since the 2026-10-13 re-provision the
# net-zero drag does NOT open Set Face on this rig (FINDINGS-setface-
# regression.txt measured it "NO (5 tries)"), while the jclick 1px-net drag
# WITH an explicit windowfocus DOES (the same table: "jclick WITH explicit
# xdotool windowfocus -> YES"). The lane-5 jclick (defined above) already
# takes the focus, so the open is just the focus-wrapped 1px click.
face_open_click() {
    local x=$1 y=$2
    if [[ $ORACLE_LANE == 5 ]]; then
        jclick "$x" "$y"
        return 0
    fi
    xdotool mousemove "$x" "$y"
    sleep 0.4
    xdotool mousemove "$((x+1))" "$y"
    sleep 0.2
    xdotool mousemove "$x" "$y"
    sleep 0.4
    xdotool mousedown 1
    sleep 0.35
    xdotool mousemove "$((x+1))" "$((y+1))"
    sleep 0.3
    xdotool mousemove "$x" "$y"  # net-zero drag keeps the popup stationary
    sleep 0.2
    xdotool mouseup 1
    sleep 0.3
}
face_buttons_text() {
    # The modal's own three bottom buttons (window at 1280x1024: (0,374)
    # 175x215 image-space, i.e. (0,404) root; Random/Accept/Cancel at root
    # y 515-540 = image 485-510, B888-CC1: the lane-5 stills put Random at
    # image ~485). A crop of THAT strip is the open-gate: whole-screen OCR
    # on this rig mangles "Accept" (measured 'Recept'), so the gate reads
    # only the button strip. screen_text's crop is image space (import
    # crops 1280x800+0+30), so the +30 is already absorbed.
    screen_text "$1"
}
require_face_open() { face_open || { echo "$1 without face dialog" >&2; return 65; }; }
creation_page_open() {
    # The creation page is proven by its "Character Creation" heading (top
    # band, OCR-clean on the lane-5 rig). A SINGLE read is unreliable (the
    # stock classic client's small 11px type and screen_text's --psm 6 miss
    # it intermittently -- the B888-CC1 pilots, 2026-10-14), so callers poll
    # with creation_page_up, not this one-shot. The FacePick modal carries no
    # such heading while covering the page, and the modal's identity is the
    # Random/Accept/Cancel strip (face_open), so the two are distinguishable.
    screen_text | grep -qi 'Character Creation'
}
creation_page_up() {
    # Poll the heading up to $1 tries (default 4, 2 s apart).
    local tries=${1:-4} i
    for i in $(seq 1 "$tries"); do
        if creation_page_open; then return 0; fi
        sleep 2
    done
    return 1
}
creation_pair_open() {
    # $1 race, $2 gender (or "none"), $3 still label. Opens a fresh creation
    # slot and -- when a real gender is given -- selects the pair WITHOUT
    # submitting (no class, no points, no page 1, no create packet). That is
    # the state the stock UseLuclin=FALSE stills were taken in. It assumes
    # the caller is at character select (the close-creation verb is the only
    # way off the creation page), so it does not try to leave a screen it
    # already found: the page signal it relies on is the creation heading
    # itself, which the slot click produces. gender "none" opens the bare
    # page 0 (the race-table measurement) and makes no selection.
    local race=$1 gender=$2 label=$3 race_row gender_row
    race_row=$(race_y "$race" || true)
    gender_row=$([[ $gender == Male || $gender == Female ]] && gender_x "$gender" || true)
    # The slot button's row (create verb: 123,421) is character-select-only.
    jclick 123 421; sleep 3
    creation_page_up || { jclick 123 421; sleep 12; }
    creation_page_up || { shot "$label-create-slot-missing"; echo 'creation_pair_open: slot did not open' >&2; return 65; }
    sleep 12   # the restored selection must finish re-rendering (create verb)
    if [[ $gender == Male || $gender == Female ]]; then
        [[ -n $race_row ]] || return 65
        select_once "$race" 53 "$race_row" || return 65
        select_once "$gender" "$gender_row" 252 || return 65
        selected_at "$race" 53 "$race_row" || return 65
        selected_at "$gender" "$gender_row" 252 || return 65
        shot "$label-pair-selected" || :
    fi
    return 0
}
# B888-CC1 (#204): the machine probe behind the facepick-rows verb. The
# picker's rows read their SELECTED state from the same white-arrow glyph the
# row's text turns into (the stock client marks the chosen row), so a row is
# VISIBLE iff the 20px strip before its text (x 138-158, 12px tall at the
# row's y-6) is bright. The strip's Gray mean (0-255, %[fx:mean*255]) was
# calibrated against the 183-row matrix4 visibility table (2026-10-14): a
# VISIBLE row measured 73.92-75.02 and a HIDDEN one 20.58-21.68, a ~52-unit
# gap; threshold 48 (the midpoint) keeps ~26 units of margin on each side.
# The picker's y rows are constant per control (face 348, hair 368, haircolor
# 388, eyes 408, beard 428, beardcolor 448, wode 468), measured from the
# lane-5 modal; the pixel rows below are in the 1280x800 still. Import keeps
# a +0+30 virtual page origin, so clear it BEFORE a convert crop. Clearing
# it afterward samples 30 pixels above the arrow strip.
# $1 image, $2 race, $3 gender, $4 tsv path. Emits one line: race, gender,
# image, then label=0/1 for the seven pickers (face first, wode last).
facepick_probe_tsv() {
    local image=$1 race=$2 gender=$3 out=$4 yrow label mean vis
    {
        printf '%s\t%s\t%s\t' "$race" "$gender" "$image"
        for row in 'face:348' 'hair:368' 'haircolor:388' 'eyes:408' 'beard:428' 'beardcolor:448' 'wode:468'; do
            label=${row%%:*}; yrow=${row##*:}
            mean=$(convert "$image" +repage -crop "20x12+138+$((yrow-6-30))" +repage -colorspace Gray -format '%[fx:mean*255]' info: 2>/dev/null)
            [[ $mean =~ ^[0-9.]+$ ]] || { echo "facepick_probe_tsv: bad mean on $label" >&2; return 65; }
            vis=$(awk -v m="$mean" 'BEGIN { print (m > 48) ? 1 : 0 }')
            printf '%s=%s ' "$label" "$vis"
        done
        printf '\n'
    } >> "$out"
}
# B888-CC1 (#204): the settle discriminator behind facepick-rows. Counts how
# many of the seven picker rows read a visible arrow-strip value (>48, the
# probe's threshold). A modal that is OPEN but whose picker LIST has not yet
# rendered shows only the FACE row bright (~74) and the other six dark (~21),
# so it counts 1; a rendered list shows >= 2, because every stock (UseLuclin=
# FALSE) pair shows at least face+eyes (matrix3's 2/7 floor -- iksar/troll/
# ogre MALE show exactly face+eyes). The FACE row alone reads ~74 in BOTH
# states, so it cannot discriminate; the COUNT can. Validated 2026-10-14:
# matrix3-open (all rows) counts 7, a half-open full26f still counts 1.
facepick_row_count() {
    local row label yrow mean n=0
    for row in 'face:348' 'hair:368' 'haircolor:388' 'eyes:408' 'beard:428' 'beardcolor:448' 'wode:468'; do
        label=${row%%:*}; yrow=${row##*:}
        mean=$(convert "$1" +repage -crop "20x12+138+$((yrow-6-30))" +repage -colorspace Gray -format '%[fx:mean*255]' info: 2>/dev/null)
        [[ $mean =~ ^[0-9.]+$ ]] || continue
        if [[ $(awk -v m="$mean" 'BEGIN { print (m > 48) ? 1 : 0 }') == 1 ]]; then n=$((n+1)); fi
    done
    printf '%s\n' "$n"
}
assert_action_state() {
    local verb=$1 expected=$2 name=$3
    case $verb in
        random)
            face_open || { shot "$name-face-missing"; echo 'random missed face dialog' >&2; return 65; } ;;
        open-face)
            # B888-CC1 (#204): on the lane-5 rig the modal (root (0,404)
            # 175x215, measured) covers the creation screen's page text, and
            # its button strip is what IS legible (full-frame OCR reads
            # 'Randam Accent Cance!'), so the modal's button strip IS the
            # postcondition -- the same gate open-face's own click check
            # uses. A text assertion here can never see what the modal
            # covers, so it must not gate the step.
            face_open || { shot "$name-face-missing"; echo 'open-face: modal not open at assertion' >&2; return 65; }
            return 0 ;;
        accept|cancel)
            if face_open; then
                shot "$name-face-still-open"
                echo "$verb did not close face dialog" >&2
                return 65
            fi ;;
    esac
    assert_visible "$expected" "$name"
}
if [[ $ACTION == selftest ]]; then
    [[ $(race_y Barbarian) == 291 && $(class_y Warrior) == 627 &&
       $(gender_x Male) == 105 && $(city_y Barbarian) == 320 ]] || exit 1
    for verb in open random accept cancel; do
        expected_for "$verb" character-create >/dev/null || exit 1
    done
    [[ $(expected_for create Testname) == window:EverQuest ]] || exit 1
    # B888-CC3 (#204): the Explore-mode verbs' postconditions.
    [[ $(expected_for explore-open 0) == 'select that character' ]] || exit 1
    [[ $(expected_for hold-d 0) == 'select that character' ]] || exit 1
    [[ $(expected_for orbit 0) == 'select that character' ]] || exit 1
    [[ $(expected_for wheel 0) == 'select that character' ]] || exit 1
    [[ $(expected_for explore-esc 0) == 'Create a New Character' ]] || exit 1
    [[ $(expected_for enter-press 0) == window:EverQuest ]] || exit 1
    DIR=$2
    shot() { touch "$DIR/$1.png"; }
    # selected_at must run with NO caller-scoped 'label' (fix2f crashed
    # "line 44: label: unbound variable" once a consolidated check called it
    # directly): stub only the imaging, use the real function.
    import() { local f; f=$(echo "$@" | awk '{print $NF}'); : >"$f"; }
    convert() { [[ $STUB_MEAN ]]; echo "${STUB_MEAN:-0.5}"; }
    race=Barbarian class=Warrior gender=Male race_row=291 class_row=627
    STUB_MEAN=0.9
    selected_at "$race" 53 "$race_row" || { echo "selected_at crashed without caller label"; exit 1; }
    selected_at "$class" 153 "$class_row" || { echo "selected_at crashed without caller label"; exit 1; }
    selected_at "$gender" 105 252 || { echo "selected_at crashed without caller label"; exit 1; }
    # The detector must REJECT a white-text (unselected) row: a selected row
    # measures ~0.90-0.95 and an unselected one 1.0000 (measured on real
    # stills); the old negate-mean read 0.9919 vs 1.0000 and let fix4/fix2e/
    # fix2g through.
    STUB_MEAN=1.0
    if selected_at "$race" 53 "$race_row"; then echo "unselected row passed selected_at"; exit 1; fi
    # B888-CC1 (#204): the open-creation / open-face verbs must resolve their
    # expected postconditions (the creation screen stays up; open-face then
    # shows the modal's button strip). A verb with no expected_for arm falls
    # to the default 'window:EverQuest' and would assert a live zone.
    [[ $(expected_for open-creation Barbarian) == 'Character Creation' ]] || exit 1
    [[ $(expected_for open-face x) == 'Character Creation' ]] || exit 1
    # B888-CC1 (#204): the capture-leg verbs' per-step postconditions.
    # facepick-rows asserts its OWN modal's button strip (the reliable
    # cropped read; the strong checks are in-driver). close-creation asserts
    # the live-client window (its proof is the creation heading having
    # DISAPPEARED, in-driver, not any full-screen text). A misfiled verb
    # would still assert the zone default 'window:EverQuest' or the weak
    # 'Character' substring.
    [[ $(expected_for race-table x) == 'Character' ]] || exit 1
    [[ $(expected_for facepick-rows x) == 'Random|Accept|Cancel' ]] || exit 1
    [[ $(expected_for close-creation x) == window:EverQuest ]] || exit 1
    # The calibrated arrow probe must classify the two measured extremes:
    # the brightest hidden row (21.68) hidden, the dimmest visible (73.92)
    # visible. The convert stub yields STUB_MEAN as the strip's Gray mean.
    : >"$DIR/rows.tsv"
    STUB_MEAN=21.68
    facepick_probe_tsv fake.png Barbarian Male "$DIR/rows.tsv" || exit 1
    STUB_MEAN=73.92
    facepick_probe_tsv fake.png Elf Female "$DIR/rows.tsv" || exit 1
    grep -q $'Barbarian\tMale\tfake.png\tface=0 hair=0 haircolor=0 eyes=0 beard=0 beardcolor=0 wode=0 ' "$DIR/rows.tsv" || exit 1
    grep -q $'Elf\tFemale\tfake.png\tface=1 hair=1 haircolor=1 eyes=1 beard=1 beardcolor=1 wode=1 ' "$DIR/rows.tsv" || exit 1
    # name_filled: the convert stub returns STUB_MEAN as the std. A uniform
    # (empty) field (std 0) must be rejected and a glyph-like field (std
    # 0.172) accepted -- the fix2h mode of failure is a lost xdotool
    # keystroke leaving an empty field that submit answers "Name is not
    # valid".
    STUB_MEAN=0.0
    if name_filled; then echo "empty name field passed name_filled"; exit 1; fi
    STUB_MEAN=0.172
    name_filled || { echo "typed name field failed name_filled"; exit 1; }
    STUB_MEAN=
    SCREEN_TEXT='Character Creation'
    screen_text() { echo "$SCREEN_TEXT"; }
    # B888-CC1 (#204): the face_open gate (open-face's success check) must
    # read the modal's OWN button strip, not the whole screen: a closed
    # creation screen has no Random/Accept/Cancel and must fail it.
    if face_open; then echo 'closed creation screen passed face_open'; exit 1; fi
    SCREEN_TEXT='Random Accept Cancel'
    face_open || { echo 'open modal failed face_open (button-strip gate)'; exit 1; }
    SCREEN_TEXT='Character Creation'   # restore: the assert_visible below keys off it
    assert_visible 'Character Creation' positive || exit 1
    if assert_visible 'Missing Window' negative; then exit 1; fi
    [[ -f $DIR/negative-assertion-failed.png ]] || exit 1
    if assert_action_state random "$(expected_for random face)" missed-random; then exit 1; fi
    for verb in accept cancel; do if require_face_open "$verb"; then exit 1; fi; done
    SCREEN_TEXT='Character Creation Random Accept Cancel'
    assert_action_state random "$(expected_for random face)" random-open || exit 1
    for verb in accept cancel; do
        if assert_action_state "$verb" "$(expected_for "$verb" face)" "missed-$verb-open"; then exit 1; fi
    done
    SCREEN_TEXT='Character Creation'
    for verb in accept cancel; do
        assert_action_state "$verb" "$(expected_for "$verb" face)" "$verb-closed" || exit 1
        if assert_action_state "$verb" "$(expected_for "$verb" face 'Missing Window')" "override-$verb"; then exit 1; fi
    done
    SCREEN_TEXT='Character Creation Random Accept Cancel'
    if assert_action_state random "$(expected_for random face 'Missing Window')" override-random; then exit 1; fi
    # select_once: a missed click gets ONE settled retry (8 s before the
    # second click -- the old select_at re-clicked within 3 s mid-re-render
    # and moved the shared cursor: fix4 created class=16, fix2e race=7, both
    # rejected by validate_create) and then FAILS at 2 clicks; a verified
    # selection issues no further click.
    sleep() { :; }
    CLICKS=0
    jclick() { CLICKS=$((CLICKS+1)); }
    shot() { :; }
    selected_at() { [[ $SELECTION_OK == 1 ]]; }
    SELECTION_OK=0
    if select_once Barbarian 53 291; then exit 1; fi
    [[ $CLICKS == 2 ]] || { echo "missed click used $CLICKS clicks, expected 2 (one settled retry)"; exit 1; }
    SELECTION_OK=1
    select_once Barbarian 53 291 || exit 1
    [[ $CLICKS == 3 ]] || { echo "verified selection clicked again ($CLICKS clicks), expected 3"; exit 1; }
    # B1058: template clicks. The broker answer is FRAME pixels; the click
    # lands at root (x, y+30) plus the plan offset; a below-threshold answer
    # clicks nothing; a garbled or mis-sequenced answer is an error (65).
    unset -f shot selected_at
    shot() { :; }
    import() { local f; f=$(echo "$@" | awk '{print $NF}'); : >"$f"; }
    mkdir -p "$DIR/locate"
    CLICKED=
    jclick() { CLICKED="$1,$2"; }
    LOCATE_WAIT_S=1
    printf '0 54 262 0.9661 cv2 1\n' >"$DIR/locate/ans-0.tsv"
    click_template race_Barbarian 0 st || { echo 'click_template refused a found answer'; exit 1; }
    [[ $CLICKED == 54,292 ]] || { echo "click_template clicked $CLICKED, expected root 54,292"; exit 1; }
    printf '1 171 47 0.9999 numpy 1\n' >"$DIR/locate/ans-1.tsv"
    click_template name_getname -103,0 st || exit 1
    [[ $CLICKED == 68,77 ]] || { echo "offset click landed $CLICKED, expected the Name field 68,77"; exit 1; }
    CLICKED=
    printf '2 54 262 0.6100 cv2 0\n' >"$DIR/locate/ans-2.tsv"
    if click_template race_Barbarian 0 st; then echo 'below-threshold answer was clicked'; exit 1; fi
    [[ -z $CLICKED ]] || { echo 'below-threshold answer moved the pointer'; exit 1; }
    printf '9 54 262 0.9661 cv2 1\n' >"$DIR/locate/ans-3.tsv"
    locate_template race_Barbarian; [[ $? == 65 ]] || { echo 'mis-sequenced answer accepted'; exit 1; }
    locate_template race_Barbarian; [[ $? == 65 ]] || { echo 'missing answer did not time out with 65'; exit 1; }
    [[ $(wc -l <"$DIR/template-clicks.tsv") == 3 ]] || { echo 'template-clicks.tsv rows wrong'; exit 1; }
    # select_template: a first-time yellow verification records 'first' and
    # no settle retry; a miss records the retry and counts it.
    selected_at() { [[ $SELECTION_OK == 1 ]]; }
    SELECTION_OK=1 SETTLE_RETRIES=0
    printf '5 54 262 0.9661 cv2 1\n' >"$DIR/locate/ans-5.tsv"
    select_template race_Barbarian Barbarian st || exit 1
    [[ $(tail -1 "$DIR/template-clicks.tsv" | cut -f7,8) == $'first\t0' && $SETTLE_RETRIES == 0 ]] || { echo 'first-time selection not logged'; exit 1; }
    # broker_exchange: stores a valid answer, refuses a malformed one, and
    # streams a pending request header + PNG.
    BROKER_WAIT_S=1
    broker_exchange 7 '6 1 2 0.5000 cv2 0' >/dev/null || exit 1
    [[ $(cat "$DIR/locate/ans-6.tsv") == '6 1 2 0.5000 cv2 0' ]] || { echo 'broker answer not stored'; exit 1; }
    if broker_exchange 7 '6 1 2 0.5 cv2; rm -rf ~' >/dev/null; then echo 'malformed broker answer accepted'; exit 1; fi
    broker_exchange 7 '8 TEXT atlas 1 1.0000 cv2 4f6b' >/dev/null || { echo 'text answer refused'; exit 1; }
    if broker_exchange 7 '8 TEXT atlas 1 1.0000 cv2 4f6b; id' >/dev/null; then echo 'malformed text answer accepted'; exit 1; fi
    printf 'race_Human\n' >"$DIR/locate/req-7.tsv"; printf 'PNGBYTES' >"$DIR/locate/shot-7.png"
    [[ $(broker_exchange 7 '') == $'REQ\t7\trace_Human\nPNGBYTES' ]] || { echo 'broker request not streamed'; exit 1; }
    # B1059: text reads. The atlas decides the Name check both ways; an
    # unknown or missing answer falls back to the presence check untouched;
    # a screen check passes on an atlas match without running screen_text.
    hexof() { printf '%s' "$1" | od -An -tx1 | tr -d ' \n'; }
    name_filled_at() { echo called >"$DIR/presence"; return 0; }
    printf 'step\twhat\tmode\tdecider\tok\ttext\tscore\tpath\n' >"$DIR/text-reads.tsv"
    LOCATE_SEQ=20
    printf '20 TEXT atlas 1 1.0000 cv2 %s\n' "$(hexof 'Orac[Il]ebartp[Il]')" >"$DIR/locate/ans-20.tsv"
    name_verify 171 77 Oraclebartpl st || { echo 'atlas match refused'; exit 1; }
    [[ ! -e $DIR/presence ]] || { echo 'atlas-decided name ran the presence check'; exit 1; }
    [[ $(cat "$DIR/locate/req-20.tsv") == "read:edit:6,39,118,16:$(hexof Oraclebartpl)" ]] || { echo 'name read request wrong'; exit 1; }
    [[ $(tail -1 "$DIR/text-reads.tsv" | cut -f2,4,5,6) == $'name-field\tatlas\t1\tOrac[Il]ebartp[Il]' ]] || { echo 'text-reads row wrong'; exit 1; }
    printf '21 TEXT atlas 0 1.0000 cv2 %s\n' "$(hexof Oraclebart)" >"$DIR/locate/ans-21.tsv"
    if name_verify 171 77 Oraclebartpl st; then echo 'atlas mismatch passed'; exit 1; fi
    printf '22 TEXT unknown 0 0.0000 - \n' >"$DIR/locate/ans-22.tsv"
    name_verify 171 77 Oraclebartpl st && [[ -e $DIR/presence ]] || { echo 'unknown read skipped the presence check'; exit 1; }
    rm -f "$DIR/presence"
    name_verify 171 77 Oraclebartpl st && [[ -e $DIR/presence ]] || { echo 'missing answer skipped the presence check'; exit 1; }
    TEXT_BROKER=1 SCREEN_TEXT='nothing here'
    printf '24 TEXT atlas 1 1.0000 cv2 %s\n' "$(hexof 'Create a New Character')" >"$DIR/locate/ans-24.tsv"
    assert_visible 'Character Select|Create a New Character' atlas-screen || { echo 'atlas screen match refused'; exit 1; }
    printf '25 TEXT unknown 0 0.0000 - \n' >"$DIR/locate/ans-25.tsv"
    if assert_visible 'Character Select|Create a New Character' atlas-screen-miss; then echo 'unknown screen read passed without screen_text'; exit 1; fi
    SCREEN_TEXT='Character Select'
    printf '26 TEXT unknown 0 0.0000 - \n' >"$DIR/locate/ans-26.tsv"
    assert_visible 'Character Select|Create a New Character' tess-screen || { echo 'tesseract fallback refused'; exit 1; }
    [[ $(tail -1 "$DIR/text-reads.tsv" | cut -f4,5) == $'tesseract\t1' ]] || { echo 'tesseract decider not recorded'; exit 1; }
    unset TEXT_BROKER
    echo 'PASS: selected coordinates, random/accept/cancel missed-action and override assertions, template clicks, broker exchange and text reads'
    exit 0
fi
stop_capture() {
    local p
    for kind in video video-caster pcap; do
        p=$(cat "$DIR/$kind.pid" 2>/dev/null || :)
        if [[ $p =~ ^[0-9]+$ ]]; then kill -INT "$p" 2>/dev/null || :; fi
        rm -f "$DIR/$kind.pid"
    done
    sleep 2
    cp "$CLIENT/eqclient.ini" "$DIR/eqclient-after.ini" 2>/dev/null || :
    cp "$CLIENT"/Testcaster_*.ini "$DIR/" 2>/dev/null || :
    cp "$CLIENT"/Testchar_*.ini "$DIR/" 2>/dev/null || :
    obs_name=$(cat "$DIR/observer.name" 2>/dev/null || :)
    if [[ $obs_name =~ ^[A-Za-z]+$ ]]; then
        mkdir -p "$DIR/observer-ini"
        cp "$OBS_CLIENT/eqclient.ini" "$DIR/observer-ini/eqclient-after.ini" 2>/dev/null || :
        cp "$OBS_CLIENT/${obs_name}_"*.ini "$DIR/observer-ini/" 2>/dev/null || :
    fi
    # VM-side pkill, not an eqemu-container pkill. This oracle VM is single-tenant
    # under the lane lock; leave no Wine client alive.
    pkill -TERM -x eqgame.exe 2>/dev/null || :
    sleep 2
    if [[ -f $DIR/eqclient-global-original.ini ]]; then
        cp "$DIR/eqclient-global-original.ini" "$CLIENT/eqclient.ini" || return 65
        cmp -s "$DIR/eqclient-global-original.ini" "$CLIENT/eqclient.ini" || return 65
        touch "$DIR/eqclient-global-restored"
    fi
    # B1002: the ini-set backup was taken AFTER the luclin backup, so its
    # restore must run last or it would clobber the (already restored) INI.
    if [[ -f $DIR/eqclient-iniset-original.ini && ! -e $DIR/eqclient-iniset-restored ]]; then
        cp "$DIR/eqclient-iniset-original.ini" "$CLIENT/eqclient.ini" || return 65
        cmp -s "$DIR/eqclient-iniset-original.ini" "$CLIENT/eqclient.ini" || return 65
        touch "$DIR/eqclient-iniset-restored"
    fi
    if [[ -s $DIR/character-file.txt && -f $DIR/character-original.ini && ! -e $DIR/character-restored ]]; then
        cp "$(cat "$DIR/character-file.txt")" "$DIR/character-after.ini" || :
        cp "$DIR/character-original.ini" "$(cat "$DIR/character-file.txt")" || return 65
        cmp -s "$DIR/character-original.ini" "$(cat "$DIR/character-file.txt")" || return 65
        touch "$DIR/character-restored"
    fi
    # B1178: the UI-INI edits (`# ui-ini-set=`) come back after the character
    # restore; they are a different file (UI_<char>_<server>.ini).
    if [[ -s $DIR/ui-ini/character-file.txt && -f $DIR/ui-ini/character-original.ini && ! -e $DIR/ui-ini-restored ]]; then
        cp "$DIR/ui-ini/character-original.ini" "$DIR/ui-ini/character-after.ini" || :
        cp "$DIR/ui-ini/character-original.ini" "$(cat "$DIR/ui-ini/character-file.txt")" || return 65
        cmp -s "$DIR/ui-ini/character-original.ini" "$(cat "$DIR/ui-ini/character-file.txt")" || return 65
        touch "$DIR/ui-ini-restored"
    fi
    # B1134: the observer's eqclient.ini edits (`# obs-ini-set=`) come back last.
    if [[ -f $DIR/observer-eqclient-original.ini && ! -e $DIR/observer-eqclient-restored ]]; then
        cp "$DIR/observer-eqclient-original.ini" "$OBS_CLIENT/eqclient.ini" || return 65
        cmp -s "$DIR/observer-eqclient-original.ini" "$OBS_CLIENT/eqclient.ini" || return 65
        touch "$DIR/observer-eqclient-restored"
    fi
    rm -f "$DIR/drive-login.sh" "$DIR/drive-login-observer.sh"
    # Kill only the Xvfb this session started (xvfb.pid). Lane 5's :99 is a
    # real Xorg (oracle-xorg.service) that run found up, so no pid is saved;
    # a stale or reused pid that is not an Xvfb is left alone.
    p=$(cat "$DIR/xvfb.pid" 2>/dev/null || :)
    if [[ $p =~ ^[0-9]+$ && $(ps -o comm= -p "$p" 2>/dev/null) == Xvfb ]]; then
        kill -TERM "$p" 2>/dev/null || :
    fi
    # B1134: the caster's Xvfb of an observer session, same rule (own pid only).
    p=$(cat "$DIR/xvfb-caster.pid" 2>/dev/null || :)
    if [[ $p =~ ^[0-9]+$ && $(ps -o comm= -p "$p" 2>/dev/null) == Xvfb ]]; then
        kill -TERM "$p" 2>/dev/null || :
    fi
}
if [[ $ACTION == stop ]]; then stop_capture; exit $?; fi
if [[ $ACTION == broker ]]; then broker_exchange "${3:-}" "${4:-}"; exit $?; fi
[[ $ACTION == run ]] || exit 65
mkdir -p "$DIR"
if ! xdpyinfo -display :99 >/dev/null 2>&1; then
    Xvfb :99 -screen 0 1280x1024x24 +extension GLX +render -noreset >"$DIR/xvfb.log" 2>&1 &
    echo $! >"$DIR/xvfb.pid"
    sleep 3
    xdpyinfo -display :99 >/dev/null 2>&1 || exit 65
fi
cp "$CLIENT/eqclient.ini" "$DIR/eqclient-before.ini"
LUCLIN_MODELS=$(sed -n 's/^# use-luclin-models=//p' "$DIR/plan.tsv" | head -1)
[[ -z $LUCLIN_MODELS || $LUCLIN_MODELS == true ]] || exit 65
if [[ $LUCLIN_MODELS == true ]]; then
    cp "$CLIENT/eqclient.ini" "$DIR/eqclient-global-original.ini" || exit 65
    python3 /home/user/eq-oracle/uigap-scripts/set_facepick_luclin.py || exit 65
fi
CHARACTER=$(awk -F'\t' '$1=="login" {print $2; exit}' "$DIR/plan.tsv")
[[ $CHARACTER =~ ^[A-Za-z]+$ ]] || exit 65
CASTER_CHARACTER=$CHARACTER
# B1134 (#454): `# observer=<Character>` -- the second official client (top of
# this file). Its client copy, prefix and credentials file must exist
# (oracle_observer_setup.sh); the caster gets its own Xvfb, recorded in
# xvfb-caster.pid so stop kills only that one.
OBSERVER=$(sed -n 's/^# observer=//p' "$DIR/plan.tsv" | head -1)
if [[ -n $OBSERVER ]]; then
    [[ $OBSERVER =~ ^[A-Za-z]+$ && $OBSERVER != "$CHARACTER" ]] || exit 65
    [[ -d $OBS_CLIENT && -d $OBS_PREFIX && -f /home/user/eq-oracle/observer-$OBSERVER.cred ]] || { echo "observer $OBSERVER not set up (oracle_observer_setup.sh)" >&2; exit 65; }
    [[ $CASTER_DISPLAY =~ ^:[0-9]+$ && $CASTER_DISPLAY != "$OBS_DISPLAY" ]] || exit 65
    printf '%s\n' "$OBSERVER" >"$DIR/observer.name"
    if ! xdpyinfo -display "$CASTER_DISPLAY" >/dev/null 2>&1; then
        Xvfb "$CASTER_DISPLAY" -screen 0 1280x1024x24 +extension GLX +render -noreset >"$DIR/xvfb-caster.log" 2>&1 &
        echo $! >"$DIR/xvfb-caster.pid"
        sleep 3
        xdpyinfo -display "$CASTER_DISPLAY" >/dev/null 2>&1 || exit 65
    else
        echo "caster display $CASTER_DISPLAY already up: refusing to share it" >&2; exit 65
    fi
    # `# obs-ini-set=Section.Key=VALUE` (several allowed) edits the OBSERVER's
    # eqclient.ini (its camera), backed up first and restored at stop.
    OBS_INI_SETS=$(sed -n 's/^# obs-ini-set=//p' "$DIR/plan.tsv")
    cp "$OBS_CLIENT/eqclient.ini" "$DIR/observer-eqclient-before.ini" || exit 65
    if [[ -n $OBS_INI_SETS ]]; then
        cp "$OBS_CLIENT/eqclient.ini" "$DIR/observer-eqclient-original.ini" || exit 65
        while IFS= read -r one; do
            [[ $one =~ ^[A-Za-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9-]*=[A-Za-z0-9.-]+$ ]] || exit 65
            mkdir -p "$DIR/observer-ini"
            python3 /home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py \
                "$OBS_CLIENT" "$DIR/observer-ini" "" "$one" || exit 65
        done <<<"$OBS_INI_SETS"
    fi
fi
INI_UNSET=$(sed -n 's/^# ini-unset=//p' "$DIR/plan.tsv" | head -1)
# B1002: `# ini-set=Section.Key=VALUE` edits the GLOBAL eqclient.ini (the
# character- INI is the ini-unset domain; eqclient.ini's [Defaults] holds
# HideChatWin and friends). Backed up and restored like the global luclin
# edit.
# B1134: several `# ini-set=` rows (B1130/B1137 rig copies; keys may carry
# a '-', e.g. Options.Camera2-Distance), one backup before the first edit.
INI_SETS=$(sed -n 's/^# ini-set=//p' "$DIR/plan.tsv")
if [[ -n $INI_SETS ]]; then
    cp "$CLIENT/eqclient.ini" "$DIR/eqclient-iniset-original.ini" || exit 65
    while IFS= read -r INI_SET; do
        [[ $INI_SET =~ ^[A-Za-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9-]*=[A-Za-z0-9.-]+$ ]] || exit 65
        python3 /home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py \
            "$CLIENT" "$DIR" "" "$INI_SET" || exit 65
    done <<<"$INI_SETS"
fi
# B1058: `# create-clicks=template` makes a create row locate its race, class,
# gender and Name-field controls from templates instead of the table above.
CREATE_CLICKS=$(sed -n 's/^# create-clicks=//p' "$DIR/plan.tsv" | head -1)
[[ -z $CREATE_CLICKS || $CREATE_CLICKS == template ]] || exit 65
if [[ -n $CREATE_CLICKS ]] || grep -qE '^(click|wait)-template'$'\t' "$DIR/plan.tsv"; then
    printf 'step\ttemplate\troot_x\troot_y\tscore\tpath\tverify\tsettle_retries\n' >"$DIR/template-clicks.tsv"
    # B1059: the broker is up, so text checks try the glyph atlas first
    printf 'step\twhat\tmode\tdecider\tok\ttext\tscore\tpath\n' >"$DIR/text-reads.tsv"
    TEXT_BROKER=1
fi
if [[ -n $INI_UNSET ]]; then
    [[ $INI_UNSET =~ ^[A-Za-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9]*$ ]] || exit 65
    python3 /home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py \
        "$CLIENT" "$DIR" "$CHARACTER" "$INI_UNSET" || exit 65
fi
# B1162: `# char-ini-set=Section.Key=VALUE` sets ONE key in the login
# character's INI (e.g. Abilities.Ability1=71: the Actions-window ability slot
# that /doability 1 reads, eqgame.exe.c 133398-133402). Same backup/restore as
# ini-unset (character-original.ini, restored byte-identically at stop), so the
# two are mutually exclusive and only one row is allowed.
CHAR_INI_SET=$(sed -n 's/^# char-ini-set=//p' "$DIR/plan.tsv")
if [[ -n $CHAR_INI_SET ]]; then
    [[ -z $INI_UNSET && $(wc -l <<<"$CHAR_INI_SET") -eq 1 ]] || exit 65
    [[ $CHAR_INI_SET =~ ^[A-Za-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9]*=-?[0-9A-Za-z.]+$ ]] || exit 65
    python3 /home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py \
        "$CLIENT" "$DIR" "$CHARACTER" "$CHAR_INI_SET" || exit 65
fi
# B1178: `# ui-ini-set=Section.Key=VALUE` (several allowed) edits the login
# character's UI INI (UI_<char>_<server>.ini: window positions and Show= keys;
# eqclient.ini is the ini-set= domain, the character INI the char-ini-set=
# domain). One backup before the first edit; restored byte-identically at
# stop (ui-ini/character-original.ini, its own bookkeeping sub-dir so it can
# coexist with char-ini-set in one plan).
UI_INI_SETS=$(sed -n 's/^# ui-ini-set=//p' "$DIR/plan.tsv")
if [[ -n $UI_INI_SETS ]]; then
    [[ $(ls "$CLIENT"/UI_"${CHARACTER}"_*.ini 2>/dev/null | wc -l) -eq 1 ]] || exit 65
    cp "$CLIENT"/UI_"${CHARACTER}"_*.ini "$DIR/ui-iniset-original.ini" || exit 65
    mkdir -p "$DIR/ui-ini"
    while IFS= read -r UI_SET; do
        [[ $UI_SET =~ ^[A-Za-z][A-Za-z0-9 ]*\.[A-Za-z][A-Za-z0-9-]*=[A-Za-z0-9.-]+$ ]] || exit 65
        python3 /home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py \
            "$CLIENT" "$DIR/ui-ini" "UI_${CHARACTER}" "$UI_SET" || exit 65
    done <<<"$UI_INI_SETS"
fi
cp "$CLIENT/${CHARACTER}_"*.ini "$DIR/before-character.ini" 2>/dev/null || :
# Capture starts before even launching Wine. Save PIDs for interrupted cleanup.
# B1057: on lane 5 the eqemu containers run on this box, so `-i any` records
# every packet twice, on the docker bridge and on the container's veth
# (attempt 2: each opcode doubled, "expected one OP_CharacterCreate, got 2").
# Drop the veth copies; an oracle VM with no veth keeps the plain filter.
filter='udp and (port 5998 or port 5999 or portrange 9000-9001 or portrange 7000-7030)'
veths=$(ip -o link show type veth 2>/dev/null | awk -F': ' '{printf "%sifindex %s", (n++ ? " or " : ""), $1}')
[[ -z $veths ]] || filter="$filter and not ($veths)"
printf '%s\n' "$filter" >"$DIR/pcap.filter"
sudo -n tcpdump -i any -s 0 -U -Z user -w "$DIR/capture.pcap" "$filter" >"$DIR/pcap.log" 2>&1 &
echo $! >"$DIR/pcap.pid"
date -u +%s.%N >"$DIR/video.mp4.t0"
# B1134: `# fps=N` (B1127/B1130 rig copies: 30 fps passthrough, crf 18 for FX
# legs; x11grab >= 30 fps is the FX-leg bar). The default stays 10 fps, crf 23.
FPS=$(sed -n 's/^# fps=//p' "$DIR/plan.tsv" | head -1)
if [[ -n $FPS ]]; then
    [[ $FPS =~ ^[1-6]?[0-9]$ ]] || exit 65
    ffmpeg -nostdin -loglevel error -f x11grab -framerate "$FPS" -video_size 1280x800 -i :99+0,30 -fps_mode passthrough -c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p "$DIR/video.mp4" >"$DIR/video.log" 2>&1 &
else
    ffmpeg -nostdin -loglevel error -f x11grab -framerate 10 -video_size 1280x800 -i :99+0,30 -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p "$DIR/video.mp4" >"$DIR/video.log" 2>&1 &
fi
echo $! >"$DIR/video.pid"
sleep 3
kill -0 "$(cat "$DIR/pcap.pid")" 2>/dev/null || exit 65
kill -0 "$(cat "$DIR/video.pid")" 2>/dev/null || exit 65
# B1134: `# caster-video=1` (observer sessions) also grabs the CASTER's display
# (its own camera: the self view, for the observer's other-entity view to be
# compared with). llvmpipe renders it, so its frame rate is the client's, not
# the grab's (video-caster.mp4.t0 = its start epoch; passthrough pts).
if [[ -n $OBSERVER ]] && grep -qx '# caster-video=1' "$DIR/plan.tsv"; then
    date -u +%s.%N >"$DIR/video-caster.mp4.t0"
    ffmpeg -nostdin -loglevel error -f x11grab -framerate 30 -video_size 1280x800 -i "$CASTER_DISPLAY+0,30" -fps_mode passthrough -c:v libx264 -preset ultrafast -crf 18 -pix_fmt yuv420p "$DIR/video-caster.mp4" >"$DIR/video-caster.log" 2>&1 &
    echo $! >"$DIR/video-caster.pid"
fi
# A visible onset lets the report reject a wall-clock/video mapping that is
# more than one frame off. The marker is removed before Wine launches.
# B1057: lane 5's :99 is a long-lived Xorg whose screen saver is usually
# active when a session starts (Xvfb on lanes 1-4 is fresh); reset it so the
# capture starts on a live screen. The marker wait is bounded: lane-5 attempt
# 1 drew the marker on screen but xdotool never found its window and the
# unbounded --sync hung the session. One relaunch, then fail cleanly.
xset s reset 2>/dev/null || :
for marker_try in 1 2; do
    xmessage -title ORACLE_TIMING_MARKER -geometry 420x80+400+350 'ORACLE TIMING MARKER' >>"$DIR/marker.log" 2>&1 &
    marker_pid=$!
    timeout 20 xdotool search --sync --name '^ORACLE_TIMING_MARKER$' >/dev/null && break
    echo "timing marker not found (try $marker_try)" >>"$DIR/marker.log"
    kill "$marker_pid" 2>/dev/null || :
    wait "$marker_pid" 2>/dev/null || :
    [[ $marker_try == 2 ]] && exit 65
done
printf '%s\n' "$(stamp)" >"$DIR/video.marker.epoch"
shot timeline_begin || exit 65
sleep 3
kill "$marker_pid" 2>/dev/null || :
wait "$marker_pid" 2>/dev/null || :
[[ -z $OBSERVER ]] || use_role caster
cd "$CLIENT" || exit 65
nohup wine eqgame.exe patchme >"$DIR/wine.log" 2>&1 </dev/null &
echo $! >"$DIR/wine.pid"
step=0
while IFS=$'\t' read -r verb target arg3 arg4 arg5 arg6 arg7; do
    [[ -z $verb || $verb == \#* ]] && continue
    step=$((step+1))
    # B1134: an `obs` row is the same row for the observer's client.
    if [[ $verb == obs ]]; then
        [[ -n $OBSERVER ]] || exit 65
        verb=$target target=$arg3 arg3=$arg4 arg4=$arg5 arg5=$arg6 arg6=$arg7
        use_role observer
    elif [[ -n $OBSERVER ]]; then
        use_role caster
    fi
    hold_ms=$arg3 still_name=$arg4
    if [[ $verb == create ]]; then
        race=$arg3 class=$arg4 gender=$arg5 still_name=$arg6
    elif [[ $verb == facepick-rows ]]; then
        # The still label is the 6th field (like create), not the universal
        # still_name slot (arg4), which is the gender-less '0' placeholder.
        still_name=$arg6
    fi
    name=$(printf '%02d_%s' "$step" "${still_name:-$verb}")
    [[ $name =~ ^[A-Za-z0-9_-]+$ ]] || exit 65
    mark BEGIN "$name"
    case $verb in
        login)
            if [[ $ROLE == observer ]]; then
                # B1134: the observer's own Wine (prefix + client copy) starts
                # here; its login helper is the VM helper with this display
                # and the observer's credentials file sourced, never copied.
                [[ $target == "$OBSERVER" ]] || exit 65
                if [[ ! -e $DIR/observer-wine.pid ]]; then
                    ( cd "$CLIENT" && nohup wine eqgame.exe patchme >"$DIR/observer-wine.log" 2>&1 </dev/null & echo $! >"$DIR/observer-wine.pid" )
                fi
                python3 /home/user/eq-oracle/uigap-scripts/oracle_login_patch.py \
                    /home/user/eq-oracle/drive-login.sh "$DIR/drive-login-observer.sh" \
                    --display "$DISPLAY" --credentials "/home/user/eq-oracle/observer-$OBSERVER.cred" || exit 65
                bash "$DIR/drive-login-observer.sh" >"$DIR/login-observer.log" 2>&1 || exit 65
            else
                python3 /home/user/eq-oracle/uigap-scripts/oracle_login_patch.py \
                    /home/user/eq-oracle/drive-login.sh "$DIR/drive-login.sh" \
                    ${OBSERVER:+--display "$DISPLAY"} || exit 65
                bash "$DIR/drive-login.sh" >"$DIR/login.log" 2>&1 || exit 65
            fi ;;
        zone)
            # Seeded by the host before login; enter the selected character here.
            entered=0
            for attempt in 1 2 3 4; do
                jclick 123 315; sleep 3; jclick 180 640; sleep 25
                if ! screen_text | grep -qiE 'Character Select|Create a New Character'; then
                    entered=1
                    break
                fi
            done
            [[ $entered == 1 ]] || { shot "$name-zone-failed"; echo "zone entry did not leave character select" >&2; exit 65; } ;;
        open)
            case $target in
                hotbar) xdotool key Alt+h ;;
                inventory) xdotool key i ;;
                character-create) jclick 123 422 ;;
                *) exit 65 ;;
            esac ;;
        click)
            [[ $target =~ ^[0-9]+,[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"; jclick "$x" "$y" ;;
        # B1076 (#396): a motionless right click -- the driver had no
        # right-click verb at all, and the spellbook's memorise gesture is
        # only measurable once it can be issued on its own. Motionless is
        # deliberate here: it is a PRESS, not a drag (a drag is `hold`), so
        # DirectInput's 1px-while-held dance is not what the oracle does.
        rclick)
            [[ $target =~ ^[0-9]+,[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"; jrclick "$x" "$y" ;;
        # B1076 (#396): park the cursor. A spell dragged onto a page only
        # counts if it first followed the cursor, so the scribe drag needs
        # a motion-to-without-pressing in front of the `hold` that drags.
        mmove)
            [[ $target =~ ^[0-9]+,[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"; client_focus; xdotool mousemove "$x" "$y" ;;
        # B1076 (#396): left button down at (x,y) and HELD for hold_ms,
        # without the 1px move -- `hold` carries the DirectInput 1px drag,
        # which an item pick-up would treat as a one-pixel move of the
        # held item, not as a press-and-release in the same cell.
        lhold)
            [[ $target =~ ^[0-9]+,[0-9]+$ && $hold_ms =~ ^[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"
            client_focus
            xdotool mousemove "$x" "$y"; xdotool mousedown 1
            sleep "$(awk -v ms="$hold_ms" 'BEGIN {printf "%.3f",ms/1000}')"
            xdotool mouseup 1 ;;
        # B1076 (#396): button-1 drag, xdotool syntax (x1,y1 x2,y2 ...).
        # The 1px-while-held dance is done once at the start so DirectInput
        # registers the press; the glide is a real multi-step drag, which is
        # what moves a held item. `ldrag` carries hold_ms = settle ms AFTER
        # the release (a 1px release in the same cell is a no-op drop).
        ldrag)
            [[ $target == *","* && $hold_ms =~ ^[0-9]+$ ]] || exit 65
            client_focus
            read -r sx sy _rest <<<"$target"
            xdotool mousemove "$sx" "$sy"
            xdotool mousedown 1
            xdotool mousemove $((sx+1)) $((sy+1))
            read -r _ _ tail <<<"$target"
            # glide over every waypoint with a small delay per step. Each
            # waypoint is "x,y" and xdotool mousemove takes TWO separate
            # arguments -- passing the comma-joined string verbatim is a
            # usage error ("expected 2 coordinates"), so split it here.
            IFS=' ' read -r -a pts <<<"$tail"
            for p in "${pts[@]}"; do
                IFS=, read -r px py <<<"$p"
                xdotool mousemove "$px" "$py"; sleep 0.15
            done
            IFS=, read -r px py <<<"${pts[$((${#pts[@]}-1))]}"
            xdotool mousemove "$px" "$py"  # the release lands on the last waypoint
            sleep "$(awk -v ms="$hold_ms" 'BEGIN {printf "%.3f",ms/1000}')"
            xdotool mouseup 1 ;;
        # B1076 (#396): the button-3 version. The official client's item
        # drag is a right-button drag (the cursor picks the item up on a
        # right press; a left press on a held item DROPS it), so the scribe
        # gesture -- scroll from inventory onto the open book -- rides
        # button 3, and the memorise drag-to-gem does too.
        rdrag)
            [[ $target == *","* && $hold_ms =~ ^[0-9]+$ ]] || exit 65
            client_focus
            read -r sx sy _rest <<<"$target"
            xdotool mousemove "$sx" "$sy"
            xdotool mousedown 3
            xdotool mousemove $((sx+1)) $((sy+1))
            read -r _ _ tail <<<"$target"
            IFS=' ' read -r -a pts <<<"$tail"
            for p in "${pts[@]}"; do
                IFS=, read -r px py <<<"$p"
                xdotool mousemove "$px" "$py"; sleep 0.15
            done
            IFS=, read -r px py <<<"${pts[$((${#pts[@]}-1))]}"
            xdotool mousemove "$px" "$py"  # the release lands on the last waypoint
            sleep "$(awk -v ms="$hold_ms" 'BEGIN {printf "%.3f",ms/1000}')"
            xdotool mouseup 3 ;;
        rclick)
            # B1052: a 1px right-drag, jrclick's shape with the no-WM focus
            # rule of jclick/jrclick (B1073). The oracle's display for a RIGHT
            # press is up only WHILE HELD (B442b row 25). The probe-1 leg
            # showed no display at t=0.4 s, so the shot now runs at FOUR
            # times during the hold (0.4 s, 1.0 s, 2.0 s, 4.0 s -- each one
            # that fits inside hold_ms) to pin WHEN the window comes up,
            # not just that it does.
            [[ $target =~ ^[0-9]+,[0-9]+$ ]] || exit 65
            [[ ${hold_ms:-0} =~ ^[0-9]+$ ]] || exit 65
            rc_hold_ms=$hold_ms
            [[ $rc_hold_ms -lt 600 ]] && rc_hold_ms=600
            IFS=, read -r x y <<<"$target"
            client_focus
            xdotool mousemove "$x" "$y"; xdotool mousedown 3
            xdotool mousemove $(( x + 1 )) $(( y + 1 ))
            rc_elapsed=0
            for rc_off in 0.4 1.0 2.0 4.0; do
                rc_wait=$(awk -v e="$rc_elapsed" -v o="$rc_off" 'BEGIN{printf "%.1f", o-e}')
                [[ $rc_wait == -* ]] && break
                sleep "$rc_wait"
                rc_elapsed=$(awk -v e="$rc_elapsed" -v w="$rc_wait" 'BEGIN{printf "%.1f", e+w}')
                shot "$name-held${rc_off/./}" || :
            done
            xdotool mouseup 3 ;;
        rclick-still)
            # B1052 probe: a STATIONARY right press-hold -- mousedown, NO
            # movement at all, shots at 0.4/1.0/2.0/4.0 s, release. The
            # 1px-drag rclick suppresses nothing per B1073, but the display
            # has not appeared in 68 attempts, so this isolates the drag.
            [[ $target =~ ^[0-9]+,[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"
            client_focus
            xdotool mousemove "$x" "$y"; sleep 0.6
            xdotool mousedown 3
            rc_elapsed=0
            for rc_off in 0.4 1.0 2.0 4.0 10.0 20.0 28.0; do
                rc_wait=$(awk -v e="$rc_elapsed" -v o="$rc_off" 'BEGIN{printf "%.1f", o-e}')
                [[ $rc_wait == -* ]] && break
                sleep "$rc_wait"
                rc_elapsed=$(awk -v e="$rc_elapsed" -v w="$rc_wait" 'BEGIN{printf "%.1f", e+w}')
                shot "$name-held${rc_off/./}" || :
            done
            xdotool mouseup 3 ;;
        altclick-still)
            # B1052 probe: ALT+LEFT on a slot -- the client's own tip window
            # names it as the display's second trigger, and B442b row 26 says
            # it STAYS open until Escape (unlike the right-hold), so the
            # window is up AFTER the click and the still is trivial.
            [[ $target =~ ^[0-9]+,[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"
            client_focus
            xdotool mousemove "$x" "$y"; sleep 0.6
            xdotool keydown alt; xdotool mousedown 1; sleep 0.35
            xdotool mouseup 1; xdotool keyup alt
            sleep 0.6
            shot "$name-held04" || :
            sleep 1.0
            shot "$name-held10" || :
            ;;
        click-template) click_template "$target" "$hold_ms" "$name" || exit 65 ;;
        wait-template) wait_template "$target" "$hold_ms" "$name" || exit 65 ;;
        press) [[ -z $OBSERVER ]] || client_focus; xdotool key "$target" ;;
        hold)
            [[ $target =~ ^[0-9]+,[0-9]+$ && $hold_ms =~ ^[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"
            client_focus   # B1073: same no-WM focus rule as face_open_click
            xdotool mousemove "$x" "$y"; xdotool mousedown 1
            xdotool mousemove "$((x+1))" "$((y+1))"
            sleep "$(awk -v ms="$hold_ms" 'BEGIN {printf "%.3f",ms/1000}')"
            xdotool mouseup 1 ;;
        random) jclick 148 348 ;;
        accept) require_face_open accept || exit 65; jclick 50 519 ;;
        cancel) require_face_open cancel || exit 65; jclick 50 539 ;;
        open-face)
            # B888-CC1 (#204): open the FacePick modal of an ALREADY OPEN creation
            # screen (the stock stills' state: Set Face on the creation page).
            # The Set Face button is CC0_Face_Button, XML (2,45,64,24) in the
            # 0a row that also carries Male/Female (XML (70,45)/(134,45)): on
            # the lane-5 rig (B1057 measured) Male is display (105,252) and
            # Female (169,252) -- a 1:1 x-map (offset -1) -- so Set Face's
            # centre is display (37,252). The modal is a popup: it opens over
            # the creation screen without closing it.
            jclick 37 252
            sleep 2
            face_open || { shot "$name-face-missing"; echo 'open-face did not open the modal' >&2; exit 65; } ;;
        open-creation)
            # B888-CC1 (#204): leave the creation screen if the previous
            # pair's flow is still inside it (the Back button is shared by
            # both pages: page 1 -> page 0 -> character select; the live
            # creation screen opens on page 0), then open it fresh and
            # select the target's race and gender -- WITHOUT submitting
            # (no class, no points, no page 1, no name, no create
            # packet). This is exactly the state the stock UseLuclin=FALSE
            # stills were taken in: the pair's FacePick modal over the
            # creation page. Plan line: open-creation <race> <gender>
            # <still_name> (3 fields, the still label). The host report
            # must not require a decoded create packet for this verb (it
            # only does for the create verb).
            [[ $arg3 == Male || $arg3 == Female ]] || exit 65
            race_row=$(race_y "$target") || exit 65
            gender_row=$(gender_x "$arg3") || exit 65
            if screen_text | grep -qi 'Character Creation'; then
                # Previous pair is still inside: page 0 Back -> select.
                jclick 899 23; sleep 6
                screen_text | grep -qi 'Character Creation' && { shot "$name-back-stuck"; echo 'open-creation: Back did not leave the creation screen' >&2; exit 65; }
            fi
            screen_text | grep -qiE 'Character Select|Create a New Character' || { shot "$name-no-select"; echo 'open-creation: character select not visible' >&2; exit 65; }
            jclick 123 421; sleep 3
            if ! screen_text | grep -qi 'Character Creation'; then
                jclick 123 421; sleep 12
            fi
            screen_text | grep -qi 'Character Creation' || { shot "$name-create-slot-missing"; echo 'open-creation: slot did not open' >&2; exit 65; }
            # Let the restored selection finish re-rendering (create verb).
            sleep 12
            select_once "$target" 53 "$race_row" || exit 65
            select_once "$arg3" "$gender_row" 252 || exit 65
            selected_at "$target" 53 "$race_row" || exit 65
            selected_at "$arg3" "$gender_row" 252 || exit 65
            shot "$name-pair-selected" || : ;;
        race-table)
            # B888-CC1 (#204): ONE still of the fresh creation page 0 (the
            # full race list, no pair selected) so the race-row y-centres can
            # be measured off it. Plan line: race-table 0 0 <label> (the
            # label is the still name, arg4). The caller must be at character
            # select (see close-creation). No selection, no class, no create
            # packet.
            creation_pair_open "race-table" "none" "$name" || exit 65
            shot "$name" ;;
        close-creation)
            # B888-CC1 (#204): the one way off the creation page. The
            # FacePick modal, if open, is closed by ITS Cancel button (50,539
            # -- root (50,569); the modal is the only widget there, so the
            # click is safe ONLY when face_open holds). Then the page-0 Back
            # button returns to character select. Its position was MEASURED
            # (image (1100,15) -> display (1100,45); the still crop is
            # +0+30-offset, the xdotool clicks are root/display): a 4x crop
            # of the pilot's 02_race-table still puts the Back/Next pair at
            # the top-right, Back centred ~(1100,15) image. The old 899,23
            # was never exercised and is wrong. The heading's disappearance
            # is polled (the re-render takes a few seconds).
            # Postcondition: the character-select "Create a New Character"
            # slots (OCR-reliable on the lane-5 stills -- the only page
            # signature that reads across all psm/mode combinations).
            # Close the modal FIRST, with retries: a single Cancel click can
            # be dropped by the rig. The button ROW was MEASURED from the
            # full26b/c stills (bright-run scan, image space): the text
            # Random/Accept/Cancel sits at image y ~486-492 -- Random
            # centre ~(47,489), Accept ~(115,489), Cancel ~(132,489) -- so
            # the button bodies centre at ROOT y ~520 (the still crop is
            # +0+30-offset). The earlier 539/569 sweeps were ~20-45 px BELOW
            # the buttons, which is why every click missed. Sweep the three
            # measured centres at the measured row (plus one 6 px low for
            # the body height); clicking between buttons is harmless. Poll
            # face_open (the modal's own button strip) after every click.
            for coord in '47 520' '115 520' '132 520' '47 526' '115 526' '132 526' '50 539' '140 539'; do
                face_open || break
                set -- $coord; cxb=$1; cby=$2
                jclick "$cxb" "$cby"; sleep 3
            done
            face_open && { shot "$name-modal-still-open"; echo 'close-creation: modal still open after Cancel retries' >&2; exit 65; }
            if creation_page_up; then
                # The Back button's centre is ~(1100-1130, 45-55) display
                # (two 3x/4x vision reads of the pilot stills, 2026-10-14;
                # the old 899,23 was never exercised and is wrong). Sweep
                # both estimates: clicking between the top-right controls is
                # harmless. Poll the heading after each (re-render is slow).
                for coord in '1100 45' '1130 55'; do
                    set -- $coord; cxb=$1; cby=$2
                    jclick "$cxb" "$cby"
                    for i in 1 2 3 4; do
                        sleep 3
                        if ! creation_page_open; then break 2; fi
                    done
                done
                creation_page_open && { shot "$name-back-stuck"; echo 'close-creation: Back did not leave the creation screen' >&2; exit 65; }
            fi
            # In-driver proof we reached character select: the creation
            # heading must be GONE after Back (the one reliable creation-page
            # signal). We do NOT assert a "Character Select" string appears --
            # full-screen psm 6 reads of the panel title and the bottom "Return
            # Home" row both flake on the lane-5 rig (the 2026-10-14 pilots),
            # so a positive text assertion would be a flaky gate.
            if creation_page_up; then
                shot "$name-still-creation"; echo 'close-creation: creation heading still present after Back' >&2; exit 65
            fi
            # The modal must be GONE too (the first check ran before the Back
            # re-render finished): the full26 run exited with the heading check
            # passed and the modal still up, so the next pair's slot click
            # landed on nothing. The strip read (face_open) is cropped, not
            # flaky.
            sleep 2
            face_open && { shot "$name-modal-reopened"; echo 'close-creation: modal open after leaving the creation page' >&2; exit 65; }
            shot "$name" ;;
        facepick-rows)
            # B888-CC1 (#204): the capture leg. Plan line (tab fields):
            # facepick-rows <race> 0 0 <gender> <label> -- race in target,
            # gender in arg5, the still label in arg6 (the driver's
            # still_name slot is arg4, so this verb keeps the label in arg6,
            # the same column the create verb's still_name uses). Select the
            # pair, open the FacePick modal, still it, and run the
            # calibrated arrow probe IN-DRIVER so the step emits its own
            # TSV line (no separate host-side probe step that could read a
            # different frame). No class, no create packet.
            [[ $arg5 == Male || $arg5 == Female ]] || exit 65
            [[ -n $arg6 && $arg6 =~ ^[A-Za-z0-9_-]+$ ]] || exit 65
            creation_pair_open "$target" "$arg5" "$name" || exit 65
            # matrix3 (the reference stock capture, oracle/matrix_capture3.sh)
            # opens Set Face in a retry loop and STILLs the modal directly --
            # no close/reopen. A re-open does NOT re-render the picker list on
            # this rig: it strands the modal in the half-open "FACE + buttons
            # only" state, which is exactly the full26/pilot2 failure (all
            # pairs stilled before the list rendered). So mirror matrix3: open
            # once and settle until the list is on screen.
            # B1073 (#393): on lane 5 every open attempt is logged in the
            # timeline so a capture run can prove the FIRST attempt opened
            # the modal -- the acceptance bar for the no-WM rig (a retry is a
            # focus/sequence defect, not an acceptable settle).
            for open_attempt in 1 2 3 4 5; do
                face_open && break
                [[ $ORACLE_LANE == 5 ]] && mark face-open "attempt=$open_attempt at=37,251"
                face_open_click 37 251; sleep 2
                xdotool mousemove 600 900
            done
            face_open || { shot "$name-face-missing"; echo 'facepick-rows: modal did not open' >&2; exit 65; }
            # Settle until the picker LIST renders. The FACE row alone reads
            # ~74 in BOTH the half-open and rendered states, so it cannot
            # discriminate; the ROW COUNT can (facepick_row_count: half-open =
            # 1, rendered = >= 2, because every stock pair shows at least
            # face+eyes). matrix3 stills the modal right after it opens, but
            # its capture settles ~2s; the re-render can take longer, so poll
            # the row count for up to ~24s.
            for settle in $(seq 1 12); do
                sleep 2
                shot "$name" || :
                if [[ $(facepick_row_count "$DIR/$name.png") -ge 2 ]]; then
                    break
                fi
            done
            # If it is STILL the half-open state after the settle window, do
            # ONE verified close (the MEASURED Cancel row, not 50,539 which
            # full26/b/c proved ~20px below the button) -> reopen, then settle
            # again. The close is verified (face_open false) before the
            # re-open, so the re-open is never a no-op on an already-open
            # modal.
            if [[ $(facepick_row_count "$DIR/$name.png") -lt 2 ]]; then
                for coord in '47 520' '115 520' '132 520' '47 526' '115 526' '132 526' '50 539' '140 539'; do
                    face_open || break
                    set -- $coord; cxb=$1; cby=$2
                    jclick "$cxb" "$cby"; sleep 3
                done
                face_open && { shot "$name-close-failed"; echo 'facepick-rows: modal still open after Cancel retries' >&2; exit 65; }
                for open_attempt in 1 2 3 4 5; do
                    face_open && break
                    face_open_click 37 251; sleep 2
                    xdotool mousemove 600 900
                done
                face_open || { shot "$name-face-missing2"; echo 'facepick-rows: modal not open after re-open' >&2; exit 65; }
                for settle in $(seq 1 12); do
                    sleep 2
                    shot "$name" || :
                    [[ $(facepick_row_count "$DIR/$name.png") -ge 2 ]] && break
                done
            fi
            # The still must show a rendered list (>= 2 rows). Every stock
            # pair shows at least face+eyes; a single-row read means the list
            # never rendered (the half-open state) and the step FAILS rather
            # than recording an untrustworthy row (the full26f failure).
            shot "$name" || :
            [[ $(facepick_row_count "$DIR/$name.png") -ge 2 ]] || { shot "$name-face-not-rendered"; echo 'facepick-rows: picker list not rendered -- only the FACE row is bright, the still is the half-open state' >&2; exit 65; }
            facepick_probe_tsv "$DIR/$name.png" "$target" "$arg5" "$DIR/rows.tsv" || exit 65 ;;
        create)
            [[ $target =~ ^[A-Za-z]{3,15}$ && ( $gender == Male || $gender == Female ) ]] || exit 65
            race_row=$(race_y "$race") || exit 65
            class_row=$(class_y "$class") || exit 65
            city_row=$(city_y "$race") || exit 65
            # Untouched appearance: select race, class, gender, required points,
            # deity/city and name. No appearance controls are used.
            jclick 123 421; sleep 3
            if ! screen_text | grep -qi 'Character Creation'; then
                jclick 123 421; sleep 12
            fi
            screen_text | grep -qi 'Character Creation' || { echo "create slot did not open" >&2; exit 65; }
            # Let the restored selection finish re-rendering before the first
            # click (measured 12 s in diag-charcreate2); a click on a half
            # rendered list can light a row the cursor does not hold.
            sleep 12
            if [[ $CREATE_CLICKS == template ]]; then
                # B1058: each row is LOCATED in a fresh shot (templates/),
                # clicked, and its yellow state polled at the located point.
                select_template "race_$race" "$race" "$name" || exit 65
                race_x=$SEL_X race_yy=$SEL_Y
                select_template "class_$class" "$class" "$name" || exit 65
                class_x=$SEL_X class_yy=$SEL_Y
                select_template "gender_$gender" "$gender" "$name" || exit 65
                gender_xx=$SEL_X gender_yy=$SEL_Y
            else
                select_once "$race" 53 "$race_row" || exit 65
                select_once "$class" 153 "$class_row" || exit 65
                select_once "$gender" "$(gender_x "$gender")" 252 || exit 65
                race_x=53 race_yy=$race_row class_x=153 class_yy=$class_row
                gender_xx=$(gender_x "$gender") gender_yy=252
            fi
            jrclick 1170 348; sleep 3
            jrclick 1170 389; sleep 3
            # One consolidated selected-state check after all three controls,
            # before any other click touches the shared cursor.
            selected_at "$race" "$race_x" "$race_yy" || exit 65
            selected_at "$class" "$class_x" "$class_yy" || exit 65
            selected_at "$gender" "$gender_xx" "$gender_yy" || exit 65
            shot "$name-attributes"
            if [[ $CREATE_CLICKS == template ]]; then
                # Page 2 is up when its Get Name button is (poll, RULE 2),
                # then the same 3 s the deity click had after its own sleep.
                jclick 1243 54
                wait_template name_getname 30000 "$name" || exit 65
                getname_x=$LOC_X getname_y=$LOC_Y
                sleep 3
            else
                jclick 1243 54; sleep 15
            fi
            jclick 105 271; sleep 2
            jclick 1218 "$((city_row+17))"
            if [[ $CREATE_CLICKS == template ]]; then
                # The field is 103 px left of the Get Name centre (lane-5
                # name-check.png: Get Name centre frame 171,47, field 11..129).
                name_x=$((getname_x-103)) name_y=$getname_y
                template_log "$name" name_getname "$name_x" "$name_y" "$LOC_SCORE" "$LOC_PATH" name-field 0
            else
                name_x=68 name_y=77
            fi
            jclick "$name_x" "$name_y"; xdotool key End
            for unused in {1..20}; do xdotool key BackSpace; done
            xdotool key a; xdotool key BackSpace
            xdotool type --delay 100 "$target"; sleep 2
            if [[ $CREATE_CLICKS == template ]]; then name_check=(name_verify "$getname_x" "$getname_y" "$target" "$name")
            else name_check=(name_filled); fi
            if ! "${name_check[@]}"; then
                # Lost xdotool keystrokes (fix2h: empty field -> "Name is not
                # valid" dialog, zero create packets): re-type once, verify again.
                jclick "$name_x" "$name_y"; xdotool key End
                for unused in {1..20}; do xdotool key BackSpace; done
                xdotool type --delay 100 "$target"; sleep 2
                "${name_check[@]}" || { shot "$name-not-typed"; echo "name field not verified after re-type (${RT_DECIDER:-presence} '${RT_TEXT:-}')" >&2; exit 65; }
            fi
            shot "$name-before-submit"
            # The race/class/gender state was already verified on page 1 (the
            # consolidated check after the ability-point clicks, above) and is
            # the final authority only in the decoded create packet. There is
            # deliberately NO selected_at re-check here: this is page 2
            # (deity/city), where those page-1 list coordinates no longer hold
            # the selection, so a re-check would test the wrong pixels and fail
            # a correct run (measured in barbarian-fix2i: selected-Barbarian at
            # 53,291 read no yellow on page 2 and aborted a good capture).
            submitted=0
            for attempt in 1 2 3 4 5; do
                jclick 1243 54; sleep 12
                if screen_text | grep -qiE 'Character Select|Create a New Character'; then
                    submitted=1
                    break
                fi
            done
            # OCR of the post-submit page varies under Wine. The host report
            # requires OP_CharacterCreate in this interval; the window check
            # below only proves that a live client remains after the click.
            : ;;
        command) [[ -z $OBSERVER ]] || client_focus; xdotool key Return; xdotool type --delay 80 "$target"; xdotool key Return ;;
        explore-open)
            # B888-CC3 (#204): enter the character-select screen's Explore
            # mode. CSW_Explore_Button is XML (2,60) 48x32. Location MEASURED
            # by the y-sweep on this rig (oracle-diag run, 2026-10-15): clicks
            # at root y 220/231/241/252 produced no state change (full-frame
            # diff ~6 against the settle baseline), the click at ROOT (40,263)
            # entered Explore (full-frame diff 56.4: the left window hides,
            # the 3D scene changes, the ExploreModeWindow appears). The
            # postcondition is EXW_InfoLabel1's "Click on a character"
            # (EQUI_CharacterSelect.xml; the window sits bottom-LEFT over the
            # 3D area on this rig, not centre).
            jclick 40 263
            sleep 3 ;;
        explore-esc)
            # B888-CC3 (#204): leave Explore mode with Esc (EXW_InfoLabel2:
            # "Press Enter to enter the game, or press Esc to leave explore
            # mode."). The X11 keysym is `Escape` (XK_Escape 0xFF1B) -- this
            # rig's xdotool rejects both `Esc` and the lowercase `esc`
            # ("No such key name"), so the first two A-run attempts failed at
            # this step; `Escape` is the canonical name.
            xdotool key Escape
            sleep 3 ;;
        hold-d)
            # B888-CC3 (#204): hold the d key (Explore camera forward) for
            # hold_ms. A slow multi-step glide would test DirectInput polling
            # differently than the official client's own key auto-repeat, so
            # hold-d keeps the key down for the whole interval: the transient
            # motion is on the video.
            [[ $hold_ms =~ ^[0-9]+$ ]] || exit 65
            xdotool keydown d
            sleep "$(awk -v ms="$hold_ms" 'BEGIN {printf "%.3f",ms/1000}')"
            xdotool keyup d ;;
        orbit)
            # B888-CC3 (#204): a slow multi-step mouse drag over the scene
            # (Explore camera orbit) with the LEFT BUTTON HELD (mouse look
            # samples motion only while pressed; wine-oracle.md's orbit()
            # glides exactly this way). target is the DELTA in px "dx,dy";
            # the drag starts at (640,400), inside the 3D scene. 40 steps,
            # 100 ms apart (DirectInput polls motion; an instant delta is
            # dropped).
            [[ $target =~ ^-?[0-9]+,-?[0-9]+$ ]] || exit 65
            IFS=, read -r dx dy <<<"$target"
            xdotool mousemove 640 400
            xdotool mousedown 1
            for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do
                xdotool mousemove_relative -- "$((dx/40))" "$((dy/40))"
                sleep 0.1
            done
            xdotool mouseup 1 ;;
        wheel)
            # B888-CC3 (#204): the Explore camera zoom, N wheel-up clicks
            # (hold_ms, default 3).
            n=${hold_ms:-3}
            [[ $n =~ ^[0-9]+$ ]] || exit 65
            xdotool mousemove 640 400
            for unused in $(seq 1 "$n"); do
                xdotool click 4
                sleep 0.3
            done ;;
        enter-press)
            # B888-CC3 (#204): press Return in Explore mode -- the documented
            # "Press Enter to enter the game". The postcondition is the
            # EverQuest window (the world). Which CHARACTER entered is the
            # pcap's OP_EnterWorld plus the world log, not this verb.
            xdotool key Return
            sleep 8 ;;
        camp-quit)
            # B888-CC3 (#204): the clean in-world exit (wine-oracle.md):
            # jclick Actions -> "Camp (Exit)" ~(945,745) at 1280x1024 with
            # the 1280x800 client at root offset (-1,30). The wait gives the
            # world time to flush the character (the driver's restore stops
            # the world, which writes the zone-in back).
            jclick 945 745
            sleep 30 ;;
        still) : ;;
        wait) [[ $target =~ ^[0-9]+$ ]] || exit 65; sleep "$(awk -v ms="$target" 'BEGIN {printf "%.3f",ms/1000}')" ;;
        *) exit 65 ;;
    esac
    sleep 2
    if ! alive; then mark DISCONNECT "$name"; exit 67; fi
    if [[ $verb == create || $verb == facepick-rows || $verb == still ]]; then expected=$(expected_for "$verb" "$target")
    else expected=$(expected_for "$verb" "$target" "$arg5"); fi || exit 65
    if [[ $verb == facepick-rows || $verb == still ]]; then
        # No full-screen postcondition: facepick-rows already required
        # face_open (the modal's own Random/Accept/Cancel strip, cropped
        # read) before it probed, and that cropped strip is the reliable
        # signal -- the full-screen OCR mangles the same words (measured
        # 'Randam Accent Cance!'), and the creation page behind the modal
        # has no legible heading under psm 6 (the 2026-10-14 pilots).
        mark END "$name"
        shot "$name" || :
        cp "$CLIENT/${CHARACTER}_".ini "$DIR/$name.ini" 2>/dev/null || :
        continue
    fi
    assert_action_state "$verb" "$expected" "$name" || exit 65
    mark END "$name"
    shot "$name" || exit 65
    cp "$CLIENT/${CHARACTER}_"*.ini "$DIR/$name.ini" 2>/dev/null || :
done <"$DIR/plan.tsv"
[[ -f $DIR/template-clicks.tsv ]] && printf '%s\n' "$SETTLE_RETRIES" >"$DIR/settle-retries.txt"
stop_capture
