#!/usr/bin/env python3
"""B848: migrate live refactor/BOARD.md entries to GitLab issues.

Will's 2026-09-17 ruling (card B848, issue #4): GitLab issues are the durable
record per work item; hermes kanban cards are execution legs; MRs close issues.
Class B and P stay in BOARD.md for now -- migrate A and C first.

A LIVE entry is a bullet `- **B###** (X)` that is NOT wrapped in `~~` -- the
class parenthetical may also be written `(class X, ...)` (B504 is the live
entry in that form); both parse. Its text runs until the next bullet, a blank
line, or a section header (`#`/`---`). (BOARD.md entries never contain blank
lines -- verified by the parse itself: any blank line inside an entry range is
reported as an anomaly, not silently swallowed.) A post-parse cross-check
counts the live bullets a format-agnostic second way and FAILS on any entry
the bullet regex dropped, so a future format variant can never silently
escape the migration.

Usage:
  scripts/gitlab/board_to_issues.py --dry-run            # print the plan
  scripts/gitlab/board_to_issues.py --class A,C          # create issues
  scripts/gitlab/board_to_issues.py --class A,C --board <path>

Auth: $GITLAB_TOKEN as PRIVATE-TOKEN (never printed). The GitLab instance
cert is self-signed, so TLS verification is skipped the same way
scripts/gitlab/gql does (--k; GITLAB_CA still honoured if set).

Idempotent: an open issue whose title starts `B###:` (same number) is skipped.
"""
import argparse
import datetime
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

HOST = os.environ.get("GITLAB_URL", "https://gitlab.goldfarm.local")
PROJECT = "willeq/willeq"
BOARD_DEFAULT = "refactor/BOARD.md"

try:
    import requests
except ImportError:  # pragma: no cover - stdlib fallback
    requests = None


def http(method, url, token, payload=None):
    """One GitLab REST call. Returns (status, body-text). Self-signed cert:
    GITLAB_CA if set, else unverified (scripts/gitlab/gql does the same)."""
    headers = {"PRIVATE-TOKEN": token}
    ca = os.environ.get("GITLAB_CA")
    if requests is not None:
        r = requests.request(
            method, url, headers=headers, json=payload, timeout=30,
            verify=ca or False,
        )
        return r.status_code, r.text
    import json
    import ssl

    ctx = (
        ssl.create_default_context(cafile=ca) if ca else ssl._create_unverified_context()
    )
    data = json.dumps(payload).encode() if payload is not None else None
    if payload is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=30) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()

# The class parenthetical is written two ways in the board: `(A, ...)` and
# `(class A, ...)` (B504 is the live entry in the latter form). Both must parse
# -- a format variant is data, not a reason to silently drop an entry. The
# optional `class\s+` keeps group 2 on the class letter either way.
BULLET = re.compile(
    r"^- .*?\*\*B(\d{3,})\*\*\s*\((?:class\s+)?([A-PS])(?![A-Za-z0-9])"
)


def parse_live_entries(text):
    """Return (entries, anomalies).

    An entry's body runs from its bullet to the next entry's bullet, a section
    header (# / ---), or a blank line whose next non-blank line is NOT an
    indented continuation. Some BOARD.md entries carry later amendments after
    one or more blank lines (e.g. B814's T496 amendment) as indented
    continuation text -- those belong to the same entry and are kept.
    Anomalies: text resuming after a blank line with a NON-indented paragraph,
    or a body crossing two blank lines without a resumption in between.
    """
    lines = text.split("\n")
    entries, anomalies = [], []
    for i, l in enumerate(lines):
        m = BULLET.match(l)
        if not m:
            continue
        if l.lstrip("- ").startswith("~~"):
            continue  # struck -- closed, stays in BOARD.md
        j = i + 1
        while j < len(lines):
            nl = lines[j]
            if nl.strip() == "":
                k = j
                while k < len(lines) and lines[k].strip() == "":
                    k += 1
                if k < len(lines) and nl_k_indented_continuation(lines, k):
                    j = k  # amendment continuation of this entry
                    continue
                if k < len(lines) and lines[k].startswith("  ") \
                        and re.match(r"^\s+-\s+~~", lines[k]):
                    anomalies.append(
                        f"line {j + 1}: B{m.group(1)} entry (starts line {i + 1}) "
                        f"stops before a struck sub-bullet at line {k + 1} that its "
                        f"own text may still reference"
                    )
                break
            if nl.lstrip().startswith("- ") or nl.startswith("#") \
                    or nl.startswith("---"):
                break
            j += 1
        body = "\n".join(lines[i : j + 1])
        entries.append(
            {"num": m.group(1), "cls": m.group(2), "line": i, "body": body}
        )
    return entries, anomalies


