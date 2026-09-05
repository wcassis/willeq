# tests/cmake/hygiene.cmake -- included from tests/CMakeLists.txt.
# K00 Leg B (2026-09-03): the file-size ceiling and the live-doc citation
# checker are enforced tree-wide under ctest, not just as a manual
# verification-bar step run by an executor. Both are pure source/doc scans --
# no build artefact, no server, no display -- so they run in every
# configuration. Labeled "hygiene" (ctest -L hygiene selects just these).

ADD_TEST(NAME FileSizeCeiling
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/refactor/check_file_sizes.sh
            -C ${CMAKE_SOURCE_DIR} --all)
SET_TESTS_PROPERTIES(FileSizeCeiling PROPERTIES LABELS "hygiene")

ADD_TEST(NAME DocCitations
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_doc_citations.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(DocCitations PROPERTIES LABELS "hygiene")

# T371 (2026-09-05): DocCitations proves a cited PATH resolves; it says
# nothing about the CLAIMS. This is the regression net for the deletion half
# -- a table of retired names that must not reappear in the ORIENTATION docs
# (root CLAUDE.md, every */CLAUDE.md, README.md, docs/*.md). Same
# no-artefact/no-server/no-display shape, same "hygiene" label.
ADD_TEST(NAME DocClaims
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_doc_claims.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(DocClaims PROPERTIES LABELS "hygiene")
