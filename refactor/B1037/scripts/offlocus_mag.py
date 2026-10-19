import json, pathlib
from PIL import Image
REPO = pathlib.Path("/home/user/src/willeq-wt/kb-b1037-lane-goldens")
EV = pathlib.Path("/home/user/kb-evidence/kb-b1037-lane-goldens")
m = json.load(open(REPO / "tests/goldens/manifest.json"))
for lane in ("lane2", "lane3"):
    print("==", lane, "OFF-LOCUS (x86==pi but lane!=pi) magnitude vs PI golden, max-channel")
    for z in m["zones"]:
        zone = z["zone"]
        for f in z.get("designatedFrames", [0]):
            pi = REPO/"tests/goldens/pi"/zone/f"f{f}.png"
            x86 = REPO/"tests/goldens"/f"{zone}_f{f}.png"
            ln = EV/lane/zone/f"f{f}.png"
            px=list(Image.open(pi).convert("RGB").getdata())
            xx=list(Image.open(x86).convert("RGB").getdata())
            ll=list(Image.open(ln).convert("RGB").getdata())
            n=0; b1=b2=b4=b8=b16=0
            for i in range(len(px)):
                if px[i]!=xx[i] or px[i]==ll[i]: continue
                n+=1
                v=max(abs(px[i][k]-ll[i][k]) for k in range(3))
                if v<=1:b1+=1
                elif v<=3:b2+=1
                elif v<=7:b4+=1
                elif v<=15:b8+=1
                else:b16+=1
            if n:
                print(f"  {zone} f{f}: n {n}  <=1 {100*b1//n}% 2-3 {100*b2//n}% 4-7 {100*b4//n}% 8-15 {100*b8//n}% 16+ {100*b16//n}%")
            else:
                print(f"  {zone} f{f}: n 0")