def nl_k_indented_continuation(lines, k):
    return (
        k < len(lines)
        and lines[k].startswith("  ")
        and not BULLET.match(lines[k])
        and not lines[k].startswith(("#", "---"))
    )


def cross_check_live_counts(text, entries):
    """RED anomaly probe: the format-agnostic live-entry count must equal the
    parsed count, or FAIL.

    A live entry is a bullet that DECLARES A CLASS in the parenthetical right
    after its B-number (`(A, ...)`, `(class A, ...)`, ...). That parenthetical
    is what makes the line a classed entry rather than a bare cross-reference,
    a re-opening of a struck number that inherits its class from the struck
    original (B170/B171/B172 live lines carry no class of their own), or a
    struck entry whose parenthetical is itself `~~(B)~~` (B390). Counting the
    live bullets a second, format-agnostic way -- any unstruck bullet with a
    B-number followed by a parenthetical, whatever that parenthetical's
    contents -- and comparing the sets to the parsed set turns a silent drop
    into a hard FAIL naming the B-numbers. The parser's bullet regex knows the
    board's CURRENT class format; a future variant (a reworded paren, a new
    class letter, a stray token) that stops matching still has its paren, so
    the probe catches it before it can reach the API call that would leave that
    entry without an issue.

    Runs in every mode (dry-run and create), so a drifted board can never
    proceed. The counts are not quoted here (they drift with every new classed
    entry); the cross-check line this prints, and the `per class:` line
    `--dry-run` prints, are the measurements to cite."""
    any_entry = re.compile(r"^- .*?\*\*B(\d{3,})\*\*\s*\(")
    live = set()
    for l in text.split("\n"):
        m = any_entry.match(l)
        if m and not l.lstrip("- ").startswith("~~"):
            live.add(m.group(1))
    parsed = {e["num"] for e in entries}
    missing = sorted(live - parsed, key=lambda n: int(n))
    extra = sorted(parsed - live, key=lambda n: int(n))
    problems = []
    if missing:
        problems.append(
            "live on the board but not parsed (class-parenthetical format the "
            f"regex does not know): {', '.join('B' + n for n in missing)}"
        )
    if extra:
        problems.append(
            f"parsed but not a live classed entry: {', '.join('B' + n for n in extra)}"
        )
    if problems:
        for p in problems:
            print(f"CROSS-CHECK FAIL: {p}", file=sys.stderr)
        sys.exit(1)
    print(
        f"cross-check: {len(parsed)} parsed live entries == "
        f"{len(live)} format-agnostic live classed bullets (classes: "
        + _class_tally(entries) + ")"
    )


def _class_tally(entries):
    from collections import Counter

    return ", ".join(f"{k}={v}" for k, v in sorted(Counter(e["cls"] for e in entries).items()))


def first_sentence(body):
    """First sentence of the entry text, after the class parenthetical, <= 120
    chars. The parenthetical may carry qualifiers (e.g. `(A, *measured*,
    **fixed in T496 Leg D**)`) and one entry (B723) has a paren that is only
    closed later in the body, so the closing `)` is found by balanced-paren
    scan over the whole body, not by line or `[^)]*`. B504's parenthetical is
    terminated with a colon (`(class A, process):`); drop a leading `:` or
    `: ` after it so the `B###: ` title prefix does not double up."""
    full = " ".join(x.strip() for x in body.split("\n"))
    m = re.search(r"\*\*B\d{3,}\*\*", full)
    if not m:
        start = 0
    else:
        i = m.end()
        while i < len(full) and full[i] in " \t":
            i += 1
        if i < len(full) and full[i] == "(":
            depth = 0
            j = i
            while j < len(full):
                if full[j] == "(":
                    depth += 1
                elif full[j] == ")":
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            start = j
        else:
            start = i
    rest = full[start:].strip()
    if rest.startswith(":") or rest.startswith(": "):
        rest = rest[1:].lstrip()
    m2 = re.search(r"(?<=[.!?])\s", rest)
    s = (rest[: m2.start() + 1] if m2 else rest).strip()
    if len(s) > 120:
        s = s[:117].rsplit(" ", 1)[0] + "..."
    return s


def lane_of(body):
    """Card rule, in priority order: pi > oracle > vm > offline."""
    t = body.lower()
    if "orangepi" in t or "orange pi" in t or re.search(r"\bpi\b", t):
        return "lane::pi"
    if "oracle" in t or "official client" in t:
        return "lane::oracle"
    if "eqemu" in t or "lock" in t or "live" in t:
        return "lane::vm"
    return "lane::offline"


