Finding 6 RED/GREEN provenance (MR !168 #204)
1: RED build: origin/fable f8ead59d31c88a1c7dcccaf0f94bc713b990777d (the MR !168 base, pre-FacePick), scratch worktree, test_char_create_screen built, --gtest_filter=CharCreateClicks.SetFaceButtonIsNotGreyed: 1 test selected, FAILED (Set Face greyed -> CharCreateHit::None).
2: GREEN build: this branch head, same test body (byte-identical): 1 test selected, PASSED (Set Face resolves to the FacePick modal).
3: The test names only seam symbols present in both trees, so the baseline compiles and runs it (selected > 0); zero-selected filters are not a regression reproduction.
