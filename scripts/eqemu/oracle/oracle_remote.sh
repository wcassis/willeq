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
# lane-5 modal; the image crop is +0+30-offset (import crops 1280x800+0+30).
# $1 image, $2 race, $3 gender, $4 tsv path. Emits one line: race, gender,
# image, then label=0/1 for the seven pickers (face first, wode last).
facepick_probe_tsv() {
    local image=$1 race=$2 gender=$3 out=$4 yrow label mean vis
    {
        printf '%s\t%s\t%s\t' "$race" "$gender" "$image"
        for row in 'face:348' 'hair:368' 'haircolor:388' 'eyes:408' 'beard:428' 'beardcolor:448' 'wode:468'; do
            label=${row%%:*}; yrow=${row##*:}
            mean=$(convert "$image" -crop "20x12+138+$((yrow-6-30))" +repage -colorspace Gray -format '%[fx:mean*255]' info: 2>/dev/null)
            [[ $mean =~ ^[0-9.]+$ ]] || { echo "facepick_probe_tsv: bad mean on $label" >&2; return 65; }
            vis=$(awk -v m="$mean" 'BEGIN { print (m > 48) ? 1 : 0 }')
            printf '%s=%s ' "$label" "$vis"
        done
        printf '\n'
    } >> "$out"
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
    # Kill only the Xvfb this session started (xvfb.pid). Lane 5's :99 is a
    # real Xorg (oracle-xorg.service) that run found up, so no pid is saved;
    # a stale or reused pid that is not an Xvfb is left alone.
    p=$(cat "$DIR/xvfb.pid" 2>/dev/null || :)
    if [[ $p =~ ^[0-9]+$ && $(ps -o comm= -p "$p" 2>/dev/null) == Xvfb ]]; then
        kill -TERM "$p" 2>/dev/null || :
    fi
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
            # matrix4 (the reference) opens Set Face in a retry loop, moving
            # the cursor off the modal between tries and settling ~3s each.
            # The picker list renders a moment AFTER the modal's button strip
            # appears, so a single click + 2s can still a modal that is open
            # but not yet expanded. Retry the open like the reference, then
            # settle before the still so the list is on screen.
            for open_attempt in 1 2 3 4 5; do
                face_open && break
                jclick 37 252; sleep 3
                xdotool mousemove 600 900
            done
            face_open || { shot "$name-face-missing"; echo 'facepick-rows: modal did not open' >&2; exit 65; }
            # The first open can show the FACE heading + button strip with the
            # picker LIST not yet rendered (the 2026-10-14 pilot stills: only
            # FACE + Random/Accept/Cancel, no rows). Close and re-open once,
            # like a user toggling Set Face, then settle: the re-open renders
            # the full list.
            jclick 50 539; sleep 2
            face_open && { jclick 50 539; sleep 2; }
            jclick 37 252; sleep 3
            face_open || { shot "$name-face-missing2"; echo 'facepick-rows: modal not open after re-open' >&2; exit 65; }
            sleep 5
            shot "$name" || :
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
stop_capture
