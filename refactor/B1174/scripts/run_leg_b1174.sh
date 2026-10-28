#!/usr/bin/env bash
# B1174 (#493): the record-325 /disc leg on lane 5 (run on hermes-ext, from the
# worktree ~/src/willeq-wt/kb-b1174-record325). Pattern: refactor/B1162/scripts/
# run_leg_b1162.sh, plus the server-data hook (oracle_class_leg.sh
# --server-sql-file/--server-undo-sql-file/--server-dump-sql-file).
# usage: run_leg_b1174.sh <name>   (plan refactor/B1174/plans/<name>.tsv,
#        out ~/kb-evidence/kb-b1174-record325/oracle/<name>)
R=/home/user/kb-evidence/kb-b1174-record325
W=/home/user/src/willeq-wt/kb-b1174-record325
n=$1
mkdir -p "$R/oracle"
echo "$(date -u +%FT%TZ) leg $n start" >> $R/progress.log
EQEMU_LANE=5 bash "$W/scripts/eqemu/oracle/oracle_class_leg.sh" 5 5 "$W/refactor/B1174/plans/$n.tsv" "$R/oracle/$n" \
    --pre-sql-file "$W/refactor/B1174/sql/b1174_disc8604.sql" \
    --post-sql-file "$W/refactor/B1174/sql/b1174_disc8604.post.sql" \
    --server-sql-file "$W/refactor/B1174/sql/b1174_server_8604.sql" \
    --server-undo-sql-file "$W/refactor/B1174/sql/b1174_server_8604.undo.sql" \
    --server-dump-sql-file "$W/refactor/B1174/sql/b1174_server_8604.dump.sql" \
    > "$R/oracle/$n.run.out" 2>&1
rc=$?
echo "$(date -u +%FT%TZ) leg $n rc=$rc ($(tail -1 "$R/oracle/$n/driver.log" 2>/dev/null))" >> $R/progress.log
exit $rc
