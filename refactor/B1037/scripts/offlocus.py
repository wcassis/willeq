import json, pathlib
from PIL import Image
REPO = pathlib.Path("/home/user/src/willeq-wt/kb-b1037-lane-goldens")
EV = pathlib.Path("/home/user/kb-evidence/kb-b1037-lane-goldens")
m = json.load(open(REPO / "tests/goldens/manifest.json"))
print("lane | frame | diff-vs-Pi (full) | locus | in-locus diff (=A+C) | OFF-locus diff (=B-complement... actually out-of-locus diff)")
for lane in ("lane2", "lane3"):
    for z in m["zones"]:
        zone = z["zone"]
        for f in z.get("designatedFrames", [0]):
            pi = REPO/"tests/goldens/pi"/zone/f"f{f}.png"
            x86 = REPO/"tests/goldens"/f"{zone}_f{f}.png"
            ln = EV/lane/zone/f"f{f}.png"
            px=list(Image.open(pi).convert("RGB").getdata())
            xx=list(Image.open(x86).convert("RGB").getdata())
            ll=list(Image.open(ln).convert("RGB").getdata())
            diff=0; inloc=0; offloc=0
            for i in range(len(px)):
                d = px[i]!=ll[i]
                if d: diff+=1
                if px[i]!=xx[i]:
                    if d: inloc+=1
                else:
                    if d: offloc+=1
            print(f"  {lane} {zone} f{f}: full {diff}  in-locus {inloc}  off-locus {offloc}")
