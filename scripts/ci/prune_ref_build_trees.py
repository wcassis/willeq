#!/usr/bin/env python3
"""Prune the per-ref warm build trees on the CI runner (B1012, issue #333).

Since B1012 each pipeline builds into its OWN tree under ``build/ref/<slug>/``
and ``build-gles2/ref/<slug>/`` (``$CI_COMMIT_REF_SLUG``), so two branches'
pipelines can no longer pollute each other's warm tree (the 2026-10-08 case:
MR !119's ``test`` stage ran over binaries MR !118's ``build`` stage had just
written into the one shared ``build/``). The price is one warm tree per ref
with pipeline history, on a runner with no expiry mechanism of its own -- this
script IS that mechanism. The ``build`` and ``gate`` jobs stamp each tree they
use with ``.willeq_ref`` (the full ref name + the UTC time of the job); this
script decides per tree:

  * ``.willeq_ref`` names a ref that no longer exists on the remote -> DELETE
    (the branch was merged or deleted; its tree can never be reused);
  * the stamp is older than ``--max-age-days`` -> DELETE (a live branch nobody
    has pushed to in that long will pay one cold build when it comes back);
  * a tree with NO stamp -> DELETE (it was written by nothing this layout
    knows; the pre-B1012 shared tree's leftovers land here);
  * otherwise KEEP.

Every decision is printed with the tree's size and age, so the job's log is
the census an operator reads to see what the runner is holding. ``--dry-run``
prints the same table and deletes nothing.

The live-ref list comes from ``--live-refs-file`` (one ref name per line,
``git ls-remote --heads origin | awk '{print $2}' | sed 's#refs/heads/##'``)
rather than from a git call inside this script, so the regression test can
feed it a fixed list and the script never needs network access.

Three more modes the pipeline uses:

  * ``--stamp <tree> --ref <name>``: write the ref-only stamp (the ``build`` /
    ``gate`` jobs call this before they configure) and exit. A ref-only stamp
    claims no build: it is what the prune reads, and writing it before a
    configure also WIPES any earlier host/sha claim, so a build that then
    fails leaves a tree that no later job will trust;
  * ``--stamp <tree> --ref <name> --sha <commit> [--host <h>]`` (B1026, #346):
    the BUILD CLAIM, written only after ``cmake --build`` succeeded. Same file,
    two more lines, ``host=`` (default: this machine's hostname) and ``sha=``.
    The prune's parser ignores them, so there is ONE stamp format;
  * ``--verify <tree> --host <h> --sha <commit>`` (B1026): exit 0 when the
    tree's stamp claims a build by exactly that host at exactly that commit,
    1 otherwise (missing stamp, ref-only stamp, other host, other sha),
    printing the stored and the expected host/sha either way. The ``test`` and
    ``client-ledger-live`` jobs run it before they touch the tree: since
    gitlab-runner-2 took the ``x86-build`` tag (2026-10-11) a pipeline's
    ``build`` and ``test`` jobs can land on different hosts, and fable
    pipeline 809 tested runner-2's pre-B603 binary after runner-1 built the
    new one (a false red; a false green is the same mechanism);
  * ``--legacy-root <build|build-gles2>``: the pre-B1012 layout put the shared
    tree at the root itself (``build/CMakeCache.txt``, ``build/bin`` ...).
    Everything at that root EXCEPT ``ref/`` and ``hygiene/`` is the old shared
    tree and is removed once, so the first per-ref pipelines do not inherit it
    and the disk is not paid twice.

Safety: a ``--root`` is only pruned if it is named ``ref`` and its parent is one
of the two known build roots (``build`` / ``build-gles2``) by name; a
``--legacy-root`` must itself be named one of those two; every deleted path is
``<root>/<one bare listdir entry>``, and nothing outside the roots given on the
command line is ever touched.
"""
import argparse
import datetime as _dt
import os
import shutil
import socket
import sys

STAMP = ".willeq_ref"
KNOWN_ROOT_PARENTS = ("build", "build-gles2")


