#!/usr/bin/env bash
# Runs on the paired oracle VM; no server DB writes here.
set -uo pipefail
ACTION=${1:-} DIR=${2:-}
[[ $ACTION == selftest || ( -n $DIR && $DIR == /home/user/eq-oracle/sessions/* ) ]] || exit 65
export DISPLAY=:99 WINEPREFIX=/home/user/eq-oracle/wineprefix WINEARCH=win32 WINEDEBUG=-all LP_NUM_THREADS=1
source /mnt/projects/eq-oracle/scripts/ui-lib.sh
CLIENT=/home/user/eq-oracle/client
alive() { pgrep -x eqgame.exe >/dev/null && xdotool search --name '^EverQuest$' >/dev/null 2>&1; }
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
        *) echo "${optional:-window:EverQuest}" ;;
    esac
}
face_open() { screen_text '205x400+0+180' | grep -qiE 'Random|Accept|Cancel'; }
require_face_open() { face_open || { echo "$1 without face dialog" >&2; return 65; }; }
assert_action_state() {
    local verb=$1 expected=$2 name=$3
    case $verb in
        random)
            face_open || { shot "$name-face-missing"; echo 'random missed face dialog' >&2; return 65; } ;;
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
    for kind in video pcap; do
        p=$(cat "$DIR/$kind.pid" 2>/dev/null || :)
        if [[ $p =~ ^[0-9]+$ ]]; then kill -INT "$p" 2>/dev/null || :; fi
        rm -f "$DIR/$kind.pid"
    done
    sleep 2
    cp "$CLIENT/eqclient.ini" "$DIR/eqclient-after.ini" 2>/dev/null || :
    cp "$CLIENT"/Testcaster_*.ini "$DIR/" 2>/dev/null || :
    cp "$CLIENT"/Testchar_*.ini "$DIR/" 2>/dev/null || :
    # VM-side pkill, not an eqemu-container pkill. This oracle VM is single-tenant
    # under the lane lock; leave no Wine client alive.
    pkill -TERM -x eqgame.exe 2>/dev/null || :
    sleep 2
    if [[ -s $DIR/character-file.txt && -f $DIR/character-original.ini && ! -e $DIR/character-restored ]]; then
        cp "$(cat "$DIR/character-file.txt")" "$DIR/character-after.ini" || :
        cp "$DIR/character-original.ini" "$(cat "$DIR/character-file.txt")" || return 65
        cmp -s "$DIR/character-original.ini" "$(cat "$DIR/character-file.txt")" || return 65
        touch "$DIR/character-restored"
    fi
    rm -f "$DIR/drive-login.sh"
    # Kill only the Xvfb this session started (xvfb.pid). Lane 5's :99 is a
    # real Xorg (oracle-xorg.service) that run found up, so no pid is saved;
    # a stale or reused pid that is not an Xvfb is left alone.
    p=$(cat "$DIR/xvfb.pid" 2>/dev/null || :)
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
CHARACTER=$(awk -F'\t' '$1=="login" {print $2; exit}' "$DIR/plan.tsv")
[[ $CHARACTER =~ ^[A-Za-z]+$ ]] || exit 65
INI_UNSET=$(sed -n 's/^# ini-unset=//p' "$DIR/plan.tsv" | head -1)
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
ffmpeg -nostdin -loglevel error -f x11grab -framerate 10 -video_size 1280x800 -i :99+0,30 -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p "$DIR/video.mp4" >"$DIR/video.log" 2>&1 &
echo $! >"$DIR/video.pid"
sleep 3
kill -0 "$(cat "$DIR/pcap.pid")" 2>/dev/null || exit 65
kill -0 "$(cat "$DIR/video.pid")" 2>/dev/null || exit 65
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
cd "$CLIENT" || exit 65
nohup wine eqgame.exe patchme >"$DIR/wine.log" 2>&1 </dev/null &
echo $! >"$DIR/wine.pid"
step=0
while IFS=$'\t' read -r verb target arg3 arg4 arg5 arg6; do
    [[ -z $verb || $verb == \#* ]] && continue
    step=$((step+1))
    hold_ms=$arg3 still_name=$arg4
    if [[ $verb == create ]]; then
        race=$arg3 class=$arg4 gender=$arg5 still_name=$arg6
    fi
    name=$(printf '%02d_%s' "$step" "${still_name:-$verb}")
    [[ $name =~ ^[A-Za-z0-9_-]+$ ]] || exit 65
    mark BEGIN "$name"
    case $verb in
        login)
            python3 /home/user/eq-oracle/uigap-scripts/oracle_login_patch.py \
                /home/user/eq-oracle/drive-login.sh "$DIR/drive-login.sh" || exit 65
            bash "$DIR/drive-login.sh" >"$DIR/login.log" 2>&1 || exit 65 ;;
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
        click-template) click_template "$target" "$hold_ms" "$name" || exit 65 ;;
        wait-template) wait_template "$target" "$hold_ms" "$name" || exit 65 ;;
        press) xdotool key "$target" ;;
        hold)
            [[ $target =~ ^[0-9]+,[0-9]+$ && $hold_ms =~ ^[0-9]+$ ]] || exit 65
            IFS=, read -r x y <<<"$target"
            xdotool mousemove "$x" "$y"; xdotool mousedown 1
            xdotool mousemove "$((x+1))" "$((y+1))"
            sleep "$(awk -v ms="$hold_ms" 'BEGIN {printf "%.3f",ms/1000}')"
            xdotool mouseup 1 ;;
        random) jclick 148 348 ;;
        accept) require_face_open accept || exit 65; jclick 50 519 ;;
        cancel) require_face_open cancel || exit 65; jclick 50 539 ;;
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
        command) xdotool key Return; xdotool type --delay 80 "$target"; xdotool key Return ;;
        still) : ;;
        wait) [[ $target =~ ^[0-9]+$ ]] || exit 65; sleep "$(awk -v ms="$target" 'BEGIN {printf "%.3f",ms/1000}')" ;;
        *) exit 65 ;;
    esac
    sleep 2
    if ! alive; then mark DISCONNECT "$name"; exit 67; fi
    if [[ $verb == create ]]; then expected=$(expected_for "$verb" "$target")
    else expected=$(expected_for "$verb" "$target" "$arg5"); fi || exit 65
    assert_action_state "$verb" "$expected" "$name" || exit 65
    mark END "$name"
    shot "$name" || exit 65
    cp "$CLIENT/${CHARACTER}_"*.ini "$DIR/$name.ini" 2>/dev/null || :
done <"$DIR/plan.tsv"
[[ -f $DIR/template-clicks.tsv ]] && printf '%s\n' "$SETTLE_RETRIES" >"$DIR/settle-retries.txt"
stop_capture
