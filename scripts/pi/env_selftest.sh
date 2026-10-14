#!/usr/bin/env bash
# B1030 review -- pins scripts/pi/env.sh's lane resolution. Pure shell: no ssh,
# no device, no artefact (ctest `PiEnvLaneResolution`, label hygiene).
# The defect it guards: PI_HOST is exported, so a shell that sourced env.sh
# once (lane 1) and then ran `PI_LANE=3 bash <script>` silently got PI_LANE
# rewritten to 1 and reached the Orange Pi. A PI_LANE that disagrees with a
# listed PI_HOST must now be refused (2), never overridden.
# Exit 0 = PASS, 1 = FAIL.
set -u
ENV_SH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"
fails=0
# Each case runs in a clean environment so the caller's PI_* cannot leak in.
run() { env -i PATH="$PATH" HOME="${HOME:-/tmp}" ENV_SH="$ENV_SH" bash -c "$1" 2>&1; }
expect() {  # expect <name> <wanted output> <shell snippet>
    local got; got="$(run "$3")"
    if [ "$got" = "$2" ]; then echo "ok   $1"; else
        echo "FAIL $1"; echo "  want: $2"; echo "  got:  $got"; fails=$((fails + 1)); fi
}
P='echo rc=$? $PI_LANE $PI_HOST $PI_LANE_NAME'

expect "nothing set = lane 1"            "rc=0 1 10.0.30.192 orangepi" \
    "source \"\$ENV_SH\"; $P"
expect "PI_LANE=3 alone"                 "rc=0 3 172.16.200.202 rock4d" \
    "export PI_LANE=3; source \"\$ENV_SH\"; $P"
expect "listed PI_HOST alone picks lane" "rc=0 2 172.16.200.204 rock64" \
    "export PI_HOST=172.16.200.204; source \"\$ENV_SH\"; $P"
# B1060: lane 5 (the Orange Pi Zero) is armhf like lane 1; both resolutions must
# land on it, and a lane-1 shell must not reach it (nor it a lane-1 run).
expect "PI_LANE=5 alone"                 "rc=0 5 172.16.200.207 orangepizero armhf lima" \
    "export PI_LANE=5; source \"\$ENV_SH\"; $P \$PI_LANE_ARCH \$PI_LANE_GPU"
expect "lane 5 PI_HOST alone picks lane" "rc=0 5 172.16.200.207 orangepizero" \
    "export PI_HOST=172.16.200.207; source \"\$ENV_SH\"; $P"
expect "lane 5 vs lane-1 host refused" \
    "scripts/pi/env.sh: PI_LANE=5 but PI_HOST=10.0.30.192 is lane 1; refusing (unset PI_HOST to switch lanes)
rc=2" \
    "source \"\$ENV_SH\"; PI_LANE=5 bash -c 'source \"\$ENV_SH\"; echo reached'; echo rc=\$?"
expect "unknown lane refused"            "rc=2" \
    "PI_LANE=9 bash \"\$ENV_SH\" 2>/dev/null; echo rc=\$?"
expect "PI_LANE + its own PI_HOST"       "rc=0 3 172.16.200.202 rock4d" \
    "export PI_LANE=3 PI_HOST=172.16.200.202; source \"\$ENV_SH\"; $P"
expect "unlisted PI_HOST keeps PI_LANE"  "rc=0 2 192.0.2.7 rock64" \
    "export PI_LANE=2 PI_HOST=192.0.2.7; source \"\$ENV_SH\"; $P"
expect "unlisted PI_HOST alone = lane 1" "rc=0 1 192.0.2.7 orangepi" \
    "export PI_HOST=192.0.2.7; source \"\$ENV_SH\"; $P"
# The reviewer's reproduction, verbatim in shape: source once, then a child
# asks for lane 3 with the exported lane-1 PI_HOST still set. A sourcing
# SCRIPT must stop at the source line (exit 2), not carry on: "reached" in
# the output is the defect.
expect "reviewer case is refused" \
    "scripts/pi/env.sh: PI_LANE=3 but PI_HOST=10.0.30.192 is lane 1; refusing (unset PI_HOST to switch lanes)
rc=2" \
    "source \"\$ENV_SH\"; PI_LANE=3 bash -c 'source \"\$ENV_SH\"; echo reached \$PI_LANE \$PI_HOST'; echo rc=\$?"
expect "explicit conflict is refused" \
    "scripts/pi/env.sh: PI_LANE=2 but PI_HOST=172.16.200.202 is lane 3; refusing (unset PI_HOST to switch lanes)
rc=2" \
    "PI_LANE=2 PI_HOST=172.16.200.202 bash -c 'source \"\$ENV_SH\"; echo reached'; echo rc=\$?"
expect "executed (not sourced) exits 2" "rc=2" \
    "PI_LANE=2 PI_HOST=172.16.200.202 bash \"\$ENV_SH\" 2>/dev/null; echo rc=\$?"
expect "switching after unset PI_HOST"   "rc=0 3 172.16.200.202 rock4d" \
    "source \"\$ENV_SH\"; unset PI_HOST; PI_LANE=3 bash -c 'source \"\$ENV_SH\"; echo rc=\$? \$PI_LANE \$PI_HOST \$PI_LANE_NAME'"

if [ "$fails" -eq 0 ]; then echo "PASS: env.sh lane resolution"; exit 0; fi
echo "FAIL: $fails case(s)"; exit 1
