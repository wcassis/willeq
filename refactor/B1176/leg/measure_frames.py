#!/usr/bin/env python3
"""B1176: camera calibration + drawn heights from the leg stills/frames.

The calibration point is B's feet: a known 3-D point (x_B, y_B, ground 0)
visible in every frame. The camera's unknowns are SOLVED from that point,
never assumed (Will's rule: measured, not inferred):

  fp          first person, camera unknowns (cz_eye, f)
              (official login default: 3.65 deg down, hfov 62.8 -- T298;
              fable: /fpcam dump in A.log is the cross-check, T470)
  chase-or    chase, camera unknowns (dist d, look-at height zL), f fixed at
              550.55 = (H/2)/tan(72/2 deg) (vfov 72, the official default)
  chase-fb    chase, camera position + look-at KNOWN from fable's /camera get
              log line; f solved from B's feet py (px is the check)

Silhouette: the figure is the connected component of the ground-distance
mask that touches the predicted feet point; top row = head, bottom row in
the central columns = feet. A row back-projects to the z of the vertical
line through the subject (chase: A is on the camera's vertical plane, so
the back-projected height IS the drawn height in world units).

usage:
  measure_frames.py diff <a> <b>
  measure_frames.py calib <frame> <mode> --a-x --a-y [--a-z] --b-x --b-y
                 [--dist 20 --gnd-x --gnd-y --gnd-w --gnd-h]
  measure_frames.py height <frame> <mode> [--sol sol.json] --a-x --a-y
                 [--b-x --b-y] --subject a|b
"""
import json
import math
import sys

import cv2
import numpy as np

W, H = 1280, 800
DEG = math.pi / 180.0


def load(path):
    img = cv2.imread(path, cv2.IMREAD_COLOR)
    if img is None:
        raise SystemExit("cannot read %s" % path)
    return img


def ground_ref(img, gx, gy, gw, gh):
    patch = img[gy:gy + gh, gx:gx + gw]
    return patch.reshape(-1, 3).mean(axis=0)


def figure_mask(img, ref, dist):
    d = np.sqrt(((img.astype(np.float32) - ref.astype(np.float32)) ** 2).sum(axis=2))
    return (d > dist).astype(np.uint8)


def component_at(mask, cx, cy, win=70):
    """The connected component of the mask overlapping the window around (cx,cy)."""
    y0, y1 = max(0, cy - win), min(mask.shape[0], cy + win)
    x0, x1 = max(0, cx - win), min(mask.shape[1], cx + win)
    if not mask[y0:y1, x0:x1].any():
        return None
    n, _labels, stats = cv2.connectedComponentsWithStats(mask, connectivity=4)[:3]
    best, best_overlap = None, 0
    for i in range(1, stats.shape[0]):
        x, y, w, h, area = stats[i]
        if area < 300:
            continue
        ox = min(x1, x + w) - max(x0, x)
        oy = min(y1, y + h) - max(y0, y)
        overlap = max(0, ox) * max(0, oy)
        if overlap > best_overlap:
            best, best_overlap = i, overlap
    if best is None:
        return None
    x, y, w, h, area = stats[best]
    return (x, y, w, h)


def solve2(func, x0, tol=0.4, iters=40):
    """Gauss-Newton for a 2-unknown, 2-residual system (finite Jacobian)."""
    x = np.array(x0, dtype=float)
    h = 1e-3
    for it in range(iters):
        r = np.array(func(x), dtype=float)
        if float(np.hypot(*r)) < tol:
            return x, it
        J = np.zeros((2, 2))
        for j in range(2):
            xp = x.copy(); xp[j] += h
            J[:, j] = (np.array(func(xp), dtype=float) - r) / h
        try:
            dx = np.linalg.solve(J, -r)
        except np.linalg.LinAlgError:
            return x, it
        if not np.all(np.isfinite(dx)) or np.hypot(*dx) > 50:
            return x, it
        x = x + dx
    return x, it


class Cam:
    """Pinhole camera in EQ world space (x,y horizontal, z up), with the
    EXACT basis vectors of include/client/graphics/first_person_camera.h
    (the T298-measured official formula, shared by fable):
      F = (sin t * cos p, cos t * cos p, sin p)
      R = (cos t, -sin t, 0)
      U = R x F = (-sin t * sin p, -cos t * sin p, cos p)
    with t = atan2(fx, fy), p = asin(fz). (B1134 anchor: observer heading
    160, caster bearing 192 -> dot(caster_dir, R) > 0 = right of the
    observer, matching the leg's measured 'right'.)"""

    def __init__(self, pos, look, f):
        self.pos = np.array(pos, dtype=float)
        fwd = np.array(look, dtype=float) - self.pos
        fwd = fwd / np.linalg.norm(fwd)
        self.fwd = fwd
        th = math.atan2(fwd[0], fwd[1])
        self.right = np.array((math.cos(th), -math.sin(th), 0.0))
        self.up = np.cross(self.right, self.fwd)
        self.f = f

    def project(self, p):
        d = np.array(p, dtype=float) - self.pos
        zf = d @ self.fwd
        if zf <= 0:
            return None
        return (W / 2 + self.f * (d @ self.right) / zf,
                H / 2 - self.f * (d @ self.up) / zf)

    def backproject_z(self, x_w, y_w, py):
        """z on the vertical line (x_w,y_w) that projects to row py."""
        v = H / 2 - py

        def res(z):
            pr = self.project((x_w, y_w, z))
            return pr[1] - py if pr else 1e9
        z0 = self.pos[2]
        for _ in range(60):
            r = res(z0)
            if abs(r) < 0.05:
                return z0
            r2 = res(z0 + 0.01)
            z0 = z0 - r * 0.01 / (r2 - r) if r2 != r else z0 - 1.0
            if z0 > 50 or z0 < -10:
                return None
        return z0


