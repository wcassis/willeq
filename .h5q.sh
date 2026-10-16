#!/bin/bash
source /home/user/src/willeq-wt/kb-h5-hotbar-scenario/scripts/eqemu/env.sh
source /home/user/src/willeq-wt/kb-h5-hotbar-scenario/scripts/eqemu/restore_lib.sh
echo "== Testcaster mem_spells (from character_data if col exists) =="
eqemu_sql "SHOW COLUMNS FROM character_data;" | awk '{print $1}' | grep -i "mem\|spell" | tr '\n' ' '; echo
echo "== character_memmed_spells rows =="
eqemu_sql "SELECT * FROM character_memmed_spells LIMIT 10;"
echo "== itemtype 103 sample names (is it potion?) =="
eqemu_sql "SELECT itemtype, Name FROM items WHERE itemtype=103 LIMIT 8;"
echo "== itemtype 14/15/21 names =="
eqemu_sql "SELECT itemtype, Name FROM items WHERE itemtype IN (14,15,21) LIMIT 6;"
echo "== consumable with charges + reclevel<=2 (broad: any itemtype with maxcharges) =="
eqemu_sql "SELECT id,Name,itemtype,maxcharges,clickeffect,reclevel FROM items WHERE maxcharges>0 AND reclevel<=2 AND itemtype IN (103,14,15,21) ORDER BY reclevel,id LIMIT 20;"
echo "== eqstr 170/171 fishing success =="
sed -n '171,173p' /mnt/projects/EverQuestP1999/eqstr_us.txt
echo "== zone 96/36 type =="
eqemu_sql "SELECT id,short_name,type FROM zone WHERE id IN (96,36,54);"
