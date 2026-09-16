#!/usr/bin/env bash
# T492 Leg 0: the other host's clock minus this host's, in seconds.
#
# Measured the only way an ssh hop allows: bracket the remote `date` with a
# local one and take the midpoint, N times, then report the MEDIAN.  ssh
# connection setup is asymmetric, so a single sample is biased high by most of
# the round trip -- a ControlMaster socket removes the handshake from every
# sample after the first, which is why this opens one.
#   clock_offset.sh <user@host> [samples]
# Prints one line per sample to stderr and the median to stdout.
set -u
HOST="${1:?user@host}"; N="${2:-5}"
CTL="/tmp/.t492-clk-$$"
ssh -o BatchMode=yes -o ControlMaster=yes -o ControlPath="$CTL" -o ControlPersist=30 -N "$HOST" &
sleep 2
vals=()
for _ in $(seq 1 "$N"); do
    a=$(date +%s.%N)
    b=$(ssh -n -o BatchMode=yes -o ControlPath="$CTL" "$HOST" 'date +%s.%N' 2>/dev/null)
    c=$(date +%s.%N)
    [ -n "$b" ] || continue
    v=$(python3 -c "print(f'{$b-($a+$c)/2:.4f}')")
    echo "sample offset=${v}s rtt=$(python3 -c "print(f'{$c-$a:.4f}')")s" >&2
    vals+=("$v")
done
ssh -O exit -o ControlPath="$CTL" "$HOST" >/dev/null 2>&1
[ "${#vals[@]}" -gt 0 ] || { echo 0; exit 1; }
printf '%s\n' "${vals[@]}" | sort -g | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2}'
