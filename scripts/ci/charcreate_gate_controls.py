"""B888-CC4 (GitLab #204) — the gate's control rects and canonical state list.

The rects are QUOTED from the layout headers (char_select_layout.h and
char_create_layout.h), the same numbers those headers compute at the gate's
render size 1280x800 (the oracle crop space). GATE_STATES is the gate's
canonical state list and MUST equal tools/hwcaps/charcreate_capture.cpp's
kStates and the README's table of states — the hygiene test
(test_charcreate_gate_state_list) checks all three agree, so a new state that
is not in all three is a red test, not a silent drift.

A rect is (x0, y0, x1, y1) in the 1280x800 image space. The score is the
edge density inside the rect (a control that moved, shrank, or vanished drops
below the bar); the label check OCRs the rect and requires the client's own
text to be present (the state machine resolved the eqstr, not "eqstr <id>").
"""
from pathlib import Path

# The gate's canonical state list (file stem of each capture PNG).
GATE_STATES = [
    "select_empty",
    "select_one",
    "select_delete_prompt",
    "create_initial",
    "create_after_gender",
    "create_after_race",
    "create_after_class",
    "create_stats_part",
    "create_stats_all",
    "page1_initial",
    "page1_deity_city",
    "page1_getname",
    "page1_name_rejected",
    "facepick_open",
]

# --- the shared top controls (creation pages), char_create_layout.h @1280x800 ---
_TITLE = (534, 0, 746, 86)       # titleRect: (1280-212)/2=534
_TITLE_LABEL = (537, 24, 737, 39)  # child(title,-1,20,200,15): inset 4
_HELP = (610, 59, 668, 77)       # child(title,73,55,54,18)
_BACK = (1123, 8, 1197, 40)      # backButtonRect
_NEXT = (1211, 8, 1275, 40)      # nextButtonRect

# page 0: screen0a (0,160) 210x480 ; screen0b (1112,202) 168x395 (inset 4)
_AX, _AY = 0, 160
_FACE = (6, 209, 70, 233)        # child(0a,2,45,64,24)
_MALE = (74, 209, 138, 233)      # child(0a,70,45,64,24)
_FEMALE = (138, 209, 202, 233)   # child(0a,134,45,64,24)
_RACE = []                        # race slots: child(0a,2,85+24n,96,24)
_CLASS = []
for _n in range(15):
    _RACE.append((6, _AY + 4 + 85 + 24 * _n, 102, _AY + 4 + 109 + 24 * _n))
for _n in range(16):
    _CLASS.append((110, _AY + 4 + 85 + 24 * _n, 206, _AY + 4 + 109 + 24 * _n))
_POINTS = (1119, 251, 1220, 289)  # pointsLeftButtonRect
_PTS_DISPLAY = (1223, 251, 1255, 283)
_STAT_BTN = []   # statButtonRect(n)
_STAT_UP = []    # statUpRect(n)
_STAT_DOWN = []  # statDownRect(n)
_STAT_DISP = []  # statDisplayRect(n)
_SX, _SY, _SW, _SH = 1112, 202, 168, 395
_cw = _SW - 8
_ox, _oy = _SX + 4, _SY + 4
def _ar(l, t, r, b):
    return (_ox + _cw - l, _oy + t, _ox + _cw - r, _oy + b)
for _n in range(7):
    _STAT_BTN.append(_ar(157, 96 + 40 * _n, 56, 134 + 40 * _n))
    _STAT_UP.append(_ar(19, 96 + 40 * _n, 5, 112 + 40 * _n))
    _STAT_DOWN.append(_ar(19, 114 + 40 * _n, 5, 130 + 40 * _n))
    _STAT_DISP.append(_ar(54, 96 + 40 * _n, 22, 129 + 40 * _n))

# page 1: screen1a (0,0) 224x108 ; screen1b (0,185) 218x430 ; screen1c (1152,185) 128x389
_NAME_LABEL = (62, 9, 166, 34)   # child(1a,54,5,100,25)
_NAME_INPUT = (10, 36, 134, 56)  # child(1a,6,32,120,20)
_GETNAME = (139, 36, 212, 58)    # child(1a,131,32,73,22)
_DEITY_TITLE = (61, 199, 165, 224)  # child(1b,53,10,100,25)
_DEITY = []                      # child(1b,5,42+22n,195,22); 1b at (0,185)
for _n in range(17):
    _DEITY.append((9, 185 + 4 + 42 + 22 * _n, 204, 185 + 4 + 64 + 22 * _n))
