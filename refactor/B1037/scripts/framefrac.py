import json, pathlib
from PIL import Image
REPO = pathlib.Path("/home/user/src/willeq-wt/kb-b1037-lane-goldens")
EV = pathlib.Path("/home/user/kb-evidence/kb-b1037-lane-goldens")
m = json.load(open(REPO / "tests/goldens/manifest.json"))
for lane in ("lane2","lane3"):
    print("==", lane)
    for z in m["zones"]:
        zone=z["zone"]
        for f in z.get("designatedFrames",[0]):
            pi=REPO/"tests/goldens/pi"/zone/f"f{f}.png"
            ln=EV/lane/zone/f"f{f}.png"
            a=Image.open(pi).convert("RGB"); b=Image.open(ln).convert("RGB")
            da=list(a.getdata()); db=list(b.getdata())
            tot=len(da); d1=0; d=0
            for i in range(tot):
                mx=max(abs(db[i][c]-da[i][c]) for c in range(3))
                if mx: d+=1
                if mx==1: d1+=1
            print(f"  {zone} f{f}: {a.size} diff {d} ({100*d//tot}%)  1LSB-of-diff {100*d1//max(1,d)}%  1LSB-of-frame {100*d1//tot}%")
