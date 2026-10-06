# UIGAP-live (#213): SOURCED by scripts/scenario.sh after the ignore_tell
# client exits. The client asserted the ignored tell was NOT SHOWN; this
# asserts it was DELIVERED: the server sent it (the client's ZONE:DEBUG
# packet log) and the ignore filter dropped it (chat_component.cpp's
# MAIN:DEBUG "Ignored <channel> message from <sender>" line), and the peer
# really typed it.
_ignored="$(grep -ci 'Ignored tell message from Testbuddy' "$LOG")"
_sent="$(grep -c 'UIGAPLIVE-tell-2' "$PEERLOG")"
echo "client 'Ignored tell message from Testbuddy' lines: ${_ignored}; peer lines naming UIGAPLIVE-tell-2: ${_sent}"
grep -i 'Ignored .* message from' "$LOG" | tail -3
if [ "${_ignored:-0}" -lt 1 ]; then
    echo "FAIL: no evidence the ignored tell reached this client (delivered-but-hidden not shown)"
    return 1 2>/dev/null || exit 1
fi
if [ "${_sent:-0}" -lt 1 ]; then
    echo "FAIL: the peer's log never mentions the second tell"
    return 1 2>/dev/null || exit 1
fi
echo "PASS: the ignored tell was delivered to the client and dropped by its ignore filter"
