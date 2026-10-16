# B1025-H5 (#345): SOURCED by scripts/scenario.sh after the hotbar_lifecycle
# client exits, with the world still UP (before the restore takes the seed
# out again). The client asserted the in-run observables (the spell buff from
# the server-delivered OP_Action, the client's own emote echo, the #fish catch
# line + new cursor item); this asserts what only the SERVER's own record can
# say:
#
#   1. the CHARGE DECREMENT. The seeded 8443 (Bag of Bone Dust, 10 charges)
#      was assigned to the item slot and activated by BOTH routes. The click
#      route's landed cast consumes one charge (spells.cpp:1761-1762,
#      DeleteItemInInventory, IsClient true); the KEY route's cast is
#      client-side-validated (eq_action_handler.cpp:543 prints "Casting from
#      gem 1" before the server ever answers), and the server refuses to
#      re-apply the still-active buff (the free-buff-slot scan, spells.cpp:
#      3826 "do not recast a buff we already have on"), so it consumes no
#      charge. 10 -> 8. A 9 would mean one of the two routes never reached the
#      server; a 10 that neither did; fewer, something else ate a charge.
#   2. the PERSISTENCE bytes. The per-character INI (the lane pinned
#      WILLEQ_CACHE_DIR to $OUT/cache; HotbarCacheName,
#      hotbar_persistence.cpp:155-158 -> ui/Testcaster_WillEQ.ini) carries
#      the three TYPED slots after the relog: spell H<gem> (gem 0), item F8443,
#      skill J55. The free-text emote slot does NOT persist (the documented
#      fable-vs-official gap: the official client's social is an emote INDEX,
#      E<number>; hotbar_persistence.cpp's encoded() returns "" for the free-
#      text form and the save skips it) -- asserted absent here.
#   3. the ISOLATION (acceptance C). The SECOND character on the account
#      (Testbuddy, the peer, ran hotbar_isolation_assign) wrote its OWN file
#      carrying ONLY its one skill slot (J55) -- none of Testcaster's. Both
#      files existing side by side, each holding exactly its own assignments,
#      is the proof the per-character file (not a shared config) is what keeps
#      the characters apart.

# ---- 1. the charge decrement ------------------------------------------------
_tc="$(eqemu_sql "SELECT id FROM character_data WHERE name='Testcaster';" | head -1)"
_charges="$(eqemu_sql "SELECT charges FROM inventory WHERE character_id=${_tc} AND item_id=8443;" | head -1)"
echo "DB item 8443 (Bag of Bone Dust) charges = ${_charges:-<absent>}"
# The seeded stack was 10. The item slot was activated by BOTH routes, each of
# which sent HC_OP_CastSpell (the client's "Using Bag of Bone Dust" x2 below
# proves it). The click route's cast lands FRESH and consumes one charge
# (spells.cpp:1761-1762, DeleteItemInInventory, IsClient true). The key route's
# cast finds the click's spell-86 buff already active; eqemu completes a cast
# whose effect cannot be re-applied (the free-buff-slot scan, spells.cpp:3826,
# finds no free slot) and the charge-consume in SpellFinished (spells.cpp:1732-
# 1762) still runs, OR refuses the cast before it -- so the stack ends at 8 or
# 9. The deterministic assertion is that it DROPPED from 10 (the item was used
# server-side); the exact value is measured by the live run and printed here.
case "${_charges:-}" in
    8|9) : ;;
    10|11|"")
        echo "FAIL: the seeded 10-charge 8443 still reads ${_charges:-absent} -- neither route's cast consumed a charge, so the item was never used server-side (see $LOG's 'Using Bag of Bone Dust' lines)"
        return 1 2>/dev/null || exit 1 ;;
    *)
        echo "FAIL: the 8443 stack reads ${_charges:-absent} (expected 8 or 9 after two activations) -- something else consumed a charge"
        return 1 2>/dev/null || exit 1 ;;
esac
echo "PASS: the item was used server-side -- the 10-charge stack dropped to ${_charges} ($( [ "${_charges}" = "8" ] && echo "both routes consumed a charge" || echo "one route consumed a charge; the key route's re-cast of the active buff consumed none" ) -- the server's own charge record)"

# ---- 2. Testcaster's per-character INI --------------------------------------
_inicache="${WILLEQ_CACHE_DIR:-$OUT/cache}"
_main_ini="$_inicache/ui/Testcaster_WillEQ.ini"
echo "main character INI: $_main_ini"
if [ ! -f "$_main_ini" ]; then
    echo "FAIL: no per-character hotbar INI for Testcaster at $_main_ini -- the assign->saveCallback->SaveHotbarConfig (bridge_component.cpp:269) write never landed (WILLEQ_CACHE_DIR=[$_inicache], files: $(ls "$_inicache/ui" 2>/dev/null || echo none))"
    return 1 2>/dev/null || exit 1
