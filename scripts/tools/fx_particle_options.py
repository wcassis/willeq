#!/usr/bin/env python3
"""B1165 (#484): the FX gate's particle-options pin.

usage: fx_particle_options.py <frames_dir> [--ini <eqclient.ini>] [--particle-options]

tools/model_viewer --spell-test --plan writes <frames_dir>/particle_options.tsv: the
Options > Particles > Spell page values (+ ShowSpellEffects) the run drew with. A gate
script runs this before it measures the frames: it PRINTS the four Spell-page values and
ShowSpellEffects, compares them with the oracle capture's eqclient.ini ([Defaults]
SpellParticleDensity / SpellParticleOpacity / SpellParticleNearClipPlane /
SpellParticleCastFilter / ShowSpellEffects; default: the B1159 rig's
eqclient-after.ini, which every FX gate capture shares) and exits 65 when they differ,
unless --particle-options is given (a deliberate non-capture run). Exit 66 when the
frames carry no particle_options.tsv (a pre-B1165 harness: the values are unknown).
"""
import sys

DEFAULT_INI = "/mnt/projects/fable/review/B1159/worker/oracle/m_b1159_rig/eqclient-after.ini"
KEYS = ("SpellParticleDensity", "SpellParticleOpacity", "SpellParticleNearClipPlane",
        "SpellParticleCastFilter", "ShowSpellEffects")


def read_ini(path):
    vals, section = {}, ""
    with open(path, errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
            elif "=" in line and section == "Defaults":
                k, v = line.split("=", 1)
                if k in KEYS:
                    vals[k] = float(v)
    return vals


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    allow = "--particle-options" in argv
    ini = DEFAULT_INI
    if "--ini" in argv:
        ini = argv[argv.index("--ini") + 1]
        args = [a for a in args if a != ini]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        rows = [l.rstrip("\n").split("\t") for l in open(args[0] + "/particle_options.tsv")]
    except OSError:
        print(f"particle options: {args[0]}/particle_options.tsv missing (pre-B1165 harness)", file=sys.stderr)
        return 66
    ran = {k: float(v) for k, v in rows[1:]}
    want = read_ini(ini)
    print("particle options: " + " ".join(f"{k}={ran.get(k, float('nan')):g}" for k in KEYS))
    print("oracle capture:   " + " ".join(f"{k}={want.get(k, float('nan')):g}" for k in KEYS) + f"  ({ini})")
    diff = [k for k in KEYS if ran.get(k) != want.get(k)]
    if diff and not allow:
        print("particle options differ from the oracle capture's ini: " + ", ".join(diff) +
              " -- pass --particle-options to measure anyway", file=sys.stderr)
        return 65
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
