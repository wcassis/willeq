# B1018 (GitLab #339) — the write-only census misses multi-declarator lines

**Fixed in the census itself, `scripts/refactor/check_write_only_fields.py`.** The
DECL regex captured only the FIRST declarator of a declaration line —
`int a = 0, b = 0;` registered `a` and `b` was invisible to both censuses —
and `ConsoleRenderer::m_maxEnd` (boarded B1018) sits on exactly such a line,
which is why it never surfaced in an earlier pass. `split_declarators()` now
splits declaration bodies on top-level commas (string-literal, parenthesis,
bracket, template `<...>` and array-dimension aware) and registers every
declarator.

## AC1b — the self-test

Nine probes (`SPLIT_PROBES`, beside the existing `PROBES`) cover: plain
two/three declarators, an initializer with a call, a comma INSIDE a string
literal (`"x,y"`), a C-string array next to an int, and a template
declaration. `split_self_test()` runs them two ways and is exposed as
`--self-test` (exit 1 on any failure), registered in ctest as
`WriteOnlyFieldsSelfTest` in the `hygiene` label (the `ZeroCallersSelfTest`
pattern).

RED (the probes against the pre-B1018 collector, no split — captured in
`wof_red_run.txt`):

```
== RED: the AC1b probes against the PRE-B1018 collector (no split) ==
unsplit(pre-B1018): FAIL 'int a = 0, b = 0;'                                        split ['a'], expected ['a', 'b']
unsplit(pre-B1018): FAIL 'float x = 1.0f, y = 2.0f, z = 3.0f;'                      split ['x'], expected ['x', 'y', 'z']
unsplit(pre-B1018): FAIL 'std::string first = "a", second = "b";'                   split ['first'], expected ['first', 'second']
unsplit(pre-B1018): FAIL 'int x = 1, y = f(a, b);'                                  split ['x'], expected ['x', 'y']
unsplit(pre-B1018): FAIL 'const char* s = "x,y", t = "z";'                          split ['s'], expected ['s', 't']
unsplit(pre-B1018): FAIL 'char name[8] = "abc", flags = 0;'                         split ['name'], expected ['name', 'flags']
unsplit(pre-B1018): 9 probes, 6 failed
== GREEN: the same probes against the committed split ==
split(committed): 9 probes, 0 failed
AC1b RED captured: unsplit fails 6 probe(s), committed split 0.
```

## Whole-tree delta (measured, `wof_check_after.log`)

| census | pre-B1018 (unsplit) | post-B1018 (split) |
|---|---|---|
| declared members | 8,061 (5,397 distinct) | **8,316 (5,532 distinct)** |
| write-only fields | 45 | **45** (zero new rows) |
| unused fields | 86 | **89** (+3, all rowed in `EXEMPT_UNUSED`) |

<pre>
wof_check_after.log is the committed (split) checker's --check at the new
head (post the fable merge of f5c06497, re-merged on top of B534's output
camera route), and wof_check_merged.log is the pre-B1018 baseline at 89d1b00d
(the merge-base). Both logs live in the QA evidence directory
(kb-evidence/b1018-b1019-wave7-nets-docs/) -- *.log is gitignored, so they
are not in the tree -- and were recaptured at the new head, so they print
8316 (5532) and 8061 (5397) exactly like the table above. The B1015
PCM-cache merge (f5c06497) moved the whole-tree count from 8,320 (5,536)
to 8,317 (5,533) -- it added the seven SoundBufferCache members and removed
ten superseded AudioManager/SfxManager cache members -- and B534 (b3c921a7,
pulled in by the same fable merge) then deleted GraphicalRenderer::m_cameraMode
with its output camera route, taking the count from 8,317 (5,533) to the
8,316 (5,532) the post cell above records. The B1018 delta itself is
unaffected (259 on 153 lines, both at the new head and at the old).
</pre>

259 members became visible on the **153 lines** carrying more than one
declarator (`wof_multidecl_lines.txt`, one `Type::field` line per
declaration site). The +3 unused are genuine no-writer/no-reader leftovers
named in the new `EXEMPT_UNUSED` rows with reasons:
`DetailInput::playerVelY`, `EntityVisual::occlusionTestY`,
`EntityVisual::occlusionTestZ`. No new write-only row appeared — the 45
unchanged, all 45 classified in `EXEMPT` — so the split cannot have
invented a defect; it can only have surfaced real members, and it did not
surface a single unread-write field.

The issue's own example (`m_maxEnd`) was not a census artifact: it IS
write-only and B339 deleted it with its writes (see the "Issue handling"
section of `refactor/B339/README.md`).

## Issue notes

Posted 2026-10-13:

- #339 — https://gitlab.goldfarm.local/willeq/willeq/issues/139#note_4233
- #135 (`currentTargetInfo_` split — it is NOT write-only,
  `currentTargetLevel_` is read at
  `src/client/graphics/overlays/target_ring_draw.cpp:48`) —
  https://gitlab.goldfarm.local/willeq/willeq/issues/135#note_4230

## Hygiene fallout of the new test

`WriteOnlyFieldsSelfTest` joins the `hygiene` label (31 -> 32), which the
DocCounts net reads at `docs/module_map.md` ("hygiene ctest tests"); the
module map's line is updated in the same commit.
