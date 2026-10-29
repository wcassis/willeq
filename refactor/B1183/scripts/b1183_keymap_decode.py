#!/usr/bin/env python3
import re, sys

path = "/home/user/kb-evidence/kb-b1183-stand-state/legs/legC3b/keymap_decoded.txt"
names = {}
slots = []
for line in open(path):
    if line.startswith("KEYNAME "):
        parts = line.split()
        names[int(parts[1])] = " ".join(parts[2:])
    elif line.startswith("SLOT "):
        m = re.match(r"SLOT (\d+) k0=(\S+) k1=(\S+) raw0=(0x[0-9a-f]+) raw1=(0x[0-9a-f]+)", line)
        if m:
            slots.append((int(m.group(1)), m.group(2), m.group(3), int(m.group(4), 16), int(m.group(5), 16)))

charmap = {
    "1":0x02,"2":0x03,"3":0x04,"4":0x05,"5":0x06,"6":0x07,"7":0x08,"8":0x09,"9":0x0a,"0":0x0b,
    "-":0x0c,"=":0x0d,"q":0x10,"w":0x11,"e":0x12,"r":0x13,"t":0x14,"y":0x15,"u":0x16,"i":0x17,
    "o":0x18,"p":0x19,"[":0x1a,"]":0x1b,"a":0x1e,"s":0x1f,"d":0x20,"f":0x21,"g":0x22,"h":0x23,
    "j":0x24,"k":0x25,"l":0x26,";":0x27,"'":0x28,"`":0x29,"\\":0x2b,"z":0x2c,"x":0x2d,"c":0x2e,
    "v":0x2f,"b":0x30,"n":0x31,"m":0x32,",":0x33,".":0x34,"/":0x35,
}
namemap = {
    0x3b:"F1",0x3c:"F2",0x3d:"F3",0x3e:"F4",0x3f:"F5",0x40:"F6",0x41:"F7",0x42:"F8",0x43:"F9",
    0x44:"F10",0x57:"F11",0x58:"F12",0x0f:"TAB",0x39:"SPACE",0x45:"NUMLOCK",0x52:"NUMPAD0",
    0x4f:"NUMPAD1",0x50:"NUMPAD2",0x51:"NUMPAD3",0x4b:"NUMPAD4",0x4c:"NUMPAD5",0x4d:"NUMPAD6",
    0x47:"NUMPAD7",0x48:"NUMPAD8",0x49:"NUMPAD9",0x37:"NUMPAD*",0x4e:"NUMPAD+",0x4a:"NUMPAD-",
    0x53:"NUMPAD.",0xb5:"NUMPAD/",0x29:"TILDE",0xc9:"PGUP",0xd1:"PGDOWN",0xc7:"HOME",0xcf:"END",
    0xd2:"INSERT",0xd3:"DELETE",0xc8:"UP",0xd0:"DOWN",0xcd:"RIGHT",0xcb:"LEFT",0x2a:"LSHIFT",
    0x36:"RSHIFT",0x1d:"LCONTROL",0x9d:"RCONTROL",0x38:"LALT",0xb8:"RALT",0xdb:"LWIN",0xdc:"RWIN",
    0x9c:"NPADENTER",0x3a:"CAPSLOCK",0x0e:"BACKSPACE",0xc5:"PAUSE",0xffffffff:"NOKEY",
}
code2key = {v: k for k, v in charmap.items()}
code2key.update(namemap)

def decode(val):
    if val == 0:
        return "UNBOUND"
    if val == 0xFFFFFFFE:
        return "*NONE*"
    code = (val >> 24) & 0xFF
    low = val & 0xFFFF
    mods = []
    if val & 0x40000000: mods.append("SHIFT")
    if val & 0x20000000: mods.append("CTRL")
    if val & 0x10000000: mods.append("ALT")
    key = code2key.get(code, "code=0x%02x" % code)
    s = "+".join(mods + [key])
    if low:
        s += " low16=0x%x" % low
    return s

print("=== full decoded keymap (only bound slots) ===")
for idx, k0, k1, r0, r1 in slots:
    d0, d1 = decode(r0), decode(r1)
    if d0 == "UNBOUND" and d1 == "UNBOUND":
        continue
    name = names.get(idx, "?")
    print("%3d %-18s k0=%-28s k1=%s" % (idx, name, d0, d1))

print()
print("=== slots whose raw value has a non-zero low16 (flags other than the keycode) ===")
for idx, k0, k1, r0, r1 in slots:
    for bank, rv in (("k0", r0), ("k1", r1)):
        low = rv & 0xFFFF
        if rv not in (0, 0xFFFFFFFE, 0xFFFFFFFF) and low:
            print("SLOT %3d %-18s %s raw=0x%08x low16=0x%04x" % (idx, names.get(idx,"?"), bank, rv, low))

print()
print("=== the six pending rows' keys ===")
for idx in (12, 16, 138):
    for i, (si, k0, k1, r0, r1) in enumerate(slots):
        if si == idx:
            print("SLOT %d (%s): k0=%s k1=%s" % (idx, names.get(idx), decode(r0), decode(r1)))