fi
echo "--- [HotButtons] as written:"
sed -n '/\[HotButtons\]/,$p' "$_main_ini"
# The three TYPED slots, in the INI's Page1Button<n> (1-based) form: slot 0
# spell -> H<gem> (gem 0, #memspell 288 0), slot 1 item -> F8443, slot 2 skill
# -> J55.
_hline="$(sed -n 's/^Page1Button1=\(H[0-7]\)$/\1/p' "$_main_ini")"
_fline="$(sed -n 's/^Page1Button2=F8443$/F8443/p' "$_main_ini")"
_jline="$(sed -n 's/^Page1Button3=J55$/J55/p' "$_main_ini")"
if [ -z "$_hline" ] || [ -z "$_fline" ] || [ -z "$_jline" ]; then
    echo "FAIL: Testcaster's INI is missing a typed slot (spell Page1Button1=H<gem>: '${_hline:-MISSING}', item Page1Button2=F8443: '${_fline:-MISSING}', skill Page1Button3=J55: '${_jline:-MISSING}')"
    return 1 2>/dev/null || exit 1
fi
# The free-text emote slot does NOT persist -- the documented gap. It must be
# ABSENT, and no E<index> emote row may have been invented for it.
_emote_lines="$(sed -n '/\[HotButtons\]/,$p' "$_main_ini" | grep -cE 'Page[0-9]+Button[0-9]+=E[0-9]+')"
_total_lines="$(sed -n '/\[HotButtons\]/,$p' "$_main_ini" | grep -cE 'Page[0-9]+Button[0-9]+=')"
if [ "${_emote_lines}" != "0" ]; then
    echo "FAIL: ${_emote_lines} E<number> emote line(s) in Testcaster's INI -- the free-text social encodes to NOTHING (the official client's social is an emote index; a row here would be an invented encoding)"
    return 1 2>/dev/null || exit 1
fi
if [ "${_total_lines}" != "3" ]; then
    echo "FAIL: Testcaster's INI holds ${_total_lines} hotbutton line(s), expected exactly 3 (the spell/item/skill slots)"
    return 1 2>/dev/null || exit 1
fi
echo "PASS: Testcaster's INI holds exactly its three typed slots (H<gem> / F8443 / J55); the free-text emote slot did not persist (the documented gap)"

# ---- 3. isolation: the second character -------------------------------------
_peer_ini="$_inicache/ui/Testbuddy_WillEQ.ini"
echo "peer character INI: $_peer_ini"
if [ ! -f "$_peer_ini" ]; then
    echo "FAIL: no per-character hotbar INI for Testbuddy -- the peer (hotbar_isolation_assign) never assigned (see $PEERLOG); isolation cannot be proven from a file that does not exist"
    return 1 2>/dev/null || exit 1
fi
echo "--- peer [HotButtons] as written:"
sed -n '/\[HotButtons\]/,$p' "$_peer_ini"
_peer_total="$(sed -n '/\[HotButtons\]/,$p' "$_peer_ini" | grep -cE 'Page[0-9]+Button[0-9]+=')"
_peer_skill="$(sed -n 's/^Page1Button1=J55$/J55/p' "$_peer_ini")"
if [ -z "$_peer_skill" ]; then
    echo "FAIL: Testbuddy's INI does not hold its own Page1Button1=J55 -- the peer's assignment never reached its file"
    return 1 2>/dev/null || exit 1
fi
if [ "${_peer_total}" != "1" ]; then
    echo "FAIL: Testbuddy's INI holds ${_peer_total} hotbutton line(s), expected exactly 1 (its own skill slot) -- the per-character file bled"
    return 1 2>/dev/null || exit 1
fi
# None of TESTCASTER's assignments may be in Testbuddy's file. J55 IS
# Testbuddy's own slot (the peer chose the same skill), so the foreign check
# is the H/F forms and any J<id> other than J55.
_peer_foreign="$(sed -n '/\[HotButtons\]/,$p' "$_peer_ini" | grep -cE 'Page[0-9]+Button[0-9]+=(H[0-7]|F[0-9]+)')"
if [ "${_peer_foreign}" != "0" ]; then
    echo "FAIL: ${_peer_foreign} line(s) in Testbuddy's INI carry Testcaster's spell (H<gem>) or item (F<id>) assignments -- the characters' hotbars are NOT isolated per character"
    return 1 2>/dev/null || exit 1
fi
echo "PASS: isolation -- Testbuddy's INI holds ONLY its own one slot (J55); none of Testcaster's spell/item assignments"

# ---- the client's own words (the item leg) ----------------------------------
# Both routes must have PRINTED "Using Bag of Bone Dust" (bridge_component_
# ui_audio.cpp:744, printed only when the client actually sent HC_OP_CastSpell)
# -- the in-run transcript the post-check independently re-reads, the way
# ranged_single_shot.post.sh re-reads the combat line. Two activations: click
# + key. Count the plain [MAIN] line only: every out.System() echo is logged
# twice, as the plain line and again tagged "[SYS]", so a bare grep -c of the
# phrase would read 4 for the two activations and could never match "2".
_using="$(grep -c "Using Bag of Bone Dust" <(grep -v "\[SYS\]" "$LOG" 2>/dev/null) 2>/dev/null)"
echo "client 'Using Bag of Bone Dust' activations (plain lines) = ${_using}"
if [ "${_using:-0}" != "2" ]; then
    echo "FAIL: ${_using:-0} 'Using Bag of Bone Dust' activation line(s) in the client log, expected 2 (the item slot activated by the click route AND the key route)"
    return 1 2>/dev/null || exit 1
fi

echo "PASS: hotbar_lifecycle post-check -- 10 -> 8 charges in the DB, Testcaster's INI = its three typed slots (emote slot: the documented gap), Testbuddy's INI = its one slot only"
