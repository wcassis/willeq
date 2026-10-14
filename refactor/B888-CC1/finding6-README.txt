Finding 6 RED/GREEN provenance (MR !168 #204)
1: RED build: origin/fable ac32a25eb2c1ac5fc22570fe490a89fe1341324d (the GitLab fable tip of 2026-10-14, pre-FacePick -- verified by a direct fetch in the primary checkout; the MR !168 base f8ead59d is its ancestor), scratch worktree, test_char_create_screen built, --gtest_filter=CharCreateClicks.SetFaceButtonIsNotGreyed: 1 test selected, FAILED (Set Face greyed -> CharCreateHit::None).
2: GREEN build: this branch head, same test body (byte-identical): 1 test selected, PASSED (Set Face resolves to the FacePick modal).
3: The test names only seam symbols present in both trees, so the baseline compiles and runs it (selected > 0); zero-selected filters are not a regression reproduction.
