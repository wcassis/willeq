#!/usr/bin/env bash
# B1214 (#533): install MacroQuest2 (MQEmulator's Titanium "Legacy Classic"
# build) beside a COPY of an r540 oracle lane's client + Wine prefix.
#
#   scripts/eqemu/oracle/mq2/install.sh install   <lane 1-4> <MQEmu_Legacy_Classic_Titanium.zip> [MQ2B1214.dll]
#   scripts/eqemu/oracle/mq2/install.sh uninstall <lane 1-4>
#
# Take the lane lease FIRST (scripts/eqemu/lease take --lane N ...) and release
# it after `uninstall`. Lane 5 is refused: its :99 is the NVIDIA Xorg and its
# prefix layout differs (docs/thinkpad.md); nothing here was measured there.
#
# What it does on the oracle VM (user `user`), never touching the canonical
# ~/eq-oracle/client or ~/eq-oracle/wineprefix:
#   ~/eq-oracle/b1214/{client,wineprefix}  full copies (cp -a; 6.1 GB + 581 MB)
#   ~/eq-oracle/b1214/wineprefix/drive_c/MQ2  MacroQuest2.exe, MQ2Main.dll and
#       three plugins (MQ2ChatWnd, MQ2Telnet, MQ2MoveUtils), MacroQuest.ini
#       trimmed to those plugins, MQ2Telnet.ini (port 2323, user b1214/spike),
#       the macros/ of this directory, and the optional MQ2B1214.dll
#   ~/eq-oracle/b1214/{launch.sh,mqtel.py,proof_a.sh,proof_cam.sh}
# Then: `ssh user@<oracle> bash ~/eq-oracle/b1214/launch.sh`, the lane's
# drive-login.sh patched by oracle_login_patch.py, Enter World, and drive
# everything through `python3 -I ~/eq-oracle/b1214/mqtel.py "<cmd>" ...`.
# Recipe and evidence: refactor/B1214/README.md.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ZIP_SHA=f96098644f4742a7eebc5b5d421a634e3c18bb3e46d18949500565443b3be0d9
MAIN_SHA=4619dd78631bac622b5f6423c7cb077c444788143a5c0d52f07ff187972a33ff
ACTION=${1:-}
LANE=${2:-}
[[ $LANE =~ ^[1-4]$ ]] || { echo "usage: $0 install|uninstall <lane 1-4> [zip] [plugin.dll]" >&2; exit 64; }
ORACLE=172.16.200.$((90 + LANE))
R="ssh -n -o BatchMode=yes -o ConnectTimeout=10 user@$ORACLE"
case $ACTION in
uninstall)
    $R 'pgrep -x eqgame.exe >/dev/null && { echo "eqgame.exe still running; stop it first" >&2; exit 65; }
        WINEPREFIX=$HOME/eq-oracle/b1214/wineprefix wineserver -k 2>/dev/null || :
        rm -rf "$HOME/eq-oracle/b1214"; echo removed'
    ;;
install)
    ZIP=${3:?zip}
    PLUGIN=${4:-}
    echo "$ZIP_SHA  $ZIP" | sha256sum -c - >/dev/null || { echo "zip sha256 mismatch (expected $ZIP_SHA)" >&2; exit 65; }
    STAGE=$(mktemp -d)
    trap 'rm -rf "$STAGE"' EXIT
    mkdir -p "$STAGE/x" "$STAGE/MQ2/Macros" "$STAGE/MQ2/Logs"
    unzip -q "$ZIP" -d "$STAGE/x"
    echo "$MAIN_SHA  $STAGE/x/MQ2Main.dll" | sha256sum -c - >/dev/null || { echo "MQ2Main.dll sha256 mismatch" >&2; exit 65; }
    cp "$STAGE/x/"{MacroQuest2.exe,MQ2Main.dll,MQ2ChatWnd.dll,MQ2Telnet.dll,MQ2MoveUtils.dll,MQ2MoveUtils.ini} "$STAGE/MQ2/"
    python3 -I - "$STAGE/x/MacroQuest.ini" "$STAGE/MQ2/MacroQuest.ini" <<'EOF'
import sys
t = open(sys.argv[1]).read()
head, rest = t.split('[Plugins]', 1)
tail = rest[rest.index('\n\n\n'):] if '\n\n\n' in rest else ''
head = head.replace('DebugSpewToFile=0', 'DebugSpewToFile=1')
plug = '[Plugins]\nmq2chatwnd=mq2chatwnd\nmq2telnet=mq2telnet\nmq2moveutils=mq2moveutils\n'
open(sys.argv[2], 'w', newline='\r\n').write(head + plug + tail)
EOF
    printf '[Telnet Server]\r\nPort=2323\r\nLocalOnly=0\r\nWelcome=MQ2 B1214\r\nLoginPrompt=login: \r\nPasswordPrompt=password: \r\n[Users]\r\nb1214=spike\r\n' >"$STAGE/MQ2/MQ2Telnet.ini"
    cp "$HERE"/macros/*.mac "$STAGE/MQ2/Macros/"
    [[ -z $PLUGIN ]] || cp "$PLUGIN" "$STAGE/MQ2/MQ2B1214.dll"
    $R 'set -e; [ ! -e "$HOME/eq-oracle/b1214" ] || { echo "~/eq-oracle/b1214 exists; uninstall first" >&2; exit 65; }
        pgrep -x eqgame.exe >/dev/null && { echo "a client is running on this lane" >&2; exit 65; }
        mkdir -p "$HOME/eq-oracle/b1214"
        cp -a "$HOME/eq-oracle/wineprefix" "$HOME/eq-oracle/b1214/wineprefix"
        cp -a "$HOME/eq-oracle/client" "$HOME/eq-oracle/b1214/client"'
    scp -q -r "$STAGE/MQ2" "user@$ORACLE:eq-oracle/b1214/wineprefix/drive_c/MQ2"
    scp -q "$HERE/launch.sh" "$HERE/mqtel.py" "$HERE/proof_a.sh" "$HERE/proof_cam.sh" "user@$ORACLE:eq-oracle/b1214/"
    echo "installed on lane $LANE ($ORACLE): ~/eq-oracle/b1214"
    ;;
*) echo "usage: $0 install|uninstall <lane 1-4> [zip] [plugin.dll]" >&2; exit 64 ;;
esac
