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

# B888-CC1: XML geometry must be checked even by the source-only hygiene job.
ADD_TEST(NAME FacePickXmlGeometry
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/b888/check_facepick_geometry.py
            --source ${CMAKE_SOURCE_DIR}
            --xml /mnt/projects/EverQuestP1999/uifiles/default/EQUI_FacePick.xml)
SET_TESTS_PROPERTIES(FacePickXmlGeometry PROPERTIES LABELS "hygiene")

ADD_TEST(NAME DocCitations
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_doc_citations.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(DocCitations PROPERTIES LABELS "hygiene")

# B1020 (wave-7 retrospective ids 5, 37): DocCitations' anchored form
# `path:LINE (anchor)` fails when the anchor is not on that line -- a line
# number that merely exists was hygiene-green on four wrong lines. The
# --self-test drives that rule's accept/refuse shapes on a temp tree; no tree
# is scanned, so it is instant. Same "hygiene" label.
ADD_TEST(NAME DocCitationsSelfTest
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_doc_citations.py
            --self-test)
SET_TESTS_PROPERTIES(DocCitationsSelfTest PROPERTIES LABELS "hygiene")

# B1030 (#350): docker/Dockerfile.arm64-noble is docker/Dockerfile.arm-noble with
# the architecture swapped and nothing else. The first arm64 file (52cb54d1)
# drifted unnoticed for three months (SourceForge wgets after B970, missing
# probe sources, a stale target list) because nothing compared them; this
# regenerates it through the substitution table in the script and fails on any
# other difference. Pure text, instant, no docker.
ADD_TEST(NAME DockerfileArm64Derived
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/check_arm64_dockerfile.sh)
SET_TESTS_PROPERTIES(DockerfileArm64Derived PROPERTIES LABELS "hygiene")
# B1030 review: scripts/pi/env.sh refuses a PI_LANE that disagrees with a
# listed (exported) PI_HOST instead of silently rewriting the lane. Pure shell,
# no ssh, no device.
ADD_TEST(NAME PiEnvLaneResolution
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/pi/env_selftest.sh)
SET_TESTS_PROPERTIES(PiEnvLaneResolution PROPERTIES LABELS "hygiene")

# B1034 (#357): the x86 surfaceless-EGL config fixture
# (tests/data/b1034/surfaceless_es2.json) stays honest: the hygiene tree
# re-dumps the list the x86 device fork consumes (initEGLSurfaceless's own
# query, the CI GL pin = llvmpipe, no /dev/dri) and diffs it. A driver update
# that changes llvmpipe's config list goes RED here; re-dump with
# `bash scripts/ci/egl_config_dump.sh dump <dir>` and commit the fixture.
ADD_TEST(NAME EglConfigDumpB1034
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/egl_config_dump.sh verify
            ${CMAKE_SOURCE_DIR}/tests/data/b1034)
SET_TESTS_PROPERTIES(EglConfigDumpB1034 PROPERTIES LABELS "hygiene"
    SKIP_RETURN_CODE 235)

# B1034 (#357): the chooseConfigForGbmFormat wrapper is hand-copied into BOTH
# device forks (docker/irrlicht-drm/ and docker/irrlicht-drm-x86/CIrrDeviceFB.cpp)
# and must stay byte-identical in its body (the include lines differ). A pure
# source diff; the shared logic lives in
# docker/irrlicht-drm/gbm_config_select.h (unit-tested by
# test_egl_config_select in the build stage).
ADD_TEST(NAME GbmConfigSelectForsB1034
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_gbm_config_select_forks.py
            ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(GbmConfigSelectForsB1034 PROPERTIES LABELS "hygiene")

# B1034 (#357): the pi_hdmi_force_on / pi_hdmi_restore contract of
# scripts/pi/env.sh, pinned against a fake device surface (shadowed ssh/sudo,
# local status/record files) -- no device, no ssh, no root. Like
# PiEnvLaneResolution above.
ADD_TEST(NAME PiHdmiForceSelftest
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/pi/hdmi_force_selftest.sh)
SET_TESTS_PROPERTIES(PiHdmiForceSelftest PROPERTIES LABELS "hygiene")

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

# B888-CC4 (GitLab #204): the charcreate gate's state list is ONE list, kept in
# three places that must not drift apart: GATE_STATES in
# scripts/ci/charcreate_gate_controls.py, the kStates[] array in
# tools/hwcaps/charcreate_capture.cpp, and the scores table in
# refactor/B888-CC4/README.md. A state the gate names but the capture does not
# emit (or the README does not score) is a silent coverage hole. Pure text
# scan -- no build artefact, no display; the "hygiene" label, runs in every
# configuration.
ADD_TEST(NAME CharCreateGateStateList
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_charcreate_gate_state_list.py)
SET_TESTS_PROPERTIES(CharCreateGateStateList PROPERTIES LABELS "hygiene")

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
# against the pinned data/willeq.eqe1.sha256 ONLY: reproducibility from the
# inputs is the property it owns. B1094 (#414) removed the installed-pack
# leg -- one mutable shared file was the oracle for N branches, so a
# pack-changing card installing its branch pack over the shared file made
# every other pipeline at the fable tip red here (MR !230 pipeline 1626).
# The installed-vs-pin property is InstalledPackMatchesPin below.
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

# B1094 (#414): the other half of the old PackReproducible verdict. The
# installed shared pack must match this tree's pin -- the property that
# matters on fable, where the installed pack IS the tree's. The wrapper
# scripts/ci/installed_pack_matches_pin.sh converts the verdict: on
# CI_COMMIT_BRANCH=fable a mismatch is a HARD FAIL (a merge changed the pin
# and the orchestrator has not installed yet -- say so loudly), anywhere
# else it prints and passes (a pack-changing branch legitimately carries a
# new pin against the fable-installed pack; its pipeline stays green on its
# own, and the orchestrator installs the shared pack at merge). No build and
# no builder binaries -- a plain hygiene test.
ADD_TEST(NAME InstalledPackMatchesPin
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/installed_pack_matches_pin.sh)
SET_TESTS_PROPERTIES(InstalledPackMatchesPin PROPERTIES
    LABELS "hygiene"
    TIMEOUT 60)

# B1094 (#414): the contract that pins the split above -- --check must ignore
# the installed pack (stub builders, temp files only, the shared pack is
# never read or written) and --check-installed must fail on a mismatch, with
# the wrapper failing hard on fable and printing-and-passing elsewhere.
# Pure python + bash against a temp directory, no builder binaries.
ADD_TEST(NAME PackCheckContract
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_pack_check_contract.py)
SET_TESTS_PROPERTIES(PackCheckContract PROPERTIES
    LABELS "hygiene"
    TIMEOUT 300)

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

# B1018 (issue #339, AUDIT-WAVE7 on MR !130): the write-only census's
# DECL regex captured only the FIRST declarator of `T a = 0, b = 0;`, so
# `b` was invisible to the census -- that is why `m_maxEnd` (written, never
# read) never got an EXEMPT row. The split (split_declarators, top-level
# commas) plus its probes now run as their own self-test beside the
# ZeroCallers one above: the `int a = 0, b = 0;` row (AC1b's own) and the
# negative shapes a naive comma split would break (template arguments, an
# expression's comma, a literal's comma). Instant -- no tree is scanned.
# The ratchet itself (WriteOnlyFields) sees the split through the same
# script; its delta was 259 members on 153 lines, write-only 45 -> 45
# (zero new rows), unused 86 -> 89 (three rowed in EXEMPT_UNUSED).
ADD_TEST(NAME WriteOnlyFieldsSelfTest
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_write_only_fields.py
            --self-test)
SET_TESTS_PROPERTIES(WriteOnlyFieldsSelfTest PROPERTIES LABELS "hygiene")

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

# B1024 (#344): the per-platform golden-set resolution. --verify-x86 re-records
# the x86 gate's benchmark invocation (subprocess patched) and requires it to
# stay byte-identical to tests/data/b1024/x86_gate_plan.txt -- the fixture was
# generated on the UNTOUCHED tree in step 1, so a platform change that moves
# the x86 golden paths or the gate's command is red here before it reaches CI.
# --check-pi pins the pi resolver (tests/goldens/pi/<zone>/f<n>.png, the
# platforms.pi section, --bench, --drm), the NOT YET BLESSED verify, the
# --bless --platform pi refusal, the --capture layout (nothing under
# tests/goldens/) and the Mesa-prefix maps proof. Pure-python, "hygiene" label.
ADD_TEST(NAME VisualGatePlatform
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_visual_gate_platform.py)
SET_TESTS_PROPERTIES(VisualGatePlatform PROPERTIES LABELS "hygiene")

# B923 / #242: the repeatable budget check for a CLIENT-ledger capture.
# `scripts/client_ledger_table.py check` asserts every budgeted
# memoryLedgerBudgets category of a client_ledger_capture.sh evidence dir at
# ready and steady_60s -- including the client-only owners the gate's
# benchmark never builds (B923's whole point: the gate sees the benchmark,
# not the client). This ctest pins that contract with no server, no display
# and no binary: a capture-shaped fixture in the client's own log-line format,
# driven through `check` for the three exit codes (0 in-budget / 1 a
# client-only category over budget / 3 a run without steady_60s). Same
# pure-python shape as VisualGateLedgerAssertions above.
ADD_TEST(NAME ClientLedgerBudgetCheck
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_client_ledger_check.py)
SET_TESTS_PROPERTIES(ClientLedgerBudgetCheck PROPERTIES LABELS "hygiene")
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

# B908 (issue #228): "B100" named two different items -- the struck
# InputAction entry (T368) and the soldunga EQE1 name-truncation work, filed
# under GitLab issue #100 by mistake and renumbered to B910. Nothing checked
# that a refactor/B###/ directory's declared subject agreed with the
# BOARD.md header(s) naming the same number; check_board_dir_numbers.py is
# the net -- RED on the pre-B908 tree (refactor/B100/ vs the unrelated B100
# InputAction header), GREEN after the soldunga work moved to refactor/B910/.
# Pure source/doc scan, no artefact/server/display, same "hygiene" label.
ADD_TEST(NAME BoardDirNumbers
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/refactor/check_board_dir_numbers.py
            --repo ${CMAKE_SOURCE_DIR})
SET_TESTS_PROPERTIES(BoardDirNumbers PROPERTIES LABELS "hygiene")

# B907 (issue #227): the CI runner keeps one warm build tree across every
# branch's pipeline (docs/ci.md), so a binary left in bin/ by branch A's
# pipeline made check_build_tree_orphans (above, T175) correctly fail branch
# B's pipeline next. check_build_tree_orphans.py's --prune mode removes such a
# leftover, using the SAME target-discovery function (_discover()) the
# read-only check itself uses -- this is that mode's own regression net.
# Builds a fake CMake file-API reply by hand in a fresh
# tempfile.TemporaryDirectory() per test (unique per process, so it is safe
# under `ctest -jN` across worktree slots -- no ScopedTempDir needed, this is
# pure Python, not one of the C++ binaries test_tmp_path_census.cpp scans).
# Pure source scan, no artefact/server/display, same "hygiene" label as its
# neighbours.
ADD_TEST(NAME CheckBuildTreeOrphansPrune
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_check_build_tree_orphans.py -v)
SET_TESTS_PROPERTIES(CheckBuildTreeOrphansPrune PROPERTIES LABELS "hygiene")

# B1012 (#333): the per-ref warm-tree expiry on the CI runner. Pure temp-dir
# test of scripts/ci/prune_ref_build_trees.py's decision rules + refusals.
ADD_TEST(NAME PruneRefBuildTrees
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_prune_ref_build_trees.py -v)
SET_TESTS_PROPERTIES(PruneRefBuildTrees PROPERTIES LABELS "hygiene")

# B1171 (#490): the FX harness sweeps cast at the level the ORACLE capture ran
# (its executed `#level`, scripts/fx/capture_level.py), and a comparison row
# whose harness level differs from the oracle row's is refused. Pure temp-dir
# fixtures (a capture's plan.tsv + steps.tsv, a run's timing.tsv / plan).
ADD_TEST(NAME FxCaptureLevel
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_fx_capture_level.py -v)
SET_TESTS_PROPERTIES(FxCaptureLevel PROPERTIES LABELS "hygiene")

# B1026 (#346): two runners carry `x86-build`, so a pipeline's build and test
# jobs can land on different hosts (fable pipeline 809: false red). Pins the
# .gitlab-ci.yml shape that makes that safe: build-test/client-ledger-live verify
# the host+sha stamp before they consume the tree (rebuild in-job on a
# mismatch), the build steps exist once and claim the tree only after
# `cmake --build`, and .goldfarm no longer claims a single tagged runner.
ADD_TEST(NAME CiBuildTreeStamp
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_ci_build_tree_stamp.py)
SET_TESTS_PROPERTIES(CiBuildTreeStamp PROPERTIES LABELS "hygiene")

# B1042 (#362): the CI queue savings -- the guarded ccache launcher on every
# x86 configure (+ the hit-rate report after every build), docs-only diffs
# skipping the C++ jobs while hygiene always runs, and build+test as ONE job.
ADD_TEST(NAME CiQueueRules
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_ci_queue_rules.py)
SET_TESTS_PROPERTIES(CiQueueRules PROPERTIES LABELS "hygiene")

# B1093 (#413): the ARM cross-build container dies with the shell that started
# it -- both build scripts run it named + labelled under a kill trap, and both
# CI ARM jobs sweep this host's orphans (a cancelled/timed-out job's SIGKILL
# skips the trap) before they build.
ADD_TEST(NAME ArmContainerGuard
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_arm_container_guard.py)
SET_TESTS_PROPERTIES(ArmContainerGuard PROPERTIES LABELS "hygiene")

# B975: zone-hop warmup must not contaminate the crowd's idle timing table.
ADD_TEST(NAME NameTagCrowdTiming
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/b975/test_frametiming_table.py)
SET_TESTS_PROPERTIES(NameTagCrowdTiming PROPERTIES LABELS "hygiene")

ADD_TEST(NAME CrowdObservationControls
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/b964/test_observation_controls.py)
SET_TESTS_PROPERTIES(CrowdObservationControls PROPERTIES LABELS "hygiene")

ADD_TEST(NAME RenderedZoneFinalVisit
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/pi/test_rendered_zone_final_visit.py)
SET_TESTS_PROPERTIES(RenderedZoneFinalVisit PROPERTIES LABELS "hygiene")

# B946 (#264): the rig's half of the [gldriver] net -- the summarizer's
# mesa_driver field over synthetic client.log lines (no Pi, no GL, no maps
# snapshots), pinned the same way as its neighbours above.
ADD_TEST(NAME RenderedZoneSummarizeGlDriver
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/pi/test_rendered_zone_gl_driver.py)
SET_TESTS_PROPERTIES(RenderedZoneSummarizeGlDriver PROPERTIES LABELS "hygiene")

# B1057 (#377): the oracle driver's lane gate (lane 5 accepted, 0/6 refused,
# lock and create-name safety) and the remote stop path that must never kill
# a display it did not start (lane 5's :99 is a real Xorg). Offline stubs.
ADD_TEST(NAME OracleSessionSafety
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/session_safety.sh)
SET_TESTS_PROPERTIES(OracleSessionSafety PROPERTIES LABELS "hygiene")
ADD_TEST(NAME OracleRemoteStopSafety
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/remote_stop_safety.sh)
SET_TESTS_PROPERTIES(OracleRemoteStopSafety PROPERTIES LABELS "hygiene")
# B1134 (#454): the observer's login copy (a second official client as a
# second player character) sources its credentials file, never types a secret.
ADD_TEST(NAME OracleLoginPatch
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/login_patch_test.sh)
SET_TESTS_PROPERTIES(OracleLoginPatch PROPERTIES LABELS "hygiene")

# B1129 (#449): the official client refuses a gem cast its class cannot use
# and sends nothing (refactor/B1118/README.md), so a plan doing that is
# refused with the spell and the class named; the per-lane gem-pitch table
# (lane 5 measured, lanes 1-4 never guessed); and the multi-class provisioning
# helper's hooks (class switch only from the home class, world-down undo of
# class + memorised gems + buffs). Device-free fixtures; the real
# spells_us.txt is cross-checked only when the EQ client files are present.
ADD_TEST(NAME OracleGemCastPlanCheck
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/oracle_plan_check_test.py)
SET_TESTS_PROPERTIES(OracleGemCastPlanCheck PROPERTIES LABELS "hygiene")
ADD_TEST(NAME OracleClassLegSafety
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/class_leg_safety.sh)
SET_TESTS_PROPERTIES(OracleClassLegSafety PROPERTIES LABELS "hygiene")

# B1089 (#409): the lane lease's hermetic net. The core
# (scripts/eqemu/lease_core.sh) is host-side and self-contained, so running
# it against a temp dir IS the unit test -- no ssh, no network, no lane
# host. Covers: take on a free lane, second taker refused, show HELD with
# expires_in, non-owner release refused (rc=63, the B922 owner-check half),
# owner renew extends last_touched, TTL expiry -> EXPIRED, takeover after
# expiry names the former holder, the former holder's renew fails with 63 +
# the re-lease one-liner, re-acquire after release, reap releases a dead
# run's lease and kills exactly its tracked pids (a live run's process is
# untouched), reap --dry releases nothing and kills nothing, a five-way
# concurrent take race (exactly one winner under flock), and a corrupt
# record (refused, not deleted). ~5 s (three sub-second TTL sleeps), plain
# "hygiene" label. Red-before is N/A: the helper is new.
ADD_TEST(NAME LeaseCoreSelfTest
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/eqemu/lease_selftest.sh)
SET_TESTS_PROPERTIES(LeaseCoreSelfTest PROPERTIES LABELS "hygiene" TIMEOUT 120)

# B1058 (#378): the oracle driver's template library located over the archived
# corpus (review/B1058/corpus): every positive within 1 px of its measured
# centre and above min_score, every negative below it, cv2 and numpy agreeing
# to 1e-3. SKIPs (125) on a host without the corpus or NumPy/Pillow -- the CI
# runners have neither.
ADD_TEST(NAME OracleLocateTemplates
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/oracle_locate_test.py)
SET_TESTS_PROPERTIES(OracleLocateTemplates PROPERTIES LABELS "hygiene"
    SKIP_RETURN_CODE 125 TIMEOUT 900)

# B1059 (#379): the glyph-atlas text reader over pinned, labelled crops of the
# archived oracle frames (truth from the pcap/DB/plan/XML side): every
# expect=read crop read exactly, every expect=unknown crop UNKNOWN, never a
# wrong text; cv2 and numpy segmenting and reading identically. SKIPs (125)
# without the review corpus or NumPy/Pillow -- the CI runners have neither.
ADD_TEST(NAME OracleGlyphReader
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/eqemu/oracle/tests/oracle_glyphs_test.py)
SET_TESTS_PROPERTIES(OracleGlyphReader PROPERTIES LABELS "hygiene"
    SKIP_RETURN_CODE 125 TIMEOUT 900)
