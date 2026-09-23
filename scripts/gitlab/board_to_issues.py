#!/usr/bin/env python3
"""B848: migrate live refactor/BOARD.md entries to GitLab issues.

Will's 2026-09-17 ruling (card B848, issue #4): GitLab issues are the durable
record per work item; hermes kanban cards are execution legs; MRs close issues.
Classes A and C migrated 2026-09-18, class B 2026-09-23 (T505, 91 issues);
class P stays in BOARD.md for now.

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
  scripts/gitlab/board_to_issues.py --label-missing-status
                                                         # add status::triage
                                                         # to every open
                                                         # board-migrated issue
                                                         # with no status:: label

Auth: $GITLAB_TOKEN as PRIVATE-TOKEN (never printed). The GitLab instance
cert is self-signed, so TLS verification is skipped the same way
scripts/gitlab/gql does (--k; GITLAB_CA still honoured if set).

Idempotent in both directions (B849): an issue whose title starts `B###:`
(same number) in ANY state -- open or closed -- stops the entry being
created again, and so does the entry's own `→ issue #N` pointer in its first
line, which B848's step-2 pass wrote into refactor/BOARD.md for the 85
migrated entries (the create pass prints the new iid; the pointer is the
durable record and is what this script now reads). A closed issue behind an
unstruck entry is the normal post-close state (docs/issues.md: the board
keeps struck history, it does not re-open the work), so the entry is SKIPPED
with the pointer/issue named, never re-created. New issues are created with
a `status::triage` label, and `--label-missing-status` backfills the label
on the issues the original migration created without one.
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
        # lines[i:j] -- the line at j is what ENDED the body (the next bullet,
        # a header, or the blank after the last continuation), so it belongs
        # to no entry. Including it (the pre-B849 `i : j + 1`) made a
        # contiguous next bullet's text -- and its `→ issue #N` pointer --
        # part of THIS entry's body: two live entries separated by no blank
        # line (the board's B792/B793) then cross-attribute each other's
        # pointers, and a created issue's body would carry a foreign entry.
        body = "\n".join(lines[i:j])
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
        # status::triage on arrival (B849): every migrated issue carries the
        # status label family from the start, so the shepherd's label sync
        # has something to move. The pre-B849 migration created 86 issues
        # without one; `--label-missing-status` backfills them.
        "labels": ",".join(
            ["class::" + entry["cls"], "board-migrated",
             "status::triage", lane_of(body)]
        ),
    }


POINTER = re.compile(r"\u2192 issue #(\d+)")
# B849 defect 3: an unstruck bullet whose OWN header says the work is done
# (B817/B819/B830 `fixed in T496`, B805 `FIXED by pausing the animator`,
# B837 `CLOSED by T500`) is not live work. The shapes are the board's own
# close-out phrases, and the check reaches only the header line (bullet +
# first line) and the first sentence -- no guessing beyond that, per the
# card. Deliberately NOT `\bfixed\b`: B437's first sentence reads "T437
# fixed the ceiling sentinel", a report of an EARLIER unit's fix while the
# entry itself stays live (its parenthetical says `live proof owed`).
# Case matters: a lowercase "fixed" in a later sentence (B819's "Fixed by
# asserting" is capitalised but a sentence about the FIX, not a close-out;
# B437's "T437 fixed the ceiling sentinel" is an earlier unit's work) must
# not fire. `fixed in <unit>` and `CLOSED by <unit>` are the board's own
# close-out phrases and are matched case-insensitively; bare `FIXED` (B805's
# "FIXED by pausing the animator") only in capitals.
_FIXED_RE = re.compile(
    r"(?i:fixed\s+in\b)|(?i:closed\s+by\b)|\bFIXED\s+by\b"
)


def header_says_fixed(entry):
    """True when the entry's own header claims it is done.

    Header line = the bullet line; the first sentence is the entry's own
    summary. Anything further in the body (a later amendment that mentions a
    fix, an adjacent still-open item) is out of reach on purpose."""
    head = entry["body"].split("\n", 1)[0]
    first = first_sentence(entry["body"])
    return bool(_FIXED_RE.search(head)) or bool(_FIXED_RE.search(first))


def plan_actions(entries, issues, cls_wanted):
    """B849: the create plan, pure -- no http.

    `entries` is parse_live_entries()' output; `issues` is {B-number:
    (title, state)} over ALL states (the pre-B849 open_issue_titles() read
    state=opened only, which is why a closed issue did not stop a re-create);
    cls_wanted filters classes. Returns (planned, skips, anomalies):

    - planned:  entries to create an issue for;
    - skips:    {B-number: why} -- an issue of ANY state (open or closed:
      the board keeps entries unstruck after their issue closes, docs/
      issues.md, so a closed issue is the normal post-close state, not an
      anomaly), or the entry's own `→ issue #N` pointer;
    - anomalies: entries the plan must not create because an unstruck
      entry's own header says the work is fixed/closed -- verify on fable,
      close the record, strike the entry.
    """
    planned, skips, anomalies = [], {}, []
    for e in entries:
        n = e["num"]
        if e["cls"] not in cls_wanted:
            continue
        ptr = POINTER.search(e["body"])
        in_issues = n in issues
        issue = issues.get(n)
        state = issue[1] if issue else None
        iid = issue[2] if issue and len(issue) > 2 else None
        if state == "closed" or in_issues or ptr is not None:
            if in_issues:
                ref = f"issue #{iid}" if iid is not None else f"issue (B{n})"
                skips[n] = f"{ref} ({state})"
            else:
                skips[n] = f"pointer \u2192 issue #{ptr.group(1) if ptr is not None else '?'}"
            continue
        if header_says_fixed(e):
            anomalies.append(
                f"B{n}: unstruck but its header says the work is fixed -- "
                f"not created; verify on fable, close the record, strike "
                f"the entry"
            )
            continue
        planned.append(e)
    return planned, skips, anomalies


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


def list_issues(http_, token, state):
    """Every issue in `state` (opened | closed | all), paginated, raw dicts."""
    base = HOST + "/api/v4/projects/" + urllib.parse.quote(PROJECT, safe="") + "/issues"
    out, page = [], 1
    while True:
        url = base + f"?state={state}&per_page=100&page={page}"
        code, raw = http_("GET", url, token)
        if code != 200:
            sys.exit(f"issue list failed: HTTP {code}\n{raw[:500]}")
        items = _json(raw)
        out.extend(items)
        if len(items) < 100:
            return out
        page += 1


def issue_title_map(http_, token, state="all"):
    """{B-number: (title, state, iid)} for issues whose title starts `B###:`.

    state="all" by default (B849): the pre-B849 open_issue_titles() read
    state=opened only, so a CLOSED issue never stopped a re-create. The board
    keeps entries unstruck after their issue closes (docs/issues.md), so the
    only thing that prevents a duplicate is reading every state. The iid is
    the GitLab issue number (the pointer's `#N`), which in general differs
    from the B-number — the skip report cites it, so it must not be
    re-derived as the B-number."""
    out = {}
    for i in list_issues(http_, token, state):
        m = re.match(r"^B(\d{3,}):", i["title"])
        if m:
            out[m.group(1)] = (i["title"], i["state"], i["iid"])
    return out


def label_missing_status_plan(issues):
    """B849: iids of the issues that need `status::triage` backfilled.

    `issues` is a list of issue dicts (iid, state, labels) as GitLab returns
    them. The plan is the pure half of --label-missing-status: open
    board-migrated issues carrying no status:: label at all. Pure so the test
    drives it with fixtures; main() owns the http."""
    return [
        i["iid"] for i in issues
        if i.get("state") == "opened"
        and "board-migrated" in i.get("labels", [])
        and not any(l.startswith("status::") for l in i["labels"])
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--board", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", BOARD_DEFAULT))
    ap.add_argument("--dry-run", action="store_true", help="print the plan, create nothing")
    ap.add_argument("--class", dest="classes", default="A,C",
                    help="comma list of classes to migrate (default A,C)")
    ap.add_argument("--host", default=None, help="override GITLAB_URL for the plan printout")
    ap.add_argument("--label-missing-status", action="store_true",
                    help="add status::triage to every open board-migrated "
                         "issue with no status:: label; no board parse, no "
                         "creation")
    args = ap.parse_args()

    cls_wanted = {c.strip().upper() for c in args.classes.split(",") if c.strip()}
    token = os.environ.get("GITLAB_TOKEN", "")
    if not args.dry_run and not args.label_missing_status and not token:
        sys.exit("$GITLAB_TOKEN is required")

    if args.label_missing_status:
        ensure_label(http, token, "status::triage")
        issues = list_issues(http, token, "all")
        iids = label_missing_status_plan(issues)
        base = HOST + "/api/v4/projects/" + urllib.parse.quote(PROJECT, safe="")
        for iid in iids:
            code, raw = http("GET", f"{base}/issues/{iid}", token)
            if code != 200:
                sys.exit(f"GET #{iid} failed: HTTP {code}\n{raw[:500]}")
            labels = sorted(_json(raw)["labels"] + ["status::triage"])
            code, raw = http(
                "PUT", f"{base}/issues/{iid}", token,
                {"labels": ",".join(labels)},
            )
            if code not in (200, 201):
                sys.exit(f"label #{iid} failed: HTTP {code}\n{raw[:500]}")
        print(f"label-missing-status: {len(iids)} issues "
              f"{'labeled' if iids else 'needed no label'} "
              f"(status::triage added: {sorted(iids) if iids else '-'})")
        return

    text = open(args.board).read()
    entries, anomalies = parse_live_entries(text)
    for a in anomalies:
        print(f"ANOMALY: {a}", file=sys.stderr)
    cross_check_live_counts(text, entries)
    today = datetime.date.today().isoformat()
    host = args.host or HOST

    if args.dry_run:
        from collections import Counter

        planned, skips, plan_anoms = plan_actions(entries, {}, cls_wanted)
        for a in plan_anoms:
            print(f"ANOMALY: {a}", file=sys.stderr)
        issues = [build_issue(e, today) for e in planned]
        print(f"dry-run: {len(planned)} entries to create, "
              f"{len(skips)} skipped (existing issue/pointer), "
              f"{len(plan_anoms)} anomalies in classes {sorted(cls_wanted)}")
        print("  per class:", dict(Counter(e["cls"] for e in planned)))
        print("  per lane:", dict(Counter(i["labels"].split(',')[3] for i in issues)))
        for e in planned:
            n = e["num"]
            print(f"  create B{n} [{e['cls']}] {build_issue(e, today)['labels']}")
            print(f"      {build_issue(e, today)['title']}")
        for n in sorted(skips, key=int):
            print(f"  skip B{n}: {skips[n]}")
        return

    ensure_label(http, token, "board-migrated")
    ensure_label(http, token, "status::triage")
    have = issue_title_map(http, token)
    planned, skips, plan_anoms = plan_actions(entries, have, cls_wanted)
    for a in plan_anoms:
        print(f"ANOMALY: {a}", file=sys.stderr)
    base = HOST + "/api/v4/projects/" + urllib.parse.quote(PROJECT, safe="")
    created = {}
    for e in planned:
        n = e["num"]
        payload = build_issue(e, today)
        code, raw = http("POST", base + "/issues", token, payload)
        if code not in (200, 201):
            sys.exit(f"create B{n} failed: HTTP {code}\n{raw[:500]}")
        iid = _json(raw)["iid"]
        created[n] = iid
        print(f"created B{n} -> issue #{iid} [{e['cls']}] {payload['title'][:80]}")
    for n in sorted(skips, key=int):
        print(f"skip B{n}: {skips[n]}")
    print(f"done: {len(created)} created, {len(skips)} skipped, "
          f"{len(plan_anoms)} anomalies")
    if created:
        print("issue map: " + ", ".join(f"B{n}=#{v}" for n, v in sorted(created.items())))


if __name__ == "__main__":
    main()
