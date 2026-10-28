# B1180 (#499): read-only gdb probe of the official client's sprite half-width
# factors in EQGraphicsDX9 CParticleSystem::Render (FUN_100605b0).
#
# The screen-aligned (mode == 1, D0x70) branch builds the half-width at
# 0x1006335a..0x1006336f:
#   halfwidth = [esp+0xc8] * [eax+0x18] * [esp+0x1c] * [esp+0x30] * [esi+0x6c] * 0.5
#   (0.5 at 0x101c594c; [esp+0x30] = s6C the attach scale, B1164;
#    [esi+0x6c] = the particle width; [eax] = [esp+0x514] = the matrix,
#    [eax+0x18] = its 0x18 element.)
# UNTRACED before this: [esp+0xc8] (A), [esp+0x1c] (B), the matrix.
#
# Break at 0x100632f0 (`cmp edx,1` = 83 fa 01): edx = the sprite mode (D0x70),
# [esp+0x18] = s6C*D16C, and every factor is live. We read only (no write to
# the inferior except gdb's own int3s, removed at detach).
#
# Run by b1180_hw_watch.sh:  sudo -n env H_OUT=.. H_TMAX=.. gdb -p <pid> -x this
import gdb, os, struct, time

OUT_PATH = os.environ['H_OUT']
TMAX = float(os.environ.get('H_TMAX', '60'))
EVERY = int(os.environ.get('H_EVERY', '5'))
OUT = open(OUT_PATH, 'a', buffering=1)


def W(m):
    OUT.write(m + '\n')


# --- base detection: the lowest EQGraphicsDX9.dll mapping in the inferior's
# --- maps. (Wine loads it at 0x01d70000 on lane 5, not the 0x10000000 imagebase.)
# The pid is passed in from the watch via H_PID (gdb's inferior.pid is a
# property in some builds, so .pid() raises 'int' object is not callable).
def detect_base():
    pid = os.environ.get('H_PID')
    if not pid:
        try:
            inf = gdb.selected_inferior()
            p = getattr(inf, 'pid', None)
            pid = p() if callable(p) else p
        except Exception as e:
            W('BASE pid err %r' % e)
    if not pid:
        return None
    try:
        lo = None
        for line in open('/proc/%s/maps' % pid):
            if 'EQGraphicsDX9.dll' not in line:
                continue
            a = int(line.split('-')[0], 16)
            if lo is None or a < lo:
                lo = a
        if lo is not None:
            return lo
    except Exception as e:
        W('BASE detect err %r' % e)
    env = os.environ.get('H_BASE')
    return int(env, 16) if env else None


BASE = detect_base()
if BASE is None:
    W('REFUSED: no EQGraphicsDX9 base; no H_BASE given')
    gdb.execute('detach')
    raise SystemExit(0)
D = BASE - 0x10000000
W('BASE base=0x%x delta=0x%x' % (BASE, D & 0xffffffff))


def mem(a, n):
    return bytes(gdb.selected_inferior().read_memory(a, n))


def u32(a):
    return struct.unpack('<I', mem(a, 4))[0]


def f32(a):
    return struct.unpack('<f', mem(a, 4))[0]


def cstr(a, n):
    return mem(a, n).split(b'\0')[0].decode('latin1')


def reg(r):
    return int(gdb.parse_and_eval('$' + r)) & 0xffffffff


# cmp edx,1 at 0x100632f0 (83 fa 01); fld [esp+0xc8] at 0x1006335a (d9 84 24 c8 00 00 00)
EXPECT = {0x100632f0: bytes.fromhex('83fa01'),
          0x1006335a: bytes.fromhex('d98424c8000000')}
ok = True
for off, want in EXPECT.items():
    got = mem(off + D, len(want))
    W('CHECK 0x%08x expect %s got %s' % (off, want.hex(), got.hex()))
    ok = ok and (got == want)
if not ok:
    W('REFUSED: bytes differ at base 0x%x; no breakpoint inserted' % BASE)
    gdb.execute('detach')
    raise SystemExit(0)

T0 = time.time()
seen = {}
hits = [0]


class Probe(gdb.Breakpoint):
    def __init__(self, addr):
        super().__init__('*0x%x' % addr)
        self.addr = addr

    def stop(self):
        hits[0] += 1
        if time.time() - T0 > TMAX:
            return True
        try:
            mode = reg('edx')
            edi = reg('edi')
            name = cstr(edi, 40)
            key = (mode, name)
            n = seen.get(key, 0) + 1
            seen[key] = n
            if len(seen) > 400:
                return True
            if n > 4 and (n % EVERY):
                return False
            ebp, esi, esp = reg('ebp'), reg('esi'), reg('esp')
            mp = u32(esp + 0x514)
            M = struct.unpack('<16f', mem(mp, 64)) if mp else [float('nan')] * 16
            A = f32(esp + 0xc8)
            B = f32(esp + 0x1c)
            C4 = f32(esp + 0xc4)   # Branch-B (billboard) height-side factor
            D8 = f32(esp + 0xd8)   # Branch-B (billboard) width-side factor
            s6C = f32(esp + 0x30)
            s19C = f32(esp + 0x48)
            s18 = f32(esp + 0x18)
            invD = f32(esp + 0x2c)
            w = f32(esi + 0x6c)
            h = f32(esi + 0x70)
            hw = A * M[6] * B * s6C * w * 0.5
            W('HW t=%.3f n=%d mode=%d edd=%s f6C=%d f19C=%d s6C=%.6f s19C=%.6f s18=%.6f '
              'w=%.6f h=%.6f A(0xc8)=%.6f B(0x1c)=%.6f C4(0xc4)=%.6f D8(0xd8)=%.6f '
              'invD(0x2c)=%.6f M00=%.6f M018=%.6f M010=%.6f M04=%.6f hw=%.6f'
              % (time.time() - T0, n, mode, name, u32(edi + 0x6c), u32(edi + 0x19c),
                 s6C, s19C, s18, w, h, A, B, C4, D8, invD, M[0], M[6], M[4], M[1], hw))
        except Exception as e:
            W('ERR %r' % e)
        return False


gdb.execute('set pagination off')
gdb.execute('handle all nostop noprint pass')
gdb.execute('handle SIGINT stop print nopass')
probe = Probe(0x100632f0 + D)
probe.silent = True
W('ARMED t=0 tmax=%.0f every=%d' % (TMAX, EVERY))
try:
    gdb.execute('continue')   # blocks until a stop (TMAX, too many keys, or process exit)
except Exception as e:
    W('CONT %r' % e)
probe.delete()
W('DONE hits=%d keys=%d t=%.3f' % (hits[0], len(seen), time.time()))
gdb.execute('detach')
W('DETACHED')