_CITY_TITLE = (1166, 197, 1270, 222)  # child(1c,10,8,100,25)
_CITY = []                       # child(1c,8,42+22n,105,22)
for _n in range(15):
    _CITY.append((1164, 185 + 4 + 42 + 22 * _n, 1269, 185 + 4 + 64 + 22 * _n))

# character select: char_select_layout.h @1280x800, the XML-derived rects.
# clientOrigin(sh) = (14, (sh-484)/2+8) = (14, 266) at sh=800. The gate scores
# willeq's render against THESE (the canonical XML rects); where the willeq
# render (or the official-client reference) places the window differently, the
# per-control IoU against the XML rect drops and the README names it as a
# finding (a mismatch is a finding, not a reason to loosen the bar).
_CSX, _CSY = 14, 266
def _cs(x, y, w, h):
    return (_CSX + x, _CSY + y, _CSX + x + w, _CSY + y + h)
_CS_TITLE = _cs(17, 20, 182, 15)
_CS_EXPLORE = _cs(2, 60, 48, 32)
_CS_DELETE = _cs(80, 60, 58, 32)
_CS_ROTATE = _cs(168, 60, 48, 32)
_CS_CHAR = [_cs(5, 105 + 35 * n, 210, 32) for n in range(8)]
_CS_QUIT = _cs(5, 429, 100, 32)
_CS_ENTER = _cs(117, 429, 100, 32)


def state_controls(name):
    """ALL the control rects laid out for one gate state, by control name.

    Not every one of these is REQUIRED to be present: empty slots (the char
    select's empty rows, page-1's un-offered deity/city rows) and willeq's
    deliberately-undrawn controls (Quit / Enter — the layout header marks the
    tutorial pair OUT) have no pixels by design. Which of them a state must
    actually render is REQUIRED_CONTROLS; the rest are scored and reported
    (so a layout that moves or drops one is still visible in the ledger) but
    never fail the gate."""
    c = {}
    if name.startswith("select_"):
        c["title"] = _CS_TITLE
        c["explore"] = _CS_EXPLORE
        c["delete"] = _CS_DELETE
        c["rotate"] = _CS_ROTATE
        for i in range(8):
            c[f"char{i}"] = _CS_CHAR[i]
        c["quit"] = _CS_QUIT
        c["enter"] = _CS_ENTER
        if name == "select_delete_prompt":
            # the confirm modal sits centred over the 3D area (measured on
            # the willeq frame: the box is ~(505,300)-(780,500), Yes/No at
            # y~485). Its Yes/No strip is the modal's identity.
            c["modal"] = (505, 300, 785, 505)
    elif name == "facepick_open":
        # renderCharCreateScreen draws ONLY the FacePick modal when
        # facePick.isOpen (face_pick_panel.cpp returns before the page chrome) --
        # so the page's title/next/back are legitimately ABSENT here (scored,
        # not required). The modal: FacePickLayout @1280x800, window (0,292)
        # 175x215, child origin +4/+16.
        c["fp_title"] = (4, 308, 179, 324)
        c["fp_random"] = (54, 452, 118, 472)
        c["fp_accept"] = (19, 478, 83, 498)
        c["fp_cancel"] = (93, 478, 157, 498)
        for i in range(7):
            c[f"fp_down{i}"] = (19, 312 + 20 * i, 41, 324 + 20 * i)
            c[f"fp_up{i}"] = (137, 312 + 20 * i, 159, 324 + 20 * i)
    elif name.startswith("page1_"):
        c["title"] = _TITLE
        c["next"] = _NEXT
        c["back"] = _BACK
        c["name_label"] = _NAME_LABEL
        c["name_input"] = _NAME_INPUT
        c["get_name"] = _GETNAME
        c["deity_title"] = _DEITY_TITLE
        for i in range(len(_DEITY)):
            c[f"deity{i}"] = _DEITY[i]
        c["city_title"] = _CITY_TITLE
        for i in range(len(_CITY)):
            c[f"city{i}"] = _CITY[i]
        if name == "page1_name_rejected":
            c["reject_dialog"] = (560, 320, 820, 560)
    else:  # the page-0 creation states
        c["title"] = _TITLE
        c["next"] = _NEXT
        c["back"] = _BACK
        c["help"] = _HELP
        c["face"] = _FACE
        c["male"] = _MALE
        c["female"] = _FEMALE
        c["points"] = _POINTS
        for i in range(len(_STAT_BTN)):
            c[f"stat_btn{i}"] = _STAT_BTN[i]
            c[f"stat_up{i}"] = _STAT_UP[i]
            c[f"stat_down{i}"] = _STAT_DOWN[i]
    return c


