import struct, math
d = open("/home/user/kb-evidence/b894-forage-fishing/freporte_lane3.wtr", "rb").read()
n = struct.unpack("<I", d[14:18])[0]
NODE = struct.Struct("<i4f4i")
nodes = [NODE.unpack(d[18+36*i:18+36*(i+1)]) for i in range(n)]

def region(x, y, z):
    nn = 1
    for _ in range(80):
        nd = nodes[nn-1]
        l, r, s = nd[7], nd[8], nd[6]
        if l == 0 and r == 0: return s
        dist = x*nd[1] + y*nd[2] + z*nd[3] + nd[4]
        if dist == 0.0: return 0
        if dist > 0:
            if l == 0: return 0
            nn = l
        else:
            if r == 0: return 0
            nn = r
    return 0

def water_scan(sx, sy, sz, hu):
    """Server's CanFish water scan: probe (rod_x, rod_y, sz - i) for i in 0..99."""
    hd = (hu * 360) / 512 % 360
    r = math.radians(hd)
    rx = sx + 30 * math.sin(r)
    ry = sy + 30 * math.cos(r)
    surf = None
    for i in range(0, 100):
        reg = region(rx, ry, sz - i)
        if reg == 2:
            return ("LAVA", rx, ry)
        if reg in (1, 7):
            if surf is None:
                surf = i
    return ("WATER", rx, ry) if surf is not None else ("DRY", rx, ry)

z = -74.37
print("stand (0,-825), z=-74.37; rod 30u; water scan 0..99 down (server rule):")
for hu in range(0, 512, 64):
    kind, rx, ry = water_scan(0, -825, z, hu)
    hd = (hu * 360) / 512 % 360
    print(f"  heading={hu:3d} ({hd:6.2f}deg)  rod=({rx:+7.1f},{ry:+7.1f})  {kind}")

print("\nscan other stands (z=-74.37), all 8 headings, W/D only at the rod:")
for (sx, sy) in [(0,-825), (0,-815), (0,-805), (5,-820), (-5,-820), (20,-820), (0,-810)]:
    row = []
    for hu in range(0, 512, 64):
        kind, rx, ry = water_scan(sx, sy, z, hu)
        row.append(f"{hu}:{kind[0]}")
    print(f"  ({sx:+3d},{sy:+5d})  " + "  ".join(row))