def build_issue(entry, today):
    body = entry["body"]
    return {
        "title": f"B{entry['num']}: {first_sentence(body)}",
        "description": (
            body
            + f"\n\n---\nSource: refactor/BOARD.md B{entry['num']} "
            f"(migrated {today})"
        ),
        "labels": ",".join(["class::" + entry["cls"], "board-migrated", lane_of(body)]),
    }


def ensure_label(http_, token, name):
    url = HOST + "/api/v4/projects/" + urllib.parse.quote(PROJECT, safe="") + "/labels"
    code, raw = http_("GET", url, token)
    if code != 200:
        sys.exit(f"label list failed: HTTP {code}\n{raw[:500]}")
    for l in _json(raw):
        if l["name"] == name:
            return
    # GitLab requires a color on label creation (HTTP 400 without it).
    code, raw = http_(
        "POST", url, token,
        {"name": name, "color": os.environ.get("BOARD_MIGRATED_LABEL_COLOR", "#4c8f80")},
    )
    if code not in (200, 201):
        sys.exit(f"cannot create label {name!r}: HTTP {code}\n{raw[:500]}")
    print(f"created label {name}")


def _json(raw):
    import json

    return json.loads(raw)


def open_issue_titles(http_, token):
    """Return {B-number: title} for every OPEN issue whose title starts B###:."""
    base = HOST + "/api/v4/projects/" + urllib.parse.quote(PROJECT, safe="") + "/issues"
    out, page = {}, 1
    while True:
        url = base + f"?state=opened&per_page=100&page={page}"
        code, raw = http_("GET", url, token)
        if code != 200:
            sys.exit(f"issue list failed: HTTP {code}\n{raw[:500]}")
        items = _json(raw)
        for i in items:
            m = re.match(r"^B(\d{3,}):", i["title"])
            if m:
                out[m.group(1)] = i["title"]
        if len(items) < 100:
            return out
        page += 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--board", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", BOARD_DEFAULT))
    ap.add_argument("--dry-run", action="store_true", help="print the plan, create nothing")
    ap.add_argument("--class", dest="classes", default="A,C",
                    help="comma list of classes to migrate (default A,C)")
    ap.add_argument("--host", default=None, help="override GITLAB_URL for the plan printout")
    args = ap.parse_args()

    cls_wanted = {c.strip().upper() for c in args.classes.split(",") if c.strip()}
    token = os.environ.get("GITLAB_TOKEN", "")
    if not args.dry_run and not token:
        sys.exit("$GITLAB_TOKEN is required")

    text = open(args.board).read()
    entries, anomalies = parse_live_entries(text)
    for a in anomalies:
        print(f"ANOMALY: {a}", file=sys.stderr)
    cross_check_live_counts(text, entries)
    live = [e for e in entries if e["cls"] in cls_wanted]
    today = datetime.date.today().isoformat()
    host = args.host or HOST

    if args.dry_run:
        from collections import Counter

        issues = [build_issue(e, today) for e in live]
        print(f"dry-run: {len(live)} live entries in classes {sorted(cls_wanted)}")
        print("  per class:", dict(Counter(e["cls"] for e in live)))
        print("  per lane:", dict(Counter(i["labels"].split(',')[2] for i in issues)))
        for e, i in zip(live, issues):
            print(f"  B{e['num']} [{e['cls']}] {i['labels']}")
            print(f"      {i['title']}")
        return

    ensure_label(http, token, "board-migrated")
    have = open_issue_titles(http, token)
    base = HOST + "/api/v4/projects/" + urllib.parse.quote(PROJECT, safe="")
    created = {}
    for e in live:
        n = e["num"]
        if n in have:
            print(f"skip B{n}: open issue exists ({have[n]!r})")
            continue
        payload = build_issue(e, today)
        code, raw = http("POST", base + "/issues", token, payload)
        if code not in (200, 201):
            sys.exit(f"create B{n} failed: HTTP {code}\n{raw[:500]}")
        iid = _json(raw)["iid"]
        created[n] = iid
        print(f"created B{n} -> issue #{iid} [{e['cls']}] {payload['title'][:80]}")
    print(f"done: {len(created)} created, {len(live) - len(created)} skipped")
    if created:
        print("issue map: " + ", ".join(f"B{n}=#{v}" for n, v in sorted(created.items())))


if __name__ == "__main__":
    main()