def label_rect(name, ctrl):
    """The OCR region for a labelled control (defaults to the control's rect);
    None when the control is not laid out for this state (skip it)."""
    return state_controls(name).get(ctrl)


# Which of state_controls' rects MUST actually render for each gate state.
# Everything else is scored + reported in the ledger (so a layout that moves
# or drops a laid-out control is still visible) but never fails the gate:
# empty char slots, the un-offered deity/city rows, and willeq's deliberately
# undrawn Quit / Enter (the layout header marks the tutorial pair OUT) have no
# pixels BY DESIGN. A state whose REQUIRED set is missing a control is a real
# regression (the screen drew less than its state machine says it holds).
REQUIRED_CONTROLS = {
    "select_empty": ["title", "explore", "delete", "rotate", "char0", "char7"],
    "select_one": ["title", "explore", "delete", "rotate", "char0", "char7"],
    "select_delete_prompt": ["title", "delete", "char0", "modal"],
    # page 0: every stat row + its up/down glyphs render (the points-left and
    # all seven stat rows are always drawn); face/male/female always render.
    "create_initial":
        ["title", "next", "back", "help", "face", "male", "female", "points"]
        + [f"stat_btn{i}" for i in range(7)]
        + [f"stat_up{i}" for i in range(7)]
        + [f"stat_down{i}" for i in range(7)],
}
# the page-0 creation states all render the same fixed control set
for _s in ("create_after_gender", "create_after_race", "create_after_class",
           "create_stats_part", "create_stats_all"):
    REQUIRED_CONTROLS[_s] = list(REQUIRED_CONTROLS["create_initial"])
# page 1: the fixed chrome + the name controls + deity title; the offered
# deity rows (deity0..) render, the un-offered ones (deity14 etc) do not.
REQUIRED_CONTROLS["page1_initial"] = [
    "title", "next", "back", "name_label", "name_input", "get_name", "deity_title"]
REQUIRED_CONTROLS["page1_deity_city"] = list(REQUIRED_CONTROLS["page1_initial"])
REQUIRED_CONTROLS["page1_getname"] = list(REQUIRED_CONTROLS["page1_initial"])
REQUIRED_CONTROLS["page1_name_rejected"] = list(REQUIRED_CONTROLS["page1_initial"])
REQUIRED_CONTROLS["facepick_open"] = ["fp_title", "fp_random", "fp_accept", "fp_cancel"]


def required_controls(name):
    """The subset of state_controls(name) a state must actually render."""
    return REQUIRED_CONTROLS.get(name, [])



def _eqstr(id_, text):
    return text  # the client's own text (resolved from willeq.db)


# The labels each state must render, in its OCR rect. A control's label is the
# substring that MUST appear when OCR reads its rect. These are the client's
# own strings (the eqstr the state machine resolved); a miss means the state
# machine did not resolve the string (it drew "eqstr <id>") — a real defect.
DEFAULT_LABELS = {
    "select_empty":   {"title": "character select"},
    "select_one":     {"title": "character select", "char0": "testcaster"},
    "select_delete_prompt": {"title": "character select"},
    "create_initial": {"title": "character creation"},
    "create_after_gender": {"title": "character creation"},
    "create_after_race": {"title": "character creation"},
    "create_after_class": {"title": "character creation"},
    "create_stats_part": {"title": "character creation"},
    "create_stats_all": {"title": "character creation"},
    "page1_initial": {"title": "character creation"},
    "page1_deity_city": {"title": "character creation"},
    "page1_getname": {"title": "character creation", "name_input": "orangecaster"},
    "page1_name_rejected": {"title": "character creation"},
    "facepick_open": {"fp_title": "choose your appearance"},
}


def load_labels(path, fallback):
    """Read a labels.tsv (state<TAB>control<TAB>label, '#' comments) and merge
    it over the fallback. A missing file returns the fallback unchanged."""
    labels = {k: dict(v) for k, v in fallback.items()}
    if not path.exists():
        return labels
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            continue
        state, ctrl, label = parts
        labels.setdefault(state, {})[ctrl] = label
    return labels
