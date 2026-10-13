#!/usr/bin/env python3 -I
# Read/write Exynos5420 CMU_TOP registers via /dev/mem. usage: cmu.py read | cmu.py set_user_g3d_sw
import mmap, os, sys, struct
BASE = 0x10010000
REGS = {"SRC_TOP2":0x10208,"SRC_TOP5":0x10214,"SRC_TOP6":0x10218,"SRC_TOP11":0x10284,"SRC_TOP12":0x10288,
        "DIV_TOP2":0x10508,"VPLL_LOCK":0x10080,"VPLL_CON0":0x10170,"GATE_IP_G3D":0x10930}
fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(fd, 0x20000, offset=BASE)
def rd(off): return struct.unpack("<I", m[off:off+4])[0]
def wr(off, v): m[off:off+4] = struct.pack("<I", v)
def show():
    for n,o in REGS.items(): print(f"{n:12s} @0x{BASE+o:08x} = 0x{rd(o):08x}")
    s5, s12, s2, s6, d2, vc = rd(0x10214), rd(0x10288), rd(0x10208), rd(0x10218), rd(0x10508), rd(0x10170)
    print(f"mout_user_aclk_g3d (SRC_TOP5[16]) = {(s5>>16)&1}  (0=fin_pll 24MHz, 1=mout_sw_aclk_g3d)")
    print(f"mout_sw_aclk_g3d   (SRC_TOP12[16]) = {(s12>>16)&1} (0=dout_aclk_g3d, 1=spll)")
    print(f"mout_aclk_g3d      (SRC_TOP2[16]) = {(s2>>16)&1}  (0=vpll, 1=dpll)")
    print(f"mout_sclk_vpll     (SRC_TOP6[4]) = {(s6>>4)&1}  (0=fin_pll, 1=fout_vpll)")
    print(f"dout_aclk_g3d div  (DIV_TOP2[18:16]) = {(d2>>16)&7} (+1)")
    print(f"VPLL_CON0 enable={vc>>31&1} locked={(vc>>29)&1} M={(vc>>16)&0x3ff} P={(vc>>8)&0x3f} S={vc&7}")
if len(sys.argv)>1 and sys.argv[1]=="set_user_g3d_sw":
    v = rd(0x10214); wr(0x10214, v | (1<<16)); print("wrote SRC_TOP5 bit16=1")
show()
