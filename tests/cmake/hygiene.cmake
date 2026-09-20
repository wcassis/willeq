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

# T450 (2026-09-07, board B467): the third derived asset gets its pin.
# data/willeq.db is built by tools/willeq_db_builder from the EQ client's
# *.txt files plus data/*.json and config/race_models.json, and until this
# unit it had no recipe script, no pinned hash and no test -- while the two
# assets beside it (the BMPs and the shared pack) had all three. It could not
# have had one: the builder stamped metadata.build_date = __DATE__ " " __TIME__,
# so a recompile changed the bytes with identical inputs (T447 Review). That
# row is now metadata.input_digest, a digest of the inputs.
#
# Rebuilds into a temp path and compares the sha256 against both the pinned
# data/willeq.db.sha256 and the checked-in database, so an input drift or a
# hand-edit is a red. Touches NO shared pack and no GPU -- it reads the EQ
# client tree and writes only its own temp dir -- but the whole build is ~1 s
# of SQLite inserts plus a VACUUM, so it takes the plain "hygiene" label with
# a generous timeout rather than "slow".
IF(TARGET willeq_db_builder)
    ADD_TEST(NAME DbReproducible
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/build_willeq_db.sh --check
                --bin-dir $<TARGET_FILE_DIR:willeq_db_builder>
                --data-path ${CMAKE_SOURCE_DIR}/data
                --config-path ${CMAKE_SOURCE_DIR}/config
                --output ${CMAKE_SOURCE_DIR}/data/willeq.db
                --sha-file ${CMAKE_SOURCE_DIR}/data/willeq.db.sha256)
    SET_TESTS_PROPERTIES(DbReproducible PROPERTIES
        LABELS "hygiene"
        TIMEOUT 300)
ENDIF()

# T498 (2026-09-17, board B755): the shipped item->model table against the
# server. data/item_models.json is hand-carried, 3,661 rows, and until this unit
# NOTHING compared it to anything -- DbReproducible above pins willeq.db against
# the JSON, so the two agree with each other and neither agrees with the server.
# 2894 (Spiny Two Handed Sword) was the first row anyone checked and it was
# wrong (json 10009, server IT10004).
#
# The comparison is against a COMMITTED export, data/item_idfile.tsv (id, name,
# idfile for all 117,944 peq items, written by
# scripts/eqemu/export_item_idfile.sh under the eqemu lock), so this test needs
# no mariadb, no server, no artefact and no display -- the same shape as its
# neighbours, and the same "hygiene" label. The rule it applies is the server's
# own: Mob::GetEquipmentMaterial() is atoi(&item->IDFile[2]).
#
# It runs against data/item_models_drift_baseline.txt, which lists the 2,128
# rows that were already wrong at T498 and is a RATCHET in both directions: a
# row not on the list that drifts is a failure, and a listed row that has been
# fixed without being struck is also a failure. Regenerating the whole file is
# Will's call (it changes what the offline tools draw), not a data fix.
# `--strict` ignores the baseline and is how the red is reproduced.
ADD_TEST(NAME ItemModelsMatchServer
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check_item_models.py
            --repo ${CMAKE_SOURCE_DIR} --check --highlight 2894)
SET_TESTS_PROPERTIES(ItemModelsMatchServer PROPERTIES LABELS "hygiene")