def read_stamp(path):
    """Return (ref, stamped_at_utc) or (None, None) when absent/unparseable."""
    try:
        with open(path, encoding="utf-8") as f:
            lines = [l.strip() for l in f.read().splitlines() if l.strip()]
    except OSError:
        return None, None
    ref = None
    when = None
    for l in lines:
        if l.startswith("ref="):
            ref = l[4:]
        elif l.startswith("at="):
            try:
                when = _dt.datetime.strptime(l[3:], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=_dt.timezone.utc)
            except ValueError:
                when = None
    return ref, when


def read_stamp_fields(path):
    """Return every ``key=value`` line of a stamp as a dict ({} when absent)."""
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return {}
    fields = {}
    for l in text.splitlines():
        l = l.strip()
        if "=" in l:
            k, v = l.split("=", 1)
            fields[k] = v
    return fields


def write_stamp(tree, ref, now=None, host=None, sha=None):
    """Stamp ``tree`` (creating it) with ``ref`` and the current UTC time.

    With ``sha`` the stamp also records ``host`` (default: this machine) and
    the commit: the build claim ``verify_stamp`` checks (B1026). Without it the
    stamp is ref-only and claims no build."""
    now = now or _dt.datetime.now(_dt.timezone.utc)
    os.makedirs(tree, exist_ok=True)
    body = f"ref={ref}\nat={now.strftime('%Y-%m-%dT%H:%M:%SZ')}\n"
    if sha:
        body += f"host={host or socket.gethostname()}\nsha={sha}\n"
    with open(os.path.join(tree, STAMP), "w", encoding="utf-8") as f:
        f.write(body)


def verify_stamp(tree, host, sha, out=sys.stdout):
    """True when ``tree``'s stamp claims a build by ``host`` at ``sha`` (B1026).

    Prints one line either way, naming the stored and the expected host/sha,
    so a job log shows why a tree was trusted or rebuilt."""
    fields = read_stamp_fields(os.path.join(tree, STAMP))
    s_host = fields.get("host", "<none>")
    s_sha = fields.get("sha", "<none>")
    s_ref = fields.get("ref", "<none>")
    s_at = fields.get("at", "<none>")
    ok = bool(fields) and s_host == host and s_sha == sha
    verdict = "MATCH" if ok else "MISMATCH"
    if not fields:
        why = "no stamp"
    elif "sha" not in fields:
        why = "ref-only stamp (no successful build recorded)"
    else:
        why = "stamp read"
    print(f"stamp {verdict} {tree} ({why}): stored host={s_host} sha={s_sha} "
          f"ref={s_ref} at={s_at}; expected host={host} sha={sha}", file=out)
    return ok


def dir_size(path):
    total = 0
    for dirpath, _dirs, files in os.walk(path):
        for fn in files:
            try:
                total += os.lstat(os.path.join(dirpath, fn)).st_size
            except OSError:
                pass
    return total


def human(n):
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024.0
    return f"{n:.1f} GiB"


def decide(tree, live_refs, now, max_age_days):
    """Return (action, reason) for one tree. action is 'keep' or 'delete'."""
    ref, when = read_stamp(os.path.join(tree, STAMP))
    if ref is None or when is None:
        return "delete", "no stamp (not written by the per-ref layout)"
    if ref not in live_refs:
        return "delete", f"ref {ref!r} no longer on the remote"
    age_days = (now - when).total_seconds() / 86400.0
    if age_days > max_age_days:
        return "delete", f"ref {ref!r} last built {age_days:.1f} d ago > {max_age_days} d"
    return "keep", f"ref {ref!r} built {age_days:.1f} d ago"


