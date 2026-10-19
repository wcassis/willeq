import re, json, pathlib
from PIL import Image
REPO = pathlib.Path("/home/user/src/willeq-wt/kb-b1037-lane-goldens")
EV = pathlib.Path("/home/user/kb-evidence/kb-b1037-lane-goldens")
m = json.load(open(REPO / "tests/goldens/manifest.json"))
for lane in ("lane2", "lane3"):
    print("==", lane, "  (over FULL locus; |lane-x86| max-channel)")
    for z in m["zones"]:
            zone = z["zone"]
            for f in z.get("designatedFrames", [0]):
                pi = REPO/"tests/goldens/pi"/zone/f"f{f}.png"
                x86 = REPO/"tests/goldens"/f"{zone}_f{f}.png"
                ln = EV/lane/zone/f"f{f}.png"
                px=list(Image.open(pi).convert("RGB").getdata())
                xx=list(Image.open(x86).convert("RGB").getdata())
                ll=list(Image.open(ln).convert("RGB").getdata())
                tot=0; b01=b03=b07=0
                for i in range(len(px)):
                    if px[i]==xx[i]: continue
                    tot+=1
                    v=max(abs(xx[i][k]-ll[i][k]) for k in range(3))
                    if v<=1:b01+=1
                    if v<=3:b03+=1
                    if v<=7:b07+=1
                print(f"  {zone} f{f}: locus {tot}  <=1LSB {100*b01//tot}%  <=3LSB {100*b03//tot}%  <=7LSB {100*b07//tot}%")