# T414 Leg C (2026-09-06, board B290): the write-only field census as a ratchet.
# T404 found MixChannel::sampleRate written by every producer and read by
# nobody, for years; T407 turned that shape into a script and left 213 of its
# 224 rows untriaged. Every row is now classified in the script's EXEMPT table
# (wire / boarded / owned / report, each with a reason), and --check fails on
# two things: a write-only field that is NOT in the table, and a table entry
# whose field is no longer write-only. The second half is what keeps the table
# from rotting -- an exemption cannot outlive the row it excuses. Pure source
# scan, no artefact/server/display, same "hygiene" label as its neighbours.
# B840 (kanban t_a817842a): the NEVER-TOUCHED half, same ratchet. The script
# collected both halves all along (write-only AND unused: no writer, no
# reader), but --check ratcheted only the write-only half, which is why
# RaceModelData::decodedTextures (zero references, dead since the background-
# decode scheme it was for was superseded by ConstrainedTextureCache) survived
# uncaught. The fix has two legs: a receiver-typing sweep (a qualified use of
# a member is attributed to the receiver's own type where the tree types the
# receiver, so the DIFFERENT member EquipmentPrepData::decodedTextures stops
# counting as a use of RaceModelData::decodedTextures -- the bare-name
# collision that masked it for years), and an EXEMPT_UNUSED table ratcheted
# both ways exactly like EXEMPT. The script surfaced five more fields the old
# name-matching sweep masked the same way (CombatDamage_Struct's
# force/hit_heading/hit_pitch, TextureInfo::animationDelayMs,
# WldFragment2CHeader::size9); all eight are in the table with a reason.
ADD_TEST(NAME WriteOnlyFields
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_write_only_fields.py
            --repo ${CMAKE_SOURCE_DIR} --check)
SET_TESTS_PROPERTIES(WriteOnlyFields PROPERTIES LABELS "hygiene")

# T455 (2026-09-07, boards B490-B502): the zero-caller census as a ratchet.
# WriteOnlyFields made the FIELD half of this project's dominant defect class
# enforceable (a field written and never read). This is the FUNCTION half.
# T366, T414, T429, T442, T448 and T452 each found more implemented-but-
# unreached code BY HAND, one identifier at a time, and every one of those
# censuses died with the unit that took it. check_zero_callers.py collects
# every free function and every public/protected member function declared
# under include/ and src/, sweeps src/ include/ tests/ tools/ benchmarks/
# docker/ for uses (board B166: a census that skips tools/ is not tree-wide),
# and --check fails on two things: a zero-caller function NOT in the script's
# EXEMPT table, and a table entry whose function has a caller again. The
# second half is what keeps the table from rotting. Pure source scan, no
# artefact/server/display, same "hygiene" label as its neighbours; it walks
# ~1,200 files twice, so it gets a generous timeout rather than the "slow"
# label.
# T458 (boards B600-B607): the same --check also ratchets the functions whose
# ONLY callers are under tests/ against EXEMPT_TESTS_GROUPS -- each one a
# `seam` (a test hook on a production class), an `observer` (an accessor on a
# class with live callers) or a boarded never-wired row. Dead production code
# whose only net was its own test is deleted with that test, not listed.
ADD_TEST(NAME ZeroCallers
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_zero_callers.py
            --repo ${CMAKE_SOURCE_DIR} --check)
SET_TESTS_PROPERTIES(ZeroCallers PROPERTIES LABELS "hygiene" TIMEOUT 600)

# T486 (2026-09-14): the classifier behind ZeroCallers has its own probes.
# classify_occurrence() scored a continuation line's `value, foo(x)` /
# `a & foo(x)` / `a * foo(x)` / `a < foo(x)` as DEFINITIONS -- the direction
# that invents a dead function for the next hygiene unit to delete. The
# --self-test runs every shape the census has ever scored wrongly, and the
# definition shapes a fix must not hide, through the live classifier; no tree
# is scanned, so it is instant. Same "hygiene" label.
ADD_TEST(NAME ZeroCallersSelfTest
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_zero_callers.py
            --self-test)
SET_TESTS_PROPERTIES(ZeroCallersSelfTest PROPERTIES LABELS "hygiene")

