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

print("sanity: shark (-410,-1151,-99) ->", region(-410,-1151,-99.2), "(want 1)")
print("sanity: city (0,0,0) ->", region(0,0,0), "(want 0)")
print("sanity: stand (0,-825,-74) ->", region(0,-825,-74.37), "(land? want 0)")
print("sanity: rod h180 (-843,24,-74) ->", region(-842.9,24.1,-74.37))
print("sanity: rod h0   (0,-855,-74) ->", region(0,-855,-74.37))

# water column extent at the rod tips and around
for (x,y) in [(0,-825), (-843,24), (0,-855), (-810,0), (-830,20), (-410,-1151)]:
    surf = bot = None
    for i in range(0,100):
        r = region(x,y,-74.37 - i)
        if r in (1,7):
            if surf is None: surf = i
            bot = i
    # also probe upward from the stand z
    up = None
    for i in range(0,40):
        r = region(x,y,-74.37 + i)
        if r in (1,7): up = i
    print(f"col ({x:+6.0f},{y:+6.0f}): down(surf..bot)={surf}..{bot}  up={up}")
