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

# T373 (2026-09-05, board B130): DocClaims is a NAME table and is blind to the
# NUMBERS the orientation docs state by hand -- T371 found every one of the six
# it checked had drifted. This nets each count against a measuring function
# over the tree (enum bodies, the std::variant, registerCommand call sites,
# loadZoneStep* functions, *_component.h headers per family), and asserts that
# the two counts that MUST NOT be pinned (the suite total, the owner_ census)
# still name their measuring command instead of a number. Same
# no-artefact/no-server/no-display shape, same "hygiene" label.
ADD_TEST(NAME DocCounts
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_doc_counts.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(DocCounts PROPERTIES LABELS "hygiene")

# T445 (2026-09-07, board B457): DocClaims, DocCounts and DocCitations all stop
# at *.md -- T444 wrote that out loud, and B457 is what it cost. T428 solved the
# night ambient live and moved it; three comments in vision_ambient.h went on
# quoting the pre-T428 triple 0.08/0.08/0.15 while kNightAmbient five lines away
# said {0.26244, 0.31147, 0.24003}. That header is exactly where a future unit
# reads to sanity-check a vision floor, and solving against a stale base is the
# mistake T428 found in T405's own floors. This is the C++-comment analogue of
# DocClaims: a table of (file, retired pattern) rows matched against comment
# text only, with the same RETIRED-OK line exemption. Same
# no-artefact/no-server/no-display shape, same "hygiene" label.
ADD_TEST(NAME CommentClaims
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_comment_claims.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(CommentClaims PROPERTIES LABELS "hygiene")

# T403 (2026-09-06, board B260): the 64 BMPs under data/textures/ are 64 of
# willeq.eqe1's 67 entries, and until now they were checked-in binaries with
# no build recipe -- P26 retired their generator as dead while its output kept
# shipping, which is how the alpha-less particle atlas (B225) survived a year.
# tools/generate_textures is that generator, un-retired as a live target;
# scripts/build_textures.sh --check regenerates into a temp directory and
# asserts three things: every shipped BMP matches its pinned sha256 and the
# directory holds exactly the pinned set (PIN), every generator-owned file
# regenerates byte-identical (RECIPE), and the tool writes exactly the pinned
# names (CENSUS). Fast (~20 s, no GPU, EDT_NULL device), so no slow label.
IF(TARGET generate_textures)
    ADD_TEST(NAME TexturesReproducible
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/build_textures.sh --check
                --bin-dir $<TARGET_FILE_DIR:generate_textures>
                --textures-dir ${CMAKE_SOURCE_DIR}/data/textures
                --pin-file ${CMAKE_SOURCE_DIR}/data/textures.sha256)
    SET_TESTS_PROPERTIES(TexturesReproducible PROPERTIES
        LABELS "hygiene"
        TIMEOUT 600)
ENDIF()

# T398 (2026-09-05, board B230b): the shared UI/effects pack
# /mnt/projects/EverQuest_ETC1/willeq.eqe1 had no build recipe in the repo,
# so no atlas pixel could be changed without hand-assembling a 2.8 MB asset
# that three worktrees read concurrently. scripts/build_willeq_pack.sh now
# rebuilds it from the two checked-in input sources (procedural
# tools/ui_atlas_builder plus the 64 BMPs in data/textures/) and installs it
# atomically. This test rebuilds into a temp path and compares the sha256
# against both the pinned data/willeq.eqe1.sha256 and the installed pack, so
# a drift in either input -- or a hand-edit of the shared file -- is a red.
#
# SLOW: the ETC1 encode of 65 textures takes ~1 minute. RUN_SERIAL because
# it saturates the encoder and because three slots share the read side of
# the pack. Requires the two builder binaries, hence the TARGET guard.
IF(TARGET eqe1_builder AND TARGET ui_atlas_builder)
    ADD_TEST(NAME PackReproducible
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/build_willeq_pack.sh --check
                --bin-dir $<TARGET_FILE_DIR:eqe1_builder>
                --sha-file ${CMAKE_SOURCE_DIR}/data/willeq.eqe1.sha256)
    SET_TESTS_PROPERTIES(PackReproducible PROPERTIES
        LABELS "hygiene;slow"
        RUN_SERIAL TRUE
        TIMEOUT 900)
ENDIF()

# T414 Leg C (2026-09-06, board B290): the write-only field census as a ratchet.
# T404 found MixChannel::sampleRate written by every producer and read by
# nobody, for years; T407 turned that shape into a script and left 213 of its
# 224 rows untriaged. Every row is now classified in the script's EXEMPT table
# (wire / boarded / owned / report, each with a reason), and --check fails on
# two things: a write-only field that is NOT in the table, and a table entry
# whose field is no longer write-only. The second half is what keeps the table
# from rotting -- an exemption cannot outlive the row it excuses. Pure source
# scan, no artefact/server/display, same "hygiene" label as its neighbours.
ADD_TEST(NAME WriteOnlyFields
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_write_only_fields.py
            --repo ${CMAKE_SOURCE_DIR} --check)
SET_TESTS_PROPERTIES(WriteOnlyFields PROPERTIES LABELS "hygiene")
