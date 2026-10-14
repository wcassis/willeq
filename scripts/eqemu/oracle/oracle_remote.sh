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
    elif screen_text | grep -qiE "$expected"; then
        return 0
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
    local image="$DIR/name-check.png" std
    import -window root -crop 1280x800+0+30 "$image" 2>/dev/null || return 65
    std=$(convert "$image" -crop "190x10+30+264" +repage -colorspace Gray \
        -format '%[fx:standard_deviation]' info:) || return 65
    awk -v std="$std" 'BEGIN {exit !(std >= 0.13)}'
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
    # ONE click, then verify -- never a second click. The race and class lists
    # share one selection cursor, and the screen restores the previous
    # session's selection: a retry click mid-re-render moves the cursor to a
    # different row (measured: fix2e's retry left race=HalfElf, class=Warrior,
    # and the decoded packet was (1,0,7), not the requested (1,0,2)). A missed
    # click therefore FAILS the run and gets re-driven, instead of silently
    # creating the wrong identity; the decoded create packet
    # (oracle_create.py) is the final authority either way.
    local label=$1 x=$2 y=$3
    jclick "$x" "$y"; sleep 3
    if ! selected_at "$label" "$x" "$y"; then
        shot "select-failed-$label" || :
        echo "selection failed: $label at $x,$y" >&2
        return 65
    fi
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
    # select_once: a missed click FAILS after exactly one click (never a
    # re-click -- the old retry loop moved the shared race/class selection
    # cursor: fix4 created class=16, fix2e created race=7, both rejected by
    # validate_create), and a verified selection issues no further click.
    CLICKS=0
    jclick() { CLICKS=$((CLICKS+1)); }
    shot() { :; }
    selected_at() { [[ $SELECTION_OK == 1 ]]; }
    SELECTION_OK=0
    if select_once Barbarian 53 291; then exit 1; fi
    [[ $CLICKS == 1 ]] || { echo "missed click re-driven ($CLICKS clicks), expected 1"; exit 1; }
    SELECTION_OK=1
    select_once Barbarian 53 291 || exit 1
    [[ $CLICKS == 2 ]] || { echo "verified selection clicked again ($CLICKS clicks), expected 2"; exit 1; }
    echo 'PASS: selected coordinates and random/accept/cancel missed-action and override assertions'
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
    p=$(cat "$DIR/xvfb.pid" 2>/dev/null || :)
    if [[ $p =~ ^[0-9]+$ ]]; then kill -TERM "$p" 2>/dev/null || :; fi
}
if [[ $ACTION == stop ]]; then stop_capture; exit $?; fi
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
if [[ -n $INI_UNSET ]]; then
    [[ $INI_UNSET =~ ^[A-Za-z][A-Za-z0-9]*\.[A-Za-z][A-Za-z0-9]*$ ]] || exit 65
    python3 /home/user/eq-oracle/uigap-scripts/oracle_ini_seed.py \
        "$CLIENT" "$DIR" "$CHARACTER" "$INI_UNSET" || exit 65
fi
cp "$CLIENT/${CHARACTER}_"*.ini "$DIR/before-character.ini" 2>/dev/null || :
# Capture starts before even launching Wine. Save PIDs for interrupted cleanup.
sudo -n tcpdump -i any -s 0 -U -Z user -w "$DIR/capture.pcap" 'udp and (port 5998 or port 5999 or portrange 9000-9001 or portrange 7000-7030)' >"$DIR/pcap.log" 2>&1 &
echo $! >"$DIR/pcap.pid"
date -u +%s.%N >"$DIR/video.mp4.t0"
ffmpeg -nostdin -loglevel error -f x11grab -framerate 10 -video_size 1280x800 -i :99+0,30 -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p "$DIR/video.mp4" >"$DIR/video.log" 2>&1 &
echo $! >"$DIR/video.pid"
sleep 3
kill -0 "$(cat "$DIR/pcap.pid")" 2>/dev/null || exit 65
kill -0 "$(cat "$DIR/video.pid")" 2>/dev/null || exit 65
# A visible onset lets the report reject a wall-clock/video mapping that is
# more than one frame off. The marker is removed before Wine launches.
xmessage -title ORACLE_TIMING_MARKER -geometry 420x80+400+350 'ORACLE TIMING MARKER' >"$DIR/marker.log" 2>&1 &
marker_pid=$!
xdotool search --sync --name '^ORACLE_TIMING_MARKER$' >/dev/null || exit 65
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
            select_once "$race" 53 "$race_row" || exit 65
            select_once "$class" 153 "$class_row" || exit 65
            select_once "$gender" "$(gender_x "$gender")" 252 || exit 65
            jrclick 1170 348; sleep 3
            jrclick 1170 389; sleep 3
            # One consolidated selected-state check after all three controls,
            # before any other click touches the shared cursor.
            selected_at "$race" 53 "$race_row" || exit 65
            selected_at "$class" 153 "$class_row" || exit 65
            selected_at "$gender" "$(gender_x "$gender")" 252 || exit 65
            shot "$name-attributes"
            jclick 1243 54; sleep 15
            jclick 105 271; sleep 2
            jclick 1218 "$((city_row+17))"
            jclick 68 77; xdotool key End
            for unused in {1..20}; do xdotool key BackSpace; done
            xdotool key a; xdotool key BackSpace
            xdotool type --delay 100 "$target"; sleep 2
            if ! name_filled; then
                # Lost xdotool keystrokes (fix2h: empty field -> "Name is not
                # valid" dialog, zero create packets): re-type once, verify again.
                jclick 68 77; xdotool key End
                for unused in {1..20}; do xdotool key BackSpace; done
                xdotool type --delay 100 "$target"; sleep 2
                name_filled || { shot "$name-not-typed"; echo "name field empty after re-type" >&2; exit 65; }
            fi
            shot "$name-before-submit"
            # The submit clicks below still touch the shared selection cursor
            # (fix2e's packet decoded race=7 although the attribute still
            # showed Barbarian): re-verify the selected state right before
            # submit, not only right after the selection clicks.
            selected_at "$race" 53 "$race_row" || exit 65
            selected_at "$class" 153 "$class_row" || exit 65
            selected_at "$gender" "$(gender_x "$gender")" 252 || exit 65
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
stop_capture