def measure_feet(img, mask, px, py, wcol=28):
    x0, x1 = max(0, int(px) - wcol), min(img.shape[1], int(px) + wcol)
    y1 = min(img.shape[0], int(py) + 90)
    y0 = max(0, int(py) - 60)
    sub = mask[y0:y1, x0:x1]
    if not sub.any():
        return None
    ys, xs = np.nonzero(sub)
    # the ground-contact row: the lowest figure row whose column is central
    bot = ys.max()
    central = ys[ys > bot - 4]
    cx = int(xs[ys > bot - 4].mean()) if len(central) else int(px)
    return (cx + x0, int(bot + y0))


def calib(frame, mode, ax, ay, az, bx, by, dist=20, gnd=None, sol_in=None):
    img = load(frame)
    gx, gy, gw, gh = gnd if gnd else (None, None, 120, 60)
    ref = ground_ref(img, gx, gy, gw, gh) if gx is not None else img[600:660, 100:220].reshape(-1, 3).mean(axis=0)
    mask = figure_mask(img, ref, dist)

    def cam_from(x):
        if mode == "fp":
            cz, f = x
            eye = (ax, ay, cz)
            # 3.65 deg down (T298), heading-free: look at (ax, ay+1, cz-0.064)
            look = (ax, ay + 1.0, cz - 1.0 * math.tan(3.65 * DEG))
            return Cam(eye, look, f)
        if mode == "chase-or":
            d, zL = x
            th = 2 * math.pi * 416 / 512.0
            p = 6.0 * DEG
            fx, fy = -math.sin(th), math.cos(th)
            look = (ax, ay, zL)
            pos = (ax - d * math.sin(th), ay + d * math.cos(th),
                   zL + d * math.sin(p))
            return Cam(pos, look, 550.55)
        if mode == "chase-fb":
            (cx, cy, cz, lx, ly, lz, f) = x
            return Cam((cx, cy, cz), (lx, ly, lz), f)
        raise SystemExit("mode")

    def func(x):
        cam = cam_from(x)
        pr = cam.project((bx, by, 0.0))
        meas = measure_feet(img, mask, pr[0], pr[1])
        if pr is None or meas is None:
            return (500.0, 500.0)
        return (meas[0] - pr[0], meas[1] - pr[1])

    if mode == "fp":
        x, it = solve2(func, (7.0, 1100.0))
        sol = {"mode": mode, "cz": x[0], "f": x[1], "iters": it,
               "a-x": ax, "a-y": ay, "b-x": bx, "b-y": by}
    elif mode == "chase-or":
        x, it = solve2(func, (20.0, 7.0))
        sol = {"mode": mode, "dist": x[0], "z_look": x[1], "f": 550.55, "iters": it,
               "a-x": ax, "a-y": ay, "b-x": bx, "b-y": by}
    elif mode == "chase-fb":
        # camera fully known (fable /camera get); f solved in closed form
        # from B's feet: v = f*(d.U)/(d.F)  ->  f = v*(d.F)/(d.U)
        if sol_in is None:
            raise SystemExit("chase-fb needs --sol-in cam.json (cx cy cz lx ly lz)")
        cin = sol_in
        cam = Cam((cin["cx"], cin["cy"], cin["cz"]),
                  (cin["lx"], cin["ly"], cin["lz"]), 1000.0)
        d = np.array([bx, by, 0.0]) - cam.pos
        dF, dU = d @ cam.fwd, d @ cam.up
        if dF <= 0 or abs(dU) < 1e-6:
            raise SystemExit("feet point behind camera or on horizon")
        pr = cam.project((bx, by, 0.0))
        meas = measure_feet(img, mask, pr[0], pr[1])
        if meas is None:
            raise SystemExit("no figure at predicted feet point")
        v_meas = H / 2 - meas[1]
        f = v_meas * dF / dU
        cam.f = f
        pr = cam.project((bx, by, 0.0))
        sol = {"mode": mode, "cx": cin["cx"], "cy": cin["cy"], "cz": cin["cz"],
               "lx": cin["lx"], "ly": cin["ly"], "lz": cin["lz"], "f": f,
               "feet_px": list(pr), "feet_meas": meas,
               "resid_px": [abs(meas[0] - pr[0]), abs(meas[1] - pr[1])],
               "a-x": ax, "a-y": ay, "b-x": bx, "b-y": by}
        print(json.dumps(sol))
        if "sol-out" in kv:
            with open(kv["sol-out"], "w") as fh:
                json.dump(sol, fh)
        return sol
    else:
        raise SystemExit("mode")
    cam = cam_from(x)
    pr = cam.project((bx, by, 0.0))
    meas = measure_feet(img, mask, pr[0], pr[1])
    sol["feet_px"] = [pr[0], pr[1]]
    sol["feet_meas"] = meas
    sol["resid_px"] = [abs(meas[0] - pr[0]), abs(meas[1] - pr[1])] if meas else None
    print(json.dumps(sol))
    return sol