# B824 (issue #86, kanban t_20a88e62): the deleted item-to-model JSON map,
# ratcheted. EquipmentModelLoader parsed all 3,661 rows of
# data/item_models.json into itemToModelMap_ on every startup and read it
# back from nothing -- getModelIdForItem() queries WilleqDatabase's
# item_models table (the db is built from that JSON). This unit deleted the
# map and its plumbing (the member, loadItemModelMapping[/Static],
# getMappingCount, MemoryStats::mappingCount, adoptIndex()'s map argument,
# and a second never-referenced copy on IrrlichtRenderer). check_item_model_map.py
# fails if any of the map's identifiers reappears under src/ include/ tools/
# benchmarks/ docker/ OR if getModelIdForItem() stops querying the database.
# Pure source scan, no artefact/server/display; the C++ half of the pin is
# tests/test_item_model_mapping.cpp (db resolves an item with no JSON map
# loaded). Same discipline as WriteOnlyFields (B840) and ZeroCallers (B491).
ADD_TEST(NAME ItemModelMapDeletion
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_item_model_map.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(ItemModelMapDeletion PROPERTIES LABELS "hygiene")

# T491 (2026-09-14): the memory-ledger half of the visual gate
# (scripts/ci/visual_gate.py assert_memory_ledger) and the cap-hunt script
# (scripts/ci/memory_cap_hunt.sh). The first drives the assertion on
# synthetic memory dicts -- no benchmark, no artefact. The second runs the
# hunt over a fake command under systemd-run --user --scope and SKIPs (125)
# where no user manager exists. Both are pure-script checks, "hygiene" label.
ADD_TEST(NAME VisualGateLedgerAssertions
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_visual_gate_ledger.py)
SET_TESTS_PROPERTIES(VisualGateLedgerAssertions PROPERTIES LABELS "hygiene")
ADD_TEST(NAME MemoryCapHuntScript
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/test_memory_cap_hunt.sh)
SET_TESTS_PROPERTIES(MemoryCapHuntScript PROPERTIES LABELS "hygiene" SKIP_RETURN_CODE 125)

# B849 (2026-09-18): the board-to-issue migration script's regression net.
# The pre-B849 script deduped against OPEN issues only and never read the
# `→ issue #N` pointer it wrote into refactor/BOARD.md, so closing a
# migrated issue and re-running the script created it AGAIN; and an unstruck
# bullet whose own header says the work is fixed (B817/B819/B830, B805, B837)
# was migrated as live work. test_board_to_issues.py drives plan_actions()
# and label_missing_status_plan() with injected fixtures -- the same shapes
# the real http() would have returned -- so it needs no network, no GitLab
# token and no artefact. Pure python, same "hygiene" label as its neighbours.
ADD_TEST(NAME BoardToIssuesHygiene
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/gitlab/test_board_to_issues.py -v)
SET_TESTS_PROPERTIES(BoardToIssuesHygiene PROPERTIES LABELS "hygiene")

# B811 (issue #82): the llmbench held-item before/after gate.  T494's legH
# timeline let p3's /equip step score as a success although the character
# was ALREADY holding the destination item at slot 13 (timeline_legH.tsv:27),
# because nothing asserted BEFORE != AFTER.  test_timeline_held_check.py
# drives the production gate (timeline_held_check.check_held_steps, wired
# into compile_timeline.py) and the production verifier (verify_labels.py)
# on the committed legH rows: the no-op step must be INVALID at compile time
# and its held-B row must FAIL verification, while a genuine move still
# passes.  Pure python over committed TSVs, no server, no artefact.
ADD_TEST(NAME LlmBenchHeldBeforeAfterHygiene
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/llmbench/test_timeline_held_check.py -v)
SET_TESTS_PROPERTIES(LlmBenchHeldBeforeAfterHygiene PROPERTIES LABELS "hygiene")

# B793 (issue #75): the llmbench timeline's held= claims must come from the
# inventory snapshot, not from hand-typed constants.  The committed Leg C
# timeline claimed Testcaster holds 13=9998 while the run's own snapshot
# says 14337, and its /equip after-state was refused by the client.
# test_timeline_held_snapshot.py drives the production generator
# (make_timeline.build_timeline) on the committed T492 reference snapshot
# (scripts/llmbench/fixtures/T492/db_before/) and checks the committed
# timeline.tsv against it: every held= claim is the snapshot's, the /equip
# after-state is not claimed, the held-B frames are parked with a why=, and
# a missing snapshot is a loud error.  RED on the pre-B793 tree (the
# committed file still claims 9998), GREEN once the timeline is regenerated.
# Pure python over committed TSVs, no server, no artefact.
ADD_TEST(NAME LlmBenchTimelineHeldFromSnapshot
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/llmbench/test_timeline_held_snapshot.py -v)
SET_TESTS_PROPERTIES(LlmBenchTimelineHeldFromSnapshot PROPERTIES LABELS "hygiene")
