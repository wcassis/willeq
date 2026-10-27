# B1164 (#483) dev5: read-only gdb probe of the official client's attach scale s.
# Run INSIDE gdb attached to eqgame.exe (refactor/B1164/scripts/b1164_s_watch.sh):
#   S_OUT=<log> S_TMAX=<s> gdb -p <pid> --batch -x b1164_s_gdb.py
# Two breakpoints in EQGraphicsDX9 CParticleSystem::Render (0x100605b0; edi = the
# EDD entry, ebp = the emitter instance, review/B1141/ghidra/out/EQGraphicsDX9.dis):
#   0x100609ee  right after `call 0x10039480` (the point path, +0x6C = 1): ST0 =
#               the function's return = |row 0| of the point's world matrix (point+0xA0)
#   0x10060b82  where every attach path has joined: the final s6C [esp+0x30] and
#               s19C [esp+0x48], the instance's actor +0x68 / point +0x6C / track +0x70
#               and the point's whole 4x4 matrix (point+0xA0..+0xDF).
# Nothing is written to the inferior except gdb's own int3s, which are removed at
# detach. Before inserting them the probe checks the bytes at both addresses
# against the disassembly and refuses (detaches) on a mismatch.
import gdb, os, struct, time

OUT = open(os.environ['S_OUT'], 'a', buffering=1)
TMAX = float(os.environ.get('S_TMAX', '40'))
EVERY = int(os.environ.get('S_EVERY', '50'))
# Wine relocates the DLL (lane 5: base 0x01d70000, not its preferred 0x10000000): every
# address below is the decompile's VA + D, D = the loaded base (S_BASE, from /proc/<pid>/maps) - 0x10000000.
D = int(os.environ.get('S_BASE', '0x10000000'), 16) - 0x10000000


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


# fstp dword ptr [esp+0x30] at 0x100609ee; fld dword ptr [0x101c5948] at 0x10060b82
EXPECT = {0x100609ee + D: bytes.fromhex('d95c2430'), 0x10060b82 + D: bytes.fromhex('d905')}
T0 = time.time()
seen = {}
hits = [0]


def matrix(p):
    return struct.unpack('<16f', mem(p + 0xa0, 64))


class Probe(gdb.Breakpoint):
    def __init__(self, addr):
        super().__init__('*0x%x' % addr)
        self.addr = addr

    def stop(self):
        hits[0] += 1
        now = time.time() - T0
        if now > TMAX:
            return True
        try:
            edi, ebp, esp = reg('edi'), reg('ebp'), reg('esp')
            point = u32(ebp + 0x6c)
            key = (self.addr, edi, point)
            n = seen.get(key, 0) + 1
            seen[key] = n
            if n > 3 and n % EVERY:
                return False
            name = cstr(edi, 64)
            tex = cstr(edi + 0x40, 32)
            if self.addr == 0x100609ee + D:
                st0 = float(gdb.parse_and_eval('$st0'))
                OUT.write('RET t=%.3f n=%d edd=%s tex=%s inst=0x%08x point=0x%08x s=%.6f\n'
                          % (now, n, name, tex, ebp, point, st0))
            else:
                actor, track = u32(ebp + 0x68), u32(ebp + 0x70)
                line = ('FIN t=%.3f n=%d edd=%s tex=%s f6C=%d f19C=%d inst=0x%08x actor=0x%08x point=0x%08x '
                        'track=0x%08x s6C=%.6f s19C=%.6f'
                        % (now, n, name, tex, u32(edi + 0x6c), u32(edi + 0x19c), ebp, actor, point, track,
                           f32(esp + 0x30), f32(esp + 0x48)))
                if actor:
                    line += ' actor+0x10=%.6f' % f32(actor + 0x10)
                if point:
                    m = matrix(point)
                    line += ' M=' + ','.join('%.6f' % v for v in m)
                OUT.write(line + '\n')
        except Exception as e:  # never stop the client on a read error
            OUT.write('ERR %s\n' % e)
        return False


gdb.execute('set pagination off')
gdb.execute('handle all nostop noprint pass')
gdb.execute('handle SIGINT stop print nopass')
ok = True
for a, b in EXPECT.items():
    got = mem(a, len(b))
    OUT.write('CHECK 0x%08x expect %s got %s\n' % (a, b.hex(), got.hex()))
    ok = ok and got == b
OUT.write('CHECK 0x10039480+D %s base_delta=0x%x\n' % (mem(0x10039480 + D, 6).hex(), D & 0xffffffff))
if ok:
    probes = [Probe(0x100609ee + D), Probe(0x10060b82 + D)]
    OUT.write('ARMED t=0 tmax=%.0f\n' % TMAX)
    try:
        gdb.execute('continue')
    except Exception as e:
        OUT.write('CONT %s\n' % e)
    for p in probes:
        p.delete()
    OUT.write('DONE hits=%d keys=%d t=%.3f\n' % (hits[0], len(seen), time.time() - T0))
else:
    OUT.write('REFUSED: bytes differ, no breakpoint inserted\n')
gdb.execute('detach')
OUT.write('DETACHED\n')
