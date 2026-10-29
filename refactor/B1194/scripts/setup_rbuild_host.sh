#!/usr/bin/env bash
# B1194 (#513): one-time, idempotent setup on a gitlab-runner VM so that
# `rbuild --host runner-N` can use it as a build host the way it uses the
# ThinkPad (docs/thinkpad.md, "Worker builds: scripts/hermes/rbuild (B1087)"):
#
#   1. the hermes-ext worktree mount at the SAME path (/home/user/src/willeq-wt)
#      -- the ThinkPad's own fstab line (captured 2026-10-29, see the README),
#      so every absolute path a compiler or test prints is the worker's own;
#   2. the local build tree root /home/user/willeq-builds (+ _ccache, _irrlicht);
#   3. linger for `user`, so rbuild's user-slice fallback (worker-build.slice,
#      installed by `rbuild --setup-host`) has a user manager when rbuild ssh's in.
#
# Run as root (or as `user` with passwordless sudo, if the VM grants it) -- a
# CI job on the VM runs as gitlab-runner, and `sudo -n` is NOT allowed for it
# (measured: inventory-20261029T161829Z.txt on both VMs), so the setup is a
# one-time root action per VM (see the RULING REQUEST in the README). NOTHING
# here touches the gitlab-runner's CI config; it only adds an fstab line,
# two dirs and a linger flag.
set -euo pipefail

if [ "$(id -u)" != 0 ]; then
    exec sudo -n "$0" "$@"
fi

WT_SERVER=172.16.200.100            # hermes-ext (the worker host that exports)
WT_PATH=/home/user/src/willeq-wt
BROOT=/home/user/willeq-builds

echo "== $(hostname): B1194 rbuild host setup $(date -u)"

# 1. The mount -- the ThinkPad's verbatim line (docs/thinkpad.md; the export on
#    hermes-ext carries it in /etc/exports.d/willeq-wt.exports).
if ! grep -qF "$WT_SERVER:$WT_PATH" /etc/fstab; then
    cat >> /etc/fstab <<EOF
$WT_SERVER:$WT_PATH $WT_PATH nfs vers=4.2,proto=tcp,soft,timeo=150,retrans=3,actimeo=1,noatime,nofail,_netdev,x-systemd.automount,x-systemd.idle-timeout=0 0 0
EOF
    echo "fstab: added the $WT_SERVER:$WT_PATH line"
else
    echo "fstab: line already present"
fi

mkdir -p "$WT_PATH"
chown user:user "$WT_PATH" 2>/dev/null || true

# 2. Build tree root + ccache (the rbuild layout; CCACHE_DIR=$broot/_ccache).
mkdir -p "$BROOT/_ccache" "$BROOT/_irrlicht"
chown -R user:user "$BROOT"

# 3. linger so `user`'s systemd user manager runs headless (rbuild's user
#    scopes need it; the VM's CI jobs run as gitlab-runner, not user).
loginctl enable-linger user || echo "linger: already on or loginctl refused"

systemctl daemon-reload
systemctl daemon-reload --user 2>/dev/null || true

# Mount now (nofail keeps a not-yet-reachable server from breaking boot).
if mount --all; then
    echo "mount --all: OK"
else
    echo "mount --all: FAILED (is $WT_SERVER reachable from this host? README: the firewall)"
fi
ls "$WT_PATH" 2>/dev/null | head -5 || echo "worktree not visible yet"
echo "== done: $(hostname) is rbuild-ready (pending hermes-ext ssh reachability)"