def main():
    args = sys.argv[1:]
    if args[0] == "diff":
        a, b = load(args[1]), load(args[2])
        d = float(np.abs(a.astype(int) - b.astype(int)).mean())
        print("mean_abs_pixel_diff = %.4f" % d)
        return
    if args[0] == "calib":
        frame = args[1]
        mode = args[2]
        kv = {}
        i = 3
        while i < len(args):
            if args[i].startswith("--"):
                kv[args[i][2:]] = args[i + 1]
                i += 2
            else:
                i += 1
        gnd = None
        if "gnd-x" in kv:
            gnd = (int(kv["gnd-x"]), int(kv["gnd-y"]), int(kv["gnd-w"]), int(kv["gnd-h"]))
        sol_in = None
        if "sol-in" in kv:
            sol_in = json.load(open(kv["sol-in"]))
        sol = calib(frame, mode,
                    float(kv["a-x"]), float(kv["a-y"]), float(kv.get("a-z", 0)),
                    float(kv["b-x"]), float(kv["b-y"]),
                    dist=float(kv.get("dist", 20)), gnd=gnd, sol_in=sol_in)
        if "sol-out" in kv:
            with open(kv["sol-out"], "w") as fh:
                json.dump(sol, fh)
        return
    if args[0] == "height":
        frame, mode = args[1], args[2]
        kv = {}
        i = 3
        while i < len(args):
            if args[i].startswith("--"):
                kv[args[i][2:]] = args[i + 1]
                i += 2
            else:
                i += 1
        sol = json.load(open(kv["sol"]))
        img = load(frame)
        # rebuild the camera exactly as calib did
        ax, ay = float(sol.get("a-x", 0)), float(sol.get("a-y", 0))
        if mode == "fp":
            cam = Cam((ax, ay, sol["cz"]), (ax, ay + 1.0, sol["cz"] - math.tan(3.65 * DEG)), sol["f"])
        elif mode == "chase-or":
            d, zL = sol["dist"], sol["z_look"]
            th = 2 * math.pi * 416 / 512.0
            p = 6.0 * DEG
            cam = Cam((ax - d * math.sin(th), ay + d * math.cos(th), zL + d * math.sin(p)),
                      (ax, ay, zL), sol["f"])
        else:
            cam = Cam((sol["cx"], sol["cy"], sol["cz"]), (sol["lx"], sol["ly"], sol["lz"]), sol["f"])
        subject = kv.get("subject", "a")
        sx, sy = (ax, ay) if subject == "a" else (float(kv["b-x"]), float(kv["b-y"]))
        ref = img[640:700, 100:220].reshape(-1, 3).mean(axis=0)
        mask = figure_mask(img, ref, float(kv.get("dist", 20)))
        pr = cam.project((sx, sy, 3.0))
        if pr is None:
            raise SystemExit("subject out of frame")
        comp = component_at(mask, int(pr[0]), int(pr[1]))
        if comp is None:
            print(json.dumps({"error": "no figure component at predicted point", "pred": list(pr)}))
            return
        x, y, w, h = comp
        sub = mask[y:y + h, x:x + w]
        ys, xs = np.nonzero(sub)
        # feet: the lowest row in the central 40% of the component width
        cx0, cx1 = x + int(w * 0.3), x + int(w * 0.7)
        central = (xs >= cx0) & (xs <= cx1)
        feet_row = int(ys[central].max()) if central.any() else int(ys.max())
        top_row = int(ys.min())
        z_feet = cam.backproject_z(sx, sy, feet_row)
        z_top = cam.backproject_z(sx, sy, top_row)
        out = {
            "frame": frame, "mode": mode, "subject": subject,
            "component_px": [x, y, w, h],
            "head_row": top_row, "feet_row": feet_row,
            "drawn_px": feet_row - top_row,
            "z_feet": z_feet, "z_head": z_top,
            "drawn_world": (z_head - z_feet) if (z_head is not None and z_feet is not None) else None,
        }
        print(json.dumps(out))
        if "out" in kv:
            with open(kv["out"], "w") as fh:
                json.dump(out, fh)
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