def prune(roots, live_refs, *, now=None, max_age_days=14, dry_run=False, out=sys.stdout):
    """Walk each root's immediate children; return the list of deleted paths."""
    now = now or _dt.datetime.now(_dt.timezone.utc)
    deleted = []
    for root in roots:
        root = os.path.abspath(root)
        if not os.path.isdir(root):
            print(f"{root}: absent, nothing to prune", file=out)
            continue
        parent = os.path.basename(os.path.dirname(root))
        if os.path.basename(root) != "ref" or parent not in KNOWN_ROOT_PARENTS:
            raise SystemExit(f"refusing: {root} is not <build|build-gles2>/ref")
        for name in sorted(os.listdir(root)):
            tree = os.path.join(root, name)
            if not os.path.isdir(tree) or os.path.islink(tree):
                continue
            action, reason = decide(tree, live_refs, now, max_age_days)
            size = dir_size(tree)
            print(f"{action.upper():6} {human(size):>10}  {tree}  -- {reason}", file=out)
            if action == "delete" and not dry_run:
                shutil.rmtree(tree)
                deleted.append(tree)
    return deleted


LEGACY_KEEP = ("ref", "hygiene")


def sweep_legacy_root(root, *, dry_run=False, out=sys.stdout):
    """Remove the pre-B1012 shared tree's entries at a build root."""
    root = os.path.abspath(root)
    if os.path.basename(root) not in KNOWN_ROOT_PARENTS:
        raise SystemExit(f"refusing: {root} is not a build or build-gles2 root")
    if not os.path.isdir(root):
        print(f"{root}: absent, no legacy tree", file=out)
        return []
    removed = []
    for name in sorted(os.listdir(root)):
        if name in LEGACY_KEEP:
            continue
        path = os.path.join(root, name)
        kind = "dir" if os.path.isdir(path) and not os.path.islink(path) else "file"
        size = dir_size(path) if kind == "dir" else os.lstat(path).st_size
        print(f"LEGACY {human(size):>10}  {path}  -- pre-B1012 shared tree, removed", file=out)
        if not dry_run:
            if kind == "dir":
                shutil.rmtree(path)
            else:
                os.remove(path)
            removed.append(path)
    return removed


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", action="append", default=[],
                    help="a <build|build-gles2>/ref directory (repeatable)")
    ap.add_argument("--live-refs-file",
                    help="file with one live ref name per line (git ls-remote --heads)")
    ap.add_argument("--max-age-days", type=float, default=14.0)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--legacy-root", action="append", default=[],
                    help="a build or build-gles2 root whose pre-B1012 shared tree is swept")
    ap.add_argument("--stamp", help="stamp this tree with --ref and exit")
    ap.add_argument("--ref", help="the full ref name for --stamp")
    ap.add_argument("--verify",
                    help="exit 0 iff this tree's stamp claims a build by --host at --sha (B1026)")
    ap.add_argument("--host", help="building/expected hostname (--stamp default: this machine)")
    ap.add_argument("--sha", help="the commit sha: --stamp records a build claim; --verify expects it")
    a = ap.parse_args(argv)
    if a.verify:
        if not a.host or not a.sha:
            raise SystemExit("--verify needs --host and --sha")
        return 0 if verify_stamp(a.verify, a.host, a.sha) else 1
    if a.stamp:
        if not a.ref:
            raise SystemExit("--stamp needs --ref")
        if a.host and not a.sha:
            raise SystemExit("--host without --sha: a build claim needs the commit")
        write_stamp(a.stamp, a.ref, host=a.host, sha=a.sha)
        claim = f" host={a.host or socket.gethostname()} sha={a.sha}" if a.sha else " (ref only, no build claim)"
        print(f"stamped {a.stamp} ref={a.ref}{claim}")
        return 0
    if not a.root:
        raise SystemExit("nothing to do: give --root (and --live-refs-file), --stamp or --verify")
    if not a.live_refs_file:
        raise SystemExit("--root needs --live-refs-file")
    with open(a.live_refs_file, encoding="utf-8") as f:
        live = {l.strip() for l in f if l.strip()}
    if not live:
        raise SystemExit("refusing: the live-ref list is empty (ls-remote failed?)")
    swept = []
    for r in a.legacy_root:
        swept += sweep_legacy_root(r, dry_run=a.dry_run)
    deleted = prune(a.root, live, max_age_days=a.max_age_days, dry_run=a.dry_run)
    verb = "would delete" if a.dry_run else "deleted"
    print(f"{verb} {len(deleted)} per-ref tree(s), {len(swept)} legacy entr(y/ies)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
