#!/usr/bin/env python3
"""B909: drive a RENDERED client's keyboard movement on the Orange Pi.

`--script` commands (/move, /jump, /turn) go through the GAME thread, which
takes its ground from the zone map. The renderer's collision rays
(PlayerCameraComponent::resolveIrrlichtCollision: checkCollisionBspFiltered,
findGroundZBspFiltered over RegionWorldTriangles) run only for KEYBOARD
movement. This makes a virtual keyboard with /dev/uinput (no python-evdev on
the Pi: raw ioctls) BEFORE the client starts. The client's evdev scan
(docker/irrlicht-drm/CIrrDeviceFB.cpp) takes the first device with KEY_A,
and the Pi's only other input device (gpio-keys) has none. Then it plays ONE
fixed key sequence, the same in every run, from --after seconds past the
client's `[memledger] point=ready` line.

Sequence (config/hotkeys.json: W forward, S back, D turn right, Space jump),
repeated --loops times:
    W 3.0 s, Space tap, W 1.0 s, S 2.0 s, D 0.6 s, Space tap, 0.5 s idle
Each key press/release is logged with its epoch to --events.

Runs as root (/dev/uinput is 0600 root); started under `sudo -n` by
scripts/pi_rendered_zone_sweep.sh --drive-keys. It keeps the device open
until the client log shows `point=shutdown` or --cap seconds pass, so the
client never sees its keyboard vanish mid-run.

Kernel ABI: the Pi runs a 32-bit kernel, whose struct input_event is two
32-bit longs + u16 type + u16 code + s32 value = 16 bytes, whatever time_t
the userspace uses.
"""
import argparse
import fcntl
import os
import struct
import sys
import time

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
EV_SYN, EV_KEY, SYN_REPORT = 0, 1, 0
KEY_W, KEY_A, KEY_S, KEY_D, KEY_SPACE = 17, 30, 31, 32, 57
EVENT = struct.Struct("<IIHHi")


def emit(fd, etype, code, value):
    now = time.time()
    os.write(fd, EVENT.pack(int(now), int((now % 1) * 1e6), etype, code, value))


def key(fd, code, down, log):
    emit(fd, EV_KEY, code, 1 if down else 0)
    emit(fd, EV_SYN, SYN_REPORT, 0)
    log.write(f"{time.time():.3f}\t{code}\t{'down' if down else 'up'}\n")
    log.flush()


def hold(fd, code, seconds, log):
    key(fd, code, True, log)
    time.sleep(seconds)
    key(fd, code, False, log)


def log_has(path, needle):
    try:
        with open(path, errors="replace") as f:
            return any(needle in line for line in f)
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--client-log", required=True)
    ap.add_argument("--events", required=True)
    ap.add_argument("--after", type=float, default=10.0, help="seconds after the ready point")
    ap.add_argument("--loops", type=int, default=5)
    ap.add_argument("--cap", type=float, default=330.0)
    a = ap.parse_args()

    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_SYN)
    for k in (KEY_W, KEY_A, KEY_S, KEY_D, KEY_SPACE):
        fcntl.ioctl(fd, UI_SET_KEYBIT, k)
    # struct uinput_user_dev: name[80], input_id (4 x u16), ff_effects_max,
    # absmax/absmin/absfuzz/absflat[64] each.
    dev = struct.pack("<80sHHHHI", b"b909-uinput-keyboard", 0x03, 0x909, 0x909, 1, 0) + b"\0" * (4 * 64 * 4)
    os.write(fd, dev)
    fcntl.ioctl(fd, UI_DEV_CREATE)
    t0 = time.time()
    with open(a.events, "w") as log:
        log.write(f"{t0:.3f}\tdevice\tcreated\n")
        log.flush()
        ready_at = None
        while time.time() - t0 < a.cap:
            if log_has(a.client_log, "[memledger] point=ready "):
                ready_at = time.time()
                break
            time.sleep(0.5)
        if ready_at is None:
            log.write(f"{time.time():.3f}\tno-ready\tgave-up\n")
        else:
            log.write(f"{ready_at:.3f}\tready\tseen\n")
            log.flush()
            time.sleep(a.after)
            log.write(f"{time.time():.3f}\tsequence\tstart\n")
            for _ in range(a.loops):
                hold(fd, KEY_W, 3.0, log)
                hold(fd, KEY_SPACE, 0.1, log)
                hold(fd, KEY_W, 1.0, log)
                hold(fd, KEY_S, 2.0, log)
                hold(fd, KEY_D, 0.6, log)
                hold(fd, KEY_SPACE, 0.1, log)
                time.sleep(0.5)
            log.write(f"{time.time():.3f}\tsequence\tend\n")
            log.flush()
        while time.time() - t0 < a.cap and not log_has(a.client_log, "point=shutdown"):
            time.sleep(1.0)
        log.write(f"{time.time():.3f}\tdevice\tdestroyed\n")
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
