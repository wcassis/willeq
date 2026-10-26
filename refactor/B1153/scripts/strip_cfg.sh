#!/usr/bin/env bash
# B1153 (#473): strip the spellOverrides block from config/spell_effects.json
# (the noov phase of step 1) and restore it from git afterwards. The worktree
# is NFS-shared with the ThinkPad, so the edit takes effect for renders as
# soon as it is written. NO render may run while the config is stripped for a
# withov job: the batch driver serializes the phases.
set -eu
CFG=/home/user/src/willeq-wt/kb-b1153-retire/config/spell_effects.json
case ${1:-} in
    strip)
        python3 - "$CFG" <<'PY'
import json, sys
p = sys.argv[1]
cfg = json.load(open(p))
assert "spellOverrides" in cfg, "spellOverrides already absent?"
del cfg["spellOverrides"]
json.dump(cfg, open(p, "w"), indent=2)
open(p, "a").write("\n")
print("stripped:", p)
PY
        ;;
    restore)
        ( cd /home/user/src/willeq-wt/kb-b1153-retire && git checkout -- config/spell_effects.json )
        echo restored
        ;;
    *)
        echo "usage: strip_cfg.sh strip|restore" >&2; exit 2 ;;
esac
